/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "resources.h"
#include "board_tables.h"
#include "power.h"
#include "guard.h"
#include "boot_control.h"
#include "sensor_rx.h"
#include "SWM341_timr.h"
#include "scene.h"
#include "lvgl.h"
#include <string.h>
#define IRAM __attribute__((section(".iram.bss")))
volatile uint32_t board_ms IRAM;
board_watchdog_state board_inherited_watchdog IRAM;
static uint32_t watchdog_fed_at IRAM;
void board_watchdog_feed(void) {
  /* Feed from progressing foreground code, never the timer ISR. The manual
   * requires at least five WDT clocks between feeds. 250 ms also exceeds
   * five divided clocks at the selected LRC / 32 rate. */
  if ((WDT->CR & WDT_CR_EN_Msk) &&
      (uint32_t)(board_ms - watchdog_fed_at) >= 250) {
    WDT->FEED = 0x55;
    watchdog_fed_at = board_ms;
  }
}
static void watchdog_settle(void) {
  /* Before SystemInit the core rate is inherited. Even at the documented
   * 240 MHz maximum, this is > 300 us (SDK WDT_ReInit settling interval).
   * Do not depend on SysTick or the not-yet-initialized CyclesPerUs. */
  for (volatile unsigned i = 0; i < 80000; i++)
    __NOP();
}
static void watchdog_handoff(void) {
  board_inherited_watchdog.control = WDT->CR;
  board_inherited_watchdog.reset_value = WDT->RSTVAL;
  board_inherited_watchdog.clock = SYS->CLKSEL;
  board_inherited_watchdog.reset_status = SYS->RSTSR;
  SYS->CLKEN0 |= SYS_CLKEN0_WDT_Msk;
  /* Manual pp. 54/257: change clock source only while stopped, and wait
   * >= five source clocks after feeding before stopping. */
  WDT->CR &= ~(WDT_CR_WINEN_Msk | WDT_CR_INTEN_Msk);
  WDT->RSTVAL = 65535;
  if (WDT->CR & WDT_CR_EN_Msk) {
    WDT->FEED = 0x55;
    watchdog_settle();
  }
  WDT->CR &= ~WDT_CR_EN_Msk;
  SYS->LRCCR |= 1u << SYS_LRCCR_ON_Pos;
  SYS->CLKSEL = (SYS->CLKSEL & ~SYS_CLKSEL_WDT_Msk) |
                (2u << SYS_CLKSEL_WDT_Pos);
  WDT->CR = (4u << WDT_CR_CKDIV_Pos);
  WDT->IF = 1;
  NVIC_DisableIRQ(WDT_IRQn);
  WDT->CR |= WDT_CR_EN_Msk;
  /* A stopped inherited counter need not be zero. Start without reset output,
   * then feed and let that synchronize before selecting the new threshold. */
  watchdog_settle();
  WDT->FEED = 0x55;
  watchdog_settle();
  WDT->RSTVAL = 8u * 1024u;
  WDT->CR |= WDT_CR_RSTEN_Msk;
  /* W1C only the recorded watchdog reason; don't retain a stale WDT reset
   * forever or clear unrelated power-on evidence. */
  SYS->RSTSR = SYS_RSTSR_WDT_Msk;
  watchdog_fed_at = board_ms;
}
bool board_boot_read(void *context, uint32_t offset, void *out, size_t n) {
  (void)context;
  if (!out || !n || n > SCREEN_BOOT_CHUNK || offset >= SCREEN_BOOT_BYTES ||
      n > SCREEN_BOOT_BYTES - offset)
    return false;
  uint8_t *p = out;
  /* Address zero is a valid bus address here. Inline LDRB avoids C's null
   * dereference UB; no flash/IAP command is involved. */
  while (n--) {
    uint32_t byte;
    __asm volatile("ldrb %0, [%1]" : "=r"(byte) : "r"(offset) : "memory");
    *p++ = byte;
    offset++;
  }
  return true;
}
static uint8_t rx[4096] IRAM;
static volatile unsigned read_at IRAM, write_at IRAM;
static bool overflow IRAM;
static sensor_rx sensor_queues[3] IRAM;
static void sensor_interrupt(unsigned index,UART_TypeDef *u) {
  /* Drain the eight-byte hardware FIFO while NOR/font/render work runs. */
  unsigned budget=64;uint32_t byte;
  while(budget--&&!UART_IsRXFIFOEmpty(u)) {
    UART_ReadByte(u,&byte);sensor_rx_push(&sensor_queues[index],byte,board_ms);
  }
  UART_INTClr(u,UART_IT_RX_THR|UART_IT_RX_TOUT);
}
void UART1_Handler(void){sensor_interrupt(0,UART1);}
void UART2_Handler(void){sensor_interrupt(1,UART2);}
void UART3_Handler(void){sensor_interrupt(2,UART3);}
int board_sensor_receive(unsigned index) {
  if(index>=3)return -1;
  sensor_rx *q=&sensor_queues[index];int byte=sensor_rx_pop(q);
  if(byte==-2){uint32_t mask=__get_PRIMASK();__disable_irq();sensor_rx_clear(q);__set_PRIMASK(mask);}
  return byte;
}
void board_sensor_flush(void) {
  UART_TypeDef *ports[]={UART1,UART2,UART3};
  uint32_t mask=__get_PRIMASK();__disable_irq();
  for(unsigned i=0;i<3;i++) {
    sensor_rx_clear(&sensor_queues[i]);
    unsigned budget=64;uint32_t byte;
    while(budget--&&!UART_IsRXFIFOEmpty(ports[i]))UART_ReadByte(ports[i],&byte);
    UART_INTClr(ports[i],UART_IT_RX_THR|UART_IT_RX_TOUT);
  }
  __set_PRIMASK(mask);
}
static volatile bool lvgl_tick_active IRAM;
static uint32_t esp_wake_at IRAM;
void board_lvgl_tick_stop(void){lvgl_tick_active=false;__DMB();}
void board_lvgl_tick_start(void){
  uint32_t mask=__get_PRIMASK();__disable_irq();
  lv_tick_inc(board_ms);lvgl_tick_active=true;
  __set_PRIMASK(mask);
}
void SysTick_Handler(void) { board_ms++;if(lvgl_tick_active)lv_tick_inc(1); }
void board_idle(void) {
  /* Stock wifi task releases PA5 after its one-second communication tail.
   * Keep the line high until the UART FIFO has physically finished as well. */
  if ((uint32_t)(board_ms - esp_wake_at) >= 1000 &&
      !UART_IsTXBusy(UART0))
    GPIO_ClrBit(GPIOA, 5);
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  bool pending = overflow || read_at != write_at;
  for (unsigned i = 0; i < 3; i++)
    pending |= sensor_queues[i].overflow ||
               sensor_queues[i].read != sensor_queues[i].write;
  /* CPU sleep only: LCD scanout, SDRAM refresh and UART clocks keep running.
   * An IRQ arriving after the queue check remains pending and wakes WFI. */
  if (!pending) {
    __DSB();
    __WFI();
    __ISB();
  }
  __set_PRIMASK(mask);
}
void board_delay(unsigned ms) {
  uint32_t start = board_ms;
  while ((uint32_t)(board_ms - start) < ms)
    board_idle();
}
static void us(unsigned n) {
  for (volatile unsigned i = 0; i < n * (CyclesPerUs / 4 + 1); i++)
    __NOP();
}
static void serial(UART_TypeDef *u, unsigned rate) {
  UART_InitStructure c = {0};
  c.Baudrate = rate;
  c.DataBits = UART_DATA_8BIT;
  c.Parity = UART_PARITY_NONE;
  c.StopBits = UART_STOP_1BIT;
  UART_Init(u, &c);
  UART_Open(u);
}
void board_early(void) {
  SysTick->CTRL = 0;
  for (unsigned i = 0; i < 3; i++) {
    NVIC->ICER[i] = UINT32_MAX;
    NVIC->ICPR[i] = UINT32_MAX;
  }
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
  SCB->VTOR = 0x14000;
  SCB->SCR &= ~(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
  __DSB();
  __ISB();
  watchdog_handoff();
  SCB->CPACR |= 0xfu << 20;
  __DSB();
  __ISB();
  SystemInit();
  /* Official SimplPRNG example: mode 3 uses RCHF/RCLF, no external pins. */
  SYS->PRNGCR = 3u << SYS_PRNGCR_MODE_Pos;
  PORT_Init(PORTM, 0, PORTM_PIN0_UART0_RX, 1);
  PORT_Init(PORTM, 1, PORTM_PIN1_UART0_TX, 0);
  serial(UART0, 115200);
  UART_INTEn(UART0, UART_IT_RX_THR);
  NVIC_EnableIRQ(UART0_IRQn);
  SysTick_Config(SystemCoreClock / 1000);
  __enable_irq();
  GPIO_INIT(GPIOD, 11, GPIO_INPUT_PullUp);
  GPIO_INIT(GPIOD, 13, GPIO_INPUT_PullUp);
  /* AM1008L SET: keep the module off throughout the SRAM recovery window. */
  GPIO_ClrBit(GPIOD, 12);
  GPIO_INIT(GPIOD, 12, GPIO_OUTPUT);
  /* Factory 0x1a53a: PM9 input with both pulls disabled. An internal pull-up
   * can keep external-power detection high after USB is disconnected. */
  GPIO_INIT(GPIOM, 9, GPIO_INPUT);
  GPIO_INIT(GPIOM, 15, GPIO_INPUT_PullUp);
  /* Preload the power latch before switching its pad to output. */
  SYS->CLKEN0 |= SYS_CLKEN0_GPIOM_Msk;
  GPIO_SetBit(GPIOM, 7);
  GPIO_INIT(GPIOM, 7, GPIO_OUTPUT);
  GPIO_INIT(GPIOA, 5, GPIO_OUTPUT);
  GPIO_SetBit(GPIOA, 5);
  esp_wake_at = board_ms;
}
static volatile uint32_t nonce_fallback __attribute__((section(".noinit")));
uint32_t board_nonce(void *context) {
  (void)context;
  for (unsigned budget = 200000; budget; --budget)
    if (SYS->PRNGCR & SYS_PRNGCR_RDY_Msk) {
      uint32_t nonce = SYS->PRNGDL ^ SYS->PRNGDH;
      if (nonce)
        return nonce;
    }
  /* A session nonce prevents stale UART requests; it is not an auth secret.
   * Never disable the recovery service when the optional PRNG is unavailable.
   * Retained state changes across software resets, including early failures. */
  nonce_fallback += 0x9e3779b9u;
  uint32_t nonce = nonce_fallback ^ SysTick->VAL ^ board_ms;
  return nonce ? nonce : 1;
}
void UART0_Handler(void) {
  uint32_t c;
  while (!UART_IsRXFIFOEmpty(UART0)) {
    UART_ReadByte(UART0, &c);
    unsigned next = (write_at + 1) % sizeof(rx);
    if (next == read_at)
      overflow = true;
    else {
      rx[write_at] = c;
      write_at = next;
    }
  }
  UART_INTClr(UART0, UART_IT_RX_THR | UART_IT_RX_TOUT);
}
int board_receive(void) {
  if (overflow) {
    __disable_irq();
    read_at = write_at;
    overflow = false;
    __enable_irq();
    return -2;
  }
  if (read_at == write_at)
    return -1;
  int c = rx[read_at];
  read_at = (read_at + 1) % sizeof(rx);
  return c;
}
bool board_send(void *ctx, const char *s, size_t n) {
  (void)ctx;
  if (!n) return true;
  esp_wake_at = board_ms;
  if (!GPIO_GetBit(GPIOA, 5)) {
    GPIO_SetBit(GPIOA, 5);
    /* Factory ESP32 polls GPIO2 every 30 ms. Assert wake before the first
     * start bit so it can restore APB and leave automatic light sleep. */
    board_delay(40);
  }
  uint32_t at = board_ms;
  for (size_t i = 0; i < n; i++) {
    while (UART_IsTXFIFOFull(UART0)) {
      if ((uint32_t)(board_ms - at) > 2000) {
        esp_wake_at = board_ms;
        return false;
      }
    }
    UART_WriteByte(UART0, (uint8_t)s[i]);
  }
  esp_wake_at = board_ms;
  return true;
}
bool board_uart_baud(uint32_t baud){
  if(!media_rate_valid(baud))return false;
  uint32_t at=board_ms;
  while(UART_IsTXBusy(UART0))if((uint32_t)(board_ms-at)>100)return false;
  NVIC_DisableIRQ(UART0_IRQn);
  /* SWM34S manual p. 286: the divider is applied when CTRL.EN is enabled.
   * Reading BAUD back alone cannot prove the running UART has switched. */
  UART_Close(UART0);
  UART_SetBaudrate(UART0,baud);
  UART_Open(UART0);
  uint32_t actual=UART_GetBaudrate(UART0);
  NVIC_EnableIRQ(UART0_IRQn);
  return actual>baud*98u/100u&&actual<baud*102u/100u;
}
volatile uint32_t board_sdram_error IRAM;
bool board_sdram(void) {
  board_sdram_error = 0;
  for (unsigned i = 20; i < sizeof(board_mux) / sizeof(board_mux[0]); i++)
    PORT_Init((PORT_TypeDef *)board_mux[i].port, board_mux[i].pin,
              board_mux[i].function, board_mux[i].input);
  /* Factory six-byte ABI: size=0, divider=0, CAS=1, TRP=1, TRCD=1, TRC=8.
   * Translate to current SDK named fields; RefreshTime is a new SDK field. */
  SYS->CLKEN1 |= SYS_CLKEN1_SDRAM_Msk;
  unsigned cycles = CyclesPerUs;
  SDRAMC->TIM = (1u << SDRAMC_TIM_TRP_Pos) | (1u << SDRAMC_TIM_TRCD_Pos) |
                (8u << SDRAMC_TIM_TRC_Pos) |
                ((cycles * 200) << SDRAMC_TIM_T100US_Pos);
  SDRAMC->CFG = (SDRAM_SIZE_8MB << SDRAMC_CFG_SIZE_Pos) |
                (SDRAM_CASLATENCY_3 << SDRAMC_CFG_CASDELAY_Pos) |
                ((SystemCoreClock > 66000000u) << SDRAMC_CFG_HIGHFREQ_Pos);
  SDRAMC->T64 = (64000 / 4096 + 1) * cycles;
  SDRAMC->CR = SDRAMC_CR_PWRON_Msk;
  uint32_t start = board_ms;
  while (SDRAMC->CR & SDRAMC_CR_PWRON_Msk)
    if ((uint32_t)(board_ms - start) > 50) {
      board_sdram_error = 1;
      return false;
    }
  volatile uint32_t *p = (uint32_t *)0x80000000;
  for (unsigned i = 0; i < 2048; i++)
    p[i] = 0xa55a0000 ^ i;
  for (unsigned i = 0; i < 2048; i++)
    if (p[i] != (0xa55a0000 ^ i)) {
      board_sdram_error = 2;
      return false;
    }
  for (unsigned bit = 1; bit < 21; bit++)
    p[1u << bit] = 0x5a5a0000u | bit;
  p[0] = 0xa55aa55a;
  for (unsigned bit = 1; bit < 21; bit++)
    if (p[1u << bit] != (0x5a5a0000u | bit)) {
      board_sdram_error = 3;
      return false;
    }
  if (p[0] != 0xa55aa55a) {
    board_sdram_error = 4;
    return false;
  }
  return true;
}
static void panel(uint16_t word) {
  GPIO_ClrBit(GPIOA, 6);
  for (unsigned i = 0; i < 9; i++) {
    GPIO_ClrBit(GPIOD, 10);
    GPIO_WriteBit(GPIOC, 6, (word >> (8 - i)) & 1);
    us(1);
    GPIO_SetBit(GPIOD, 10);
    us(1);
  }
  GPIO_SetBit(GPIOA, 6);
  us(1);
}
void board_backlight(unsigned pct) {
  /* One ceiling covers startup, touch wake, settings and fault shutdown. It
   * does not alter the stored brightness or the stock PWM period/polarity. */
  if (pct > SCREEN_BACKLIGHT_MAX_PERCENT)
    pct = SCREEN_BACKLIGHT_MAX_PERCENT;
  PWM_SetHDuty(PWM3, PWM_CH_A, pct * 100);
}
unsigned board_backlight_percent(void) {
  return PWM_GetHDuty(PWM3, PWM_CH_A) / 100;
}
/* LCD frame-end IRQ measures controller scan cadence, not an externally
 * probed VSYNC edge. TIMR4 is otherwise unused, counts continuously at 1 MHz.
 * Never change AUTORESTA: SDK IntEOTEn=1 would silently disable repeat scan. */
static volatile uint32_t lcd_frame_count IRAM,lcd_period_count IRAM,
    lcd_last_us IRAM,lcd_period_min IRAM,lcd_period_max IRAM;
static volatile uint64_t lcd_period_total IRAM;
static uint32_t timer_last IRAM,timer_high IRAM;
uint32_t board_time_us(void){
  /* TIMR has a 24-bit counter despite its 32-bit MMIO registers. Frame IRQs
   * sample it every scan; extend wraps without racing foreground callers. */
  uint32_t mask=__get_PRIMASK();__disable_irq();
  uint32_t raw=(~TIMR4->VALUE)&0xffffff;
  if(raw<timer_last)timer_high+=0x1000000;
  timer_last=raw;uint32_t now=timer_high|raw;
  __set_PRIMASK(mask);return now;
}
void LCD_Handler(void){
  if(!(LCD->IF&1))return;
  LCD->IF=1;
  uint32_t now=board_time_us();
  if(lcd_frame_count){
    uint32_t period=now-lcd_last_us;
    if(!lcd_period_count||period<lcd_period_min)lcd_period_min=period;
    if(period>lcd_period_max)lcd_period_max=period;
    lcd_period_total+=period;++lcd_period_count;
  }
  lcd_last_us=now;++lcd_frame_count;
}
void DMA2D_Handler(void) {
  /* Wake a sleeping flush waiter; foreground retains buffer ownership. */
  DMA2D->IF = DMA2D_IF_DONE_Msk;
}
__attribute__((noinline)) void board_display_diag(uint8_t p[128]){
  uint32_t mask=__get_PRIMASK();__disable_irq();
  uint32_t count=lcd_frame_count,periods=lcd_period_count,
      lo=lcd_period_min,hi=lcd_period_max;
  uint64_t total=lcd_period_total;
  __set_PRIMASK(mask);
  scene_put32(p+64,count);scene_put32(p+68,periods?(uint32_t)(total/periods):0);
  scene_put32(p+72,lo);scene_put32(p+76,hi);
  scene_put32(p+80,LCD->L[0].ADDR);scene_put32(p+92,LCD->CR);
  scene_put32(p+96,SystemCoreClock);
  scene_put32(p+100,((LCD->CR&LCD_CR_CLKDIV_Msk)>>LCD_CR_CLKDIV_Pos)+2);
  /* HSW/VSW are contained in the back porch, not extra scan intervals.
   * Decode the actual registers so diagnostics track the controller setup. */
  uint32_t h=LCD->CRH,v=LCD->CRV;
  scene_put32(p+104,((h&LCD_CRH_HBP_Msk)>>LCD_CRH_HBP_Pos)+
      ((h&LCD_CRH_PIX_Msk)>>LCD_CRH_PIX_Pos)+((h&LCD_CRH_HFP_Msk)>>LCD_CRH_HFP_Pos)+3);
  scene_put32(p+108,((v&LCD_CRV_VBP_Msk)>>LCD_CRV_VBP_Pos)+
      ((v&LCD_CRV_PIX_Msk)>>LCD_CRV_PIX_Pos)+((v&LCD_CRV_VFP_Msk)>>LCD_CRV_VFP_Pos)+3);
  scene_put32(p+112,1000000);scene_put32(p+116,1); // source: frame-end IRQ
  board_display_acceleration(p);
}
void board_display_present(const void *pixels) {
  /* SWM34S V1.39 p583: reload shadow layer registers at frame start;
   * hardware clears VBPRELOAD only after the reload has completed. */
  LCD->L[0].ADDR = (uint32_t)pixels;
  __DSB();
  LCD->CR |= LCD_CR_VBPRELOAD_Msk;
  __DSB();
}
bool board_display_present_pending(void) {
  __DSB();
  return (LCD->CR & LCD_CR_VBPRELOAD_Msk) != 0;
}
void board_display(void) {
  for (unsigned i = 0; i < 20; i++)
    PORT_Init((PORT_TypeDef *)board_mux[i].port, board_mux[i].pin,
              board_mux[i].function, board_mux[i].input);
  GPIO_INIT(GPIOA, 6, GPIO_OUTPUT);
  GPIO_INIT(GPIOA, 7, GPIO_OUTPUT);
  GPIO_INIT(GPIOC, 6, GPIO_OUTPUT);
  GPIO_INIT(GPIOD, 10, GPIO_OUTPUT);
  GPIO_SetBit(GPIOA, 6);
  GPIO_SetBit(GPIOC, 6);
  GPIO_SetBit(GPIOD, 10);
  GPIO_SetBit(GPIOA, 7);
  board_delay(10);
  GPIO_ClrBit(GPIOA, 7);
  board_delay(20);
  GPIO_SetBit(GPIOA, 7);
  board_delay(120);
  for (unsigned i = 0; i < sizeof(panel_words) / sizeof(panel_words[0]); i++) {
    panel(panel_words[i]);
    if (panel_words[i] == 0x11)
      board_delay(120);
    if (panel_words[i] == 0x29)
      board_delay(20);
  }
  LCD_InitStructure c = {0};
  /* 120 MHz / 8 / (560 * 894) = 29.96 Hz. Leave more SDRAM bus time
   * for rendering and DMA while retaining continuous panel scanout. */
  c.ClkDiv = 8;
  c.Format = LCD_FMT_RGB565;
  c.HnPixel = 480;
  c.VnPixel = 854;
  c.Hfp = 40;
  c.Hbp = 40;
  c.Vfp = 20;
  c.Vbp = 20;
  c.HsyncWidth = 10;
  c.VsyncWidth = 10;
  c.SampleEdge = LCD_SAMPLE_RISE;
  c.DataSource = 0x80000000;
  LCD_Init(LCD, &c);
  LCD_Start(LCD);
  TIMR_Init(TIMR4,TIMR_MODE_TIMER,CyclesPerUs,0x1000000,0);
  TIMR_Start(TIMR4);
  LCD->IF=1;NVIC_ClearPendingIRQ(LCD_IRQn);
  LCD->IE=1;NVIC_EnableIRQ(LCD_IRQn);
  PORT_Init(PORTM, 10, PORTM_PIN10_PWM3A, 0);
  PWM_InitStructure pwm = {0};
  pwm.Mode = PWM_EDGE_ALIGNED;
  pwm.Clkdiv = 8;
  pwm.Period = 10000;
  pwm.HdutyA = 0;
  PWM_Init(PWM3, &pwm);
  PWM_Start(PWM3_MSK);
  board_backlight(0);
}
/* The factory application uses SFC for reads, page programming and erases.
 * GPIO SPI is available only when explicitly selected during UART startup. */
static uint8_t spi(uint8_t value) {
  uint8_t result = 0;
  for (unsigned i = 0; i < 8; i++) {
    GPIO_ClrBit(GPIOD, 5);
    GPIO_WriteBit(GPIOD, 8, (value >> (7 - i)) & 1);
    us(1);
    GPIO_SetBit(GPIOD, 5);
    result = (result << 1) | GPIO_GetBit(GPIOD, 7);
    us(1);
  }
  return result;
}
static uint8_t nor_read_byte(void) {
  uint8_t value=0;
  for(unsigned bit=0;bit<8;bit++) {
    GPIOD->DATAPIN5=0;
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    GPIOD->DATAPIN5=1;
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    value=(value<<1)|(GPIOD->DATAPIN7&1);
  }
  return value;
}
static void cs(bool high) {
  GPIO_WriteBit(GPIOD, 6, high);
  us(1);
}
static void address(uint32_t a) {
  spi(a >> 16);
  spi(a >> 8);
  spi(a);
}
static uint8_t nor_read_mode IRAM;
static bool nor_quad_ready IRAM;
unsigned board_nor_read_mode(void) {return nor_read_mode;}
static void nor_gpio(void) {
  GPIO_SetBit(GPIOD,6);GPIO_SetBit(GPIOD,5);
  GPIO_INIT(GPIOD,6,GPIO_OUTPUT);GPIO_INIT(GPIOD,5,GPIO_OUTPUT);
  GPIO_INIT(GPIOD,8,GPIO_OUTPUT);GPIO_INIT(GPIOD,7,GPIO_INPUT);
  GPIO_SetBit(GPIOD,3);GPIO_SetBit(GPIOD,4);
  GPIO_INIT(GPIOD,3,GPIO_OUTPUT);GPIO_INIT(GPIOD,4,GPIO_OUTPUT);
}
static void nor_sfc_reset(void) {
  SYS->PRSTEN=0x55;
  SYS->PRSTR1|=SYS_PRSTR1_SFC_Msk;__DSB();
  SYS->PRSTR1&=~SYS_PRSTR1_SFC_Msk;__DSB();
  SYS->PRSTEN=0;
}
static void nor_sfc_config(bool quad) {
  SYS->CLKEN1|=SYS_CLKEN1_SFC_Msk;nor_sfc_reset();
  *((volatile uint32_t *)((uintptr_t)&SFC->CFG+0x3f4))=7;
  SFC->CFG=(3u<<SFC_CFG_CLKDIV_Pos)|SFC_CFG_CMDWREN_Msk|
      (quad?((2u<<SFC_CFG_DATA4L_RD_Pos)|SFC_CFG_DATA4L_PP_Msk):0);
  SFC->CMDAHB=((quad?0xebu:3u)<<24)|(5u<<16)|((quad?0x32u:2u)<<8)|6u;
  SFC->CFG&=~SFC_CFG_CMDWREN_Msk;
  /* Poll page programs promptly; erases use the longest WIP interval below. */
  SFC->TIM=(3u<<SFC_TIM_CSHIGH_Pos)|(3u<<SFC_TIM_IDLETO_Pos)|0xff01u;
  SFC->IE=0;SFC->IF=7;
  PORT_Init(PORTD,5,PORTD_PIN5_FSPI_SCLK,0);
  PORT_Init(PORTD,6,PORTD_PIN6_FSPI_SSEL,0);
  PORT_Init(PORTD,8,PORTD_PIN8_FSPI_MOSI,1);
  PORT_Init(PORTD,7,PORTD_PIN7_FSPI_MISO,1);
  PORT_Init(PORTD,3,PORTD_PIN3_FSPI_DATA2,1);
  PORT_Init(PORTD,4,PORTD_PIN4_FSPI_DATA3,1);
  __DSB();
}
bool board_nor_mode(void *ctx,unsigned mode) {
  (void)ctx;
  if(mode<GUARD_NOR_GPIO||mode>GUARD_NOR_SFC_QUAD)return false;
  nor_read_mode=mode;nor_quad_ready=false;
  if(mode==GUARD_NOR_GPIO)nor_gpio();
  else nor_sfc_config(mode==GUARD_NOR_SFC_QUAD);
  return true;
}
void board_nor_init(void) {
  board_nor_mode(NULL,GUARD_NOR_SFC_QUAD);
}
static bool nor_sfc_idle(unsigned timeout) {
  uint32_t start=board_ms;
  for(unsigned budget=timeout*100000u;budget;budget--) {
    if(!(SFC->SR&SFC_SR_BUSY_Msk)&&!SFC->GO)return true;
    if((uint32_t)(board_ms-start)>=timeout)break;
    board_watchdog_feed();
  }
  return false;
}
/* Register commands use the SFC even when the command itself is single-line. */
static bool nor_sfc_command(uint8_t cmd,unsigned type,uint32_t a,uint32_t data,unsigned timeout) {
  if(!nor_sfc_idle(3))return false;
  uint32_t timing=SFC->TIM;
  if(type==7)SFC->TIM=(timing&~0xffffu)|0xffffu;
  SFC->IF=7;
  SFC->CFG=(SFC->CFG&~(SFC_CFG_CMDTYPE_Msk|SFC_CFG_WREN_Msk))|
      SFC_CFG_CMDWREN_Msk|(type<<SFC_CFG_CMDTYPE_Pos)|
      ((type==6||type==7)?SFC_CFG_WREN_Msk:0);
  SFC->ADDR=a;SFC->DATA=data;SFC->CMD=cmd;
  SFC->GO=1;__DSB();__ISB();
  /* A short status write may finish before the first WIP sample. The QE
   * caller verifies its resulting status; TO still means failure. */
  bool ok=nor_sfc_idle(timeout)&&!(SFC->IF&
      (SFC_IF_TO_Msk|(type==6?0:SFC_IF_ERR_Msk)));
  SFC->CFG&=~(SFC_CFG_WREN_Msk|SFC_CFG_CMDWREN_Msk);
  SFC->TIM=timing;
  return ok;
}
uint32_t board_jedec(void) {
  if(nor_read_mode!=GUARD_NOR_GPIO) {
    if(!nor_sfc_command(0x9f,2,0,0,3))return 0;
    /* Register DATA places the first wire byte in its low byte. Keep the
     * public JEDEC value in the same wire order as GPIO SPI. */
    uint32_t id=SFC->DATA;
    return ((id&255)<<16)|(id&0xff00)|((id>>16)&255);
  }
  cs(false);spi(0x9f);
  uint32_t id=spi(0);id=id<<8|spi(0);id=id<<8|spi(0);
  cs(true);return id;
}
uint8_t board_nor_status(unsigned reg) {
  uint8_t cmd=reg==2?0x35:0x05;
  if(nor_read_mode!=GUARD_NOR_GPIO)
    return nor_sfc_command(cmd,1,0,0,3)?(uint8_t)SFC->DATA:0xff;
  cs(false);spi(cmd);uint8_t s=spi(0);cs(true);return s;
}
static bool ready(unsigned timeout) {
  uint32_t start=board_ms;
  while(board_nor_status(1)&1) {
    if((uint32_t)(board_ms-start)>timeout)return false;
    board_watchdog_feed();
  }
  return true;
}
static bool nor_quad_enable(void) {
  if(nor_read_mode!=GUARD_NOR_SFC_QUAD||nor_quad_ready)return true;
  if(board_jedec()!=0x684018)return false;
  uint8_t sr1=board_nor_status(1),sr2=board_nor_status(2);
  /* Match SFC_Init's QE setup while preserving every protection bit. */
  if(!(sr2&2)) {
    if(!nor_sfc_command(1,6,0,(uint32_t)sr1|((uint32_t)(sr2|2)<<8),1000)||
       !ready(1000)||!(board_nor_status(2)&2))return false;
  }
  nor_quad_ready=true;return true;
}
static bool writable(void) {
  return board_jedec()==0x684018&&!(board_nor_status(1)&0x7c)&&
      !(board_nor_status(2)&0x40)&&ready(1000)&&nor_quad_enable();
}
static void nor_gpio_read(uint32_t a,void *out,size_t n) {
  cs(false);spi(3);address(a);GPIOD->DATAPIN8=0;
  uint8_t *p=out;while(n--)*p++=nor_read_byte();cs(true);
}
static bool nor_sfc_read(uint32_t a,void *out,size_t n) {
  if(!nor_sfc_idle(3))return false;
  SFC->IF=7;
  uint8_t *p=out;
  while(n) {
    uint32_t word=*(volatile const uint32_t *)(0x70000000u+(a&~3u));
    unsigned offset=a&3u,count=4-offset;if(count>n)count=n;
    for(unsigned i=0;i<count;i++)*p++=(uint8_t)(word>>(8*(offset+i)));
    a+=count;n-=count;
  }
  __DSB();return nor_sfc_idle(3)&&!(SFC->IF&(SFC_IF_ERR_Msk|SFC_IF_TO_Msk));
}
bool board_nor_read(void *ctx,uint32_t a,void *out,size_t n) {
  (void)ctx;
  if(a>0x1000000u||n>0x1000000u-a||(!out&&n))return false;
  if(!n)return true;
  if(!ready(1000)||!nor_quad_enable())return false;
  board_watchdog_feed();
  if(nor_read_mode==GUARD_NOR_GPIO){nor_gpio_read(a,out,n);return true;}
  return nor_sfc_read(a,out,n);
}
static bool nor_erase_block(uint32_t a,uint8_t cmd) {
  board_watchdog_feed();
  if(nor_read_mode==GUARD_NOR_GPIO) {
    cs(false);spi(6);cs(true);
    cs(false);spi(cmd);address(a);cs(true);
  } else if(!nor_sfc_command(cmd,7,a,0,5000)) {
    char notice[96];
    int n=snprintf(notice,sizeof(notice),
        "screen_ota_error sfc_erase %08lx %08lx\r\n",
        (unsigned long)a,(unsigned long)SFC->IF);
    board_send(NULL,notice,(size_t)n);
    return false;
  }
  return ready(5000);
}
bool board_nor_erase(void *ctx,uint32_t a,size_t n) {
  (void)ctx;
  if(a<0x780000||a>0x7e8000||(a&0x7fff)||(n&0x7fff)||n>0x68000||
     n>0x7e8000-a||!writable())return false;
  while(n) {
    if(!nor_erase_block(a,0x52))return false;
    a+=0x8000;n-=0x8000;
  }
  return true;
}
static bool nor_program(uint32_t a,const void *v,size_t n) {
  if(!writable())return false;
  const uint8_t *p=v;
  while(n) {
    size_t part=256-(a&255);if(part>n)part=n;
    if(nor_read_mode==GUARD_NOR_GPIO) {
      cs(false);spi(6);cs(true);
      cs(false);spi(2);address(a);
      for(size_t i=0;i<part;i++)spi(p[i]);
      cs(true);
    } else {
      SFC->IF=7;SFC->CFG|=SFC_CFG_WREN_Msk;
      /* SFC AHB writes are aligned words. FF padding preserves adjacent bits
       * for byte-sized boot flags and unaligned resource headers. */
      for(size_t i=0;i<part;) {
        unsigned offset=(a+i)&3u,count=4-offset;
        if(count>part-i)count=part-i;
        uint32_t word=0xffffffffu;
        memcpy((uint8_t *)&word+offset,p+i,count);
        *(volatile uint32_t *)(0x70000000u+((a+i)&~3u))=word;
        i+=count;
      }
      __DSB();bool ok=nor_sfc_idle(1000)&&!(SFC->IF&SFC_IF_TO_Msk);
      bool check_write=(SFC->IF&SFC_IF_ERR_Msk)!=0;
      SFC->CFG&=~SFC_CFG_WREN_Msk;
      if(!ok)return false;
      /* ERR means WIP was already clear at the first controller sample.
       * Accept a completed short program only after exact readback. */
      if(check_write) {
        uint8_t check[256];
        if(!ready(1000)||!nor_sfc_read(a,check,part)||memcmp(check,p,part))return false;
      }
    }
    if(!ready(1000))return false;
    board_watchdog_feed();a+=part;p+=part;n-=part;
  }
  return true;
}
bool board_nor_write(void *ctx,uint32_t a,const void *v,size_t n) {
  (void)ctx;
  return a>=0x780000&&a<=0x7e8000&&n<=0x7e8000-a&&nor_program(a,v,n);
}
/* Optional wallpaper banks are isolated from factory images, fonts and OTA. */
bool board_resource_write(void *ctx,uint32_t a,const void *v,size_t n) {
  (void)ctx;
  if(!((a>=0xa00000&&a<=0xa80000&&n<=0xa80000-a)||
       (a>=SCREEN_FONT_BASE&&a<=SCREEN_FONT_BASE+2*SCREEN_FONT_BANK&&n<=SCREEN_FONT_BASE+2*SCREEN_FONT_BANK-a)))return false;
  return nor_program(a,v,n);
}
bool board_resource_erase(void *ctx,uint32_t a,size_t n) {
  (void)ctx;
  if(!((a>=0xa00000&&a<0xa80000)||(a>=SCREEN_FONT_BASE&&a<SCREEN_FONT_BASE+2*SCREEN_FONT_BANK))||
     (a&4095)||n!=4096||!writable())return false;
  return nor_erase_block(a,0x20);
}
/* Dedicated factory flag operations; general OTA writes cannot reach here. */
bool board_flag_erase(void *ctx,uint32_t a,size_t n) {
  (void)ctx;
  return a==SCREEN_BOOT_FLAG&&n==SCREEN_BOOT_FLAG_BLOCK&&writable()&&nor_erase_block(a,0xd8);
}
bool board_flag_write(void *ctx,uint32_t a,const void *v,size_t n) {
  (void)ctx;
  return a==SCREEN_BOOT_FLAG&&n==1&&v&&*(const uint8_t *)v==0xa1&&nor_program(a,v,n);
}
static void sda(bool b) {
  GPIO_WriteBit(GPIOA, 0, b);
  us(5);
}
static void scl(bool b) {
  GPIO_WriteBit(GPIOA, 1, b);
  us(5);
}
static bool clock_high(void) {
  scl(true);
  unsigned n = 100;
  while (!GPIO_GetBit(GPIOA, 1) && n--)
    us(5);
  return GPIO_GetBit(GPIOA, 1) != 0;
}
static bool start(void) {
  sda(true);
  if (!clock_high() || !GPIO_GetBit(GPIOA, 0))
    return false;
  sda(false);
  scl(false);
  return true;
}
static void stop(void) {
  sda(false);
  scl(true);
  sda(true);
}
static bool byte(uint8_t v) {
  for (unsigned i = 0; i < 8; i++) {
    sda((v >> (7 - i)) & 1);
    if (!clock_high())
      return false;
    scl(false);
  }
  sda(true);
  if (!clock_high())
    return false;
  bool ack = !GPIO_GetBit(GPIOA, 0);
  scl(false);
  return ack;
}
bool board_sht_command(uint16_t cmd) {
  bool ok = start() && byte(0x88) && byte(cmd >> 8) && byte(cmd);
  stop();
  return ok;
}
bool board_sht_read(uint8_t out[6]) {
  if (!start() || !byte(0x89)) {
    stop();
    return false;
  }
  for (unsigned k = 0; k < 6; k++) {
    uint8_t v = 0;
    sda(true);
    for (unsigned i = 0; i < 8; i++) {
      if (!clock_high()) {
        stop();
        return false;
      }
      v = (v << 1) | GPIO_GetBit(GPIOA, 0);
      scl(false);
    }
    out[k] = v;
    sda(k == 5);
    if (!clock_high()) {
      stop();
      return false;
    }
    scl(false);
  }
  stop();
  return true;
}
void board_module_write(void *context, bool high) {
  (void)context;
  GPIO_WriteBit(GPIOD, 12, high);
}
bool board_module_read(void *context) {
  (void)context;
  return GPIO_GetBit(GPIOD, 12) != 0;
}
void board_sensor_init(void) {
  /* PD12 remains low until the display and business state are ready. */
  board_sensor_checkpoint(1);
  PORT_Init(PORTB, 0, PORTB_PIN0_UART1_TX, 0);
  board_sensor_checkpoint(2);
  PORT_Init(PORTN, 5, PORTN_PIN5_UART1_RX, 1);
  board_sensor_checkpoint(3);
  PORT_Init(PORTB, 11, PORTB_PIN11_UART2_TX, 0);
  board_sensor_checkpoint(4);
  PORT_Init(PORTB, 13, PORTB_PIN13_UART2_RX, 1);
  board_sensor_checkpoint(5);
  PORT_Init(PORTB, 3, PORTB_PIN3_UART3_TX, 0);
  board_sensor_checkpoint(6);
  PORT_Init(PORTB, 4, PORTB_PIN4_UART3_RX, 1);
  board_sensor_checkpoint(7);
  serial(UART1, 9600);
  board_sensor_checkpoint(8);
  serial(UART2, 9600);
  board_sensor_checkpoint(9);
  serial(UART3, 9600);
  board_sensor_flush();
  UART_INTEn(UART1,UART_IT_RX_THR);NVIC_EnableIRQ(UART1_IRQn);
#ifndef SCREEN_UART2_QUIET
  UART_INTEn(UART2,UART_IT_RX_THR);NVIC_EnableIRQ(UART2_IRQn);
#endif
  UART_INTEn(UART3,UART_IT_RX_THR);NVIC_EnableIRQ(UART3_IRQn);
  board_sensor_checkpoint(10);
  GPIO_INIT(GPIOA, 0, GPIO_OUTPUT_OpenDrain_PullUp);
  board_sensor_checkpoint(11);
  GPIO_INIT(GPIOA, 1, GPIO_OUTPUT_OpenDrain_PullUp);
  board_sensor_checkpoint(12);
  sda(true);
  scl(true);
  board_sensor_checkpoint(13);
  GPIO_INIT(GPION, 4, GPIO_OUTPUT);
  board_sensor_checkpoint(14);
  GPIO_SetBit(GPION, 4);
  board_sensor_checkpoint(15);
  PORT_Init(PORTD, 0, PORTD_PIN0_ADC1_CH1, 0);
  board_sensor_checkpoint(16);
  ADC_InitStructure a = {0};
  a.clk_src = ADC_CLKSRC_HRC_DIV8;
  a.samplAvg = ADC_AVG_SAMPLE1;
  ADC_Init(ADC1, &a);
  board_sensor_checkpoint(17);
  ADC_SEQ_InitStructure q = {0};
  q.channels = ADC_CH1;
  q.trig_src = ADC_TRIGGER_SW;
  q.conv_cnt = 1;
  q.samp_tim = ADC_SAMPLE_8CLOCK;
  ADC_SEQ_Init(ADC1, ADC_SEQ0, &q);
  board_sensor_checkpoint(18);
  ADC_Open(ADC1);
  board_sensor_checkpoint(19);
}
unsigned board_battery(void) {
  uint16_t samples[40];
  for (unsigned i = 0; i < 40; i++) {
    ADC_Start(ADC1, ADC_SEQ0);
    uint32_t start = board_ms;
    while (!ADC_DataAvailable(ADC1, ADC_SEQ0))
      if ((uint32_t)(board_ms - start) > 10)
        return 0;
    uint32_t ch;
    samples[i] = ADC_Read(ADC1, ADC_SEQ0, &ch);
  }
  return battery_trimmed_mv(samples, GPIO_GetBit(GPIOM, 9) != 0);
}
