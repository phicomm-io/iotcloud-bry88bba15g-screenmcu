/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "factory.h"
#include "guard_service.h"
#include "boot_control.h"
#include "rtc.h"
#include "lvgl.h"
#include "resources.h"
#include "font_bank.h"
#include "responsive_font.h"
#include "wallpaper.h"
#include "sensor_rx.h"
#include "startup_ui.h"
#include "display_diag.h"
#include "media.h"
#include "stream.h"
#include "media_source.h"
#include "fast_image.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "screen_version.h"
#include <string.h>
#define IRAM __attribute__((section(".iram.bss")))
#define SCREEN_STRINGIFY_INNER(x) #x
#define SCREEN_STRINGIFY(x) SCREEN_STRINGIFY_INNER(x)
__attribute__((used, section(".firmware_identity")))
const char firmware_identity[] =
    "iotcloud-screen/BRY88BBA15G;application=0x14000;install=factory-uart;"
#ifdef SCREEN_RECOVERY_ONLY
    "mode=recovery;"
#else
    "mode=application;"
#endif
#ifdef SCREEN_STARTUP_DIAGNOSTIC
    "startup=observed;"
#endif
#ifdef SCREEN_UART2_QUIET
    "uart2=quiet;"
#endif
#if SCREEN_DMA2D
    "dma2d=on;"
#else
    "dma2d=off;"
#endif
#if SCREEN_BACKLIGHT_MAX_PERCENT < 100
    "backlight_max=" SCREEN_STRINGIFY(SCREEN_BACKLIGHT_MAX_PERCENT) ";"
#endif
    SCREEN_VERSION;
static const char screen_version[] = "iotcloud-screen-" SCREEN_VERSION;
static screen_maintenance maintenance IRAM;
static screen_guard guard IRAM;
static uint32_t guard_announce_at IRAM;
static bool normal_ready IRAM;
static bool panel_ready IRAM;
static screen_power recovery_power IRAM;
static uint32_t recovery_input_at IRAM;
static char running_md5[33] IRAM;
static const screen_boot_control boot_control = {
    .read = board_nor_read, .erase = board_flag_erase, .write = board_flag_write};
volatile uint32_t fault_latch __attribute__((section(".noinit")));
volatile uint32_t fault_latch_check __attribute__((section(".noinit")));
/* Stackless exception capture: stage, IPSR, CFSR, HFSR, MMFAR, BFAR, LR, MSP.
 * Optional PC/LR/xPSR loads are limited to a validated internal MSP frame;
 * the fault latch and reset also work when that frame is unusable. */
volatile uint32_t fault_record[11] __attribute__((section(".noinit")));
static uint32_t previous_fault[11] IRAM;
static uint32_t recovery_reason IRAM;
static uint32_t control_actual IRAM, control_expected IRAM, control_address IRAM;
static uint32_t sensor_step IRAM;
enum { STAGE_EARLY = 1, STAGE_WINDOW, STAGE_SDRAM, STAGE_BSS,
       STAGE_FACTORY, STAGE_RTC, STAGE_SENSORS, STAGE_LVGL, STAGE_PANEL,
       STAGE_NORMAL, STAGE_RTC_TICK, STAGE_SENSOR_TICK, STAGE_POWER,
       STAGE_RENDER };
static void stage(uint32_t value) { fault_record[0] = value; __DSB(); }
static void recovery_latch(bool held) {
  fault_latch_check = held ? ~0x52454356u : 0;
  fault_latch = held ? 0x52454356u : 0;
  __DSB();
}
static void power_off(void) {
  if (panel_ready) board_backlight(0);
  GPIO_ClrBit(GPIOD, 12);
  GPIO_ClrBit(GPIOA, 5);
  GPIO_ClrBit(GPIOM, 7);
  recovery_latch(false);
  /* Resetting with PD11 still held immediately reasserts PM7 at startup.
   * Let the original power latch drop the supply after the key is released.
   * If USB keeps the MCU powered, require a fresh two-second press to boot.
   * Keep feeding the watchdog so shutdown cannot turn into an 8 s restart. */
  while (!GPIO_GetBit(GPIOD, 11)) {
    board_watchdog_feed();
    __WFI();
  }
  uint32_t released_at = board_ms;
  for (;;) {
    if (GPIO_GetBit(GPIOD, 11))
      released_at = board_ms;
    else if ((uint32_t)(board_ms - released_at) >= 2000)
      NVIC_SystemReset();
    board_watchdog_feed();
    __WFI();
  }
}
static uint8_t resource_cache[SCREEN_RESOURCE_LIMIT];
static uint16_t wallpaper_cache[480*854];
static screen_runtime runtime;
static screen_power power;
static factory_owner factory;
static screen_rtc rtc;
static uint32_t rtc_revision, rtc_poll_at;
static uint32_t resource_now(void *c) { (void)c; return board_ms; }
static void resource_service(void *c);
static void rtc_start(void) {
  rtc.read = board_rtc_read;
  rtc.write = board_rtc_write;
  board_rtc_init();
  uint32_t utc;
  if (screen_rtc_load(&rtc, &utc))
    screen_runtime_clock(&runtime, utc, runtime.timezone, board_ms, false);
  rtc_poll_at = board_ms + 60000;
}
static void rtc_tick(uint32_t now) {
  if (rtc_revision != runtime.clock_revision) {
    rtc_revision = runtime.clock_revision;
    screen_rtc_request(&rtc, runtime.clock_epoch, now);
  }
  screen_rtc_tick(&rtc, now);
  if ((int32_t)(now - rtc_poll_at) < 0)
    return;
  rtc_poll_at = now + 60000;
  /* Do not replace a recent network sync with an older RTC value. */
  if (!rtc.pending && (!runtime.network_clock ||
      (rtc.status == SCREEN_RTC_READY &&
       (uint32_t)(now - runtime.network_clock_at) >= 120000))) {
    uint32_t utc;
    if (screen_rtc_load(&rtc, &utc))
      screen_runtime_clock(&runtime, utc, runtime.timezone, now, false);
  }
}
static lv_color_t framebuffer[2][480 * 854]
    __attribute__((section(".framebuffer"), aligned(64)));
static lv_color_t drawbuffer[2][480 * 40];
static lv_disp_draw_buf_t draw;
static lv_disp_drv_t display;
static screen_display_diag display_diagnostics;
static bool diag_redraw;
static bool display_dma_ready, display_dma_fault;
static uint32_t display_dma_flushes;
static unsigned display_front;
static lv_area_t display_dirty;
static bool display_dirty_valid;
static struct {
  lv_disp_drv_t *driver;
  lv_area_t area;
  lv_color_t *pixels;
  uint32_t started, started_ms;
  bool last;
  unsigned phase;
} pending_flush;
void board_display_acceleration(uint8_t p[128]) {
  scene_put32(p + 120, 1u | (display_dma_ready ? 2u : 0u) |
                            (draw.buf2 ? 4u : 0u) | 16u |
                            (display_dma_fault ? 8u : 0u) |
                            (!(SYS->CLKEN0 & SYS_CLKEN0_DMA2D_Msk) ? 32u : 0u) |
                            (!(SYS->CLKEN1 & SYS_CLKEN1_JPEG_Msk) ? 64u : 0u));
  scene_put32(p + 124, display_dma_flushes);
}
static void display_scan_failed(void) {
  /* Never release or overwrite a buffer while scanout may still own it. */
  recovery_reason = 7;
  board_recovery();
}
extern uint32_t __data_start__, __data_end__, __data_load__, __iram_start__,
    __iram_end__, __bss_start__, __bss_end__;
extern char __heap_start__, __heap_end__;
void *_sbrk(ptrdiff_t n) {
  static char *next;
  if (!next)
    next = &__heap_start__;
  if (n < 0 || n > &__heap_end__ - next) {
    errno = ENOMEM;
    return (void *)-1;
  }
  char *result = next;
  next += n;
  return result;
}
static void line(void *ctx, const char *p, size_t n) {
  (void)ctx;
  if (guard_receive(&guard, p, n, maintenance.state != 0)) {
    guard_announce_at = board_ms;
    return;
  }
  if (n == 17 && !memcmp(p, "+screen_recover:1", 17)) {
    guard.held = true;
    maintenance_query(&maintenance, board_ms);
    return;
  }
  if (normal_ready) {
    screen_runtime_feed(&runtime, p, n, board_ms);
    screen_runtime_feed(&runtime, "\r\n", 2, board_ms);
  }
}
static void guard_reset(void *context) {
  (void)context;
  board_module_write(NULL, false);
  for (unsigned i = 1; i < 11; i++)
    fault_record[i] = 0;
  recovery_latch(true);
  board_delay(30);
  NVIC_SystemReset();
}
static bool guard_install(void *context) {
  (void)context;
  if (normal_ready) screen_module_inhibit(&runtime.module, true);
  /* Installation may be requested while the diagnostic startup is held,
   * before the normal path acknowledges the preceding factory copy. */
  bool ok = maintenance.staged &&
            screen_boot_ack(&boot_control) &&
            screen_boot_commit(&boot_control, &maintenance.update);
  /* A lost reply must not leave a running application permanently inhibited. */
  if (normal_ready) screen_module_inhibit(&runtime.module, wallpaper.active || font_bank.active);
  return ok;
}
static void guard_restart(void *context) {
  (void)context;
  board_module_write(NULL, false);
  recovery_latch(false);
  board_delay(30);
  NVIC_SystemReset();
}
static uint32_t maintenance_clock(void *context) {
  (void)context;
  return board_ms;
}
static void sensors_reset(void);
static void media_board_maintenance_released(void);
static void maintenance_activity(void *context, bool active) {
  if (!active) media_board_maintenance_released();
  (void)context;
  if (normal_ready) {
    if (active) {
      screen_runtime_calibration_cancel(&runtime);
      runtime.calibration_valid_since[1] = 0;
    }
    power.ota = runtime.model.ota = active;
    runtime.model.asleep = active ? false : power.asleep;
    /* Apply before the first bounded NOR erase can block the normal tick. */
    if (active) board_backlight(power_backlight_target(0,
        runtime.display.configured ? runtime.display.policy.ceiling : 100,
        false, true));
    screen_module_inhibit(&runtime.module, active || wallpaper.active || font_bank.active);
    sensors_reset();
    screen_runtime_module_report(&runtime);
  }
  else board_module_write(NULL, false);
}
static void wallpaper_activity(void *context,bool active) {
  (void)context;
  if(normal_ready)screen_module_inhibit(&runtime.module,active || maintenance.state || wallpaper.active || font_bank.active);
}
static void maintenance_setup(void) {
  memset(&maintenance, 0, sizeof(maintenance));
  maintenance_init(&maintenance);
  maintenance.version = screen_version;
  maintenance.clock = maintenance_clock;
  maintenance.send = board_send;
  maintenance.line = line;
  maintenance.activity = maintenance_activity;
  maintenance.update.erase = board_nor_erase;
  maintenance.update.write = board_nor_write;
  maintenance.update.read = board_nor_read;
}
static void diagnostic_notice(void) {
  uint8_t bytes[SCREEN_GUARD_DIAGNOSTIC_V3_BYTES] = {SCREEN_GUARD_DIAGNOSTIC, 3};
  uint32_t words[20] = {recovery_reason, fault_record[0]};
  for (unsigned i = 0; i < 8; i++)
    words[2 + i] = previous_fault[i];
  words[10] = normal_ready ? (uint32_t)rtc.status : 0;
  words[11] = board_sdram_error;
  words[12] = board_ms;
  for (unsigned i = 0; i < 3; i++)
    words[13 + i] = previous_fault[8 + i];
  words[16] = control_actual;
  words[17] = control_expected;
  words[18] = control_address;
  words[19] = sensor_step;
  for (unsigned i = 0; i < 20; i++)
    for (unsigned j = 0; j < 4; j++)
      bytes[2 + i * 4 + j] = words[i] >> (j * 8);
  char wire[192];
  size_t n = screen_link_encode(wire, sizeof(wire), guard.session,
                                guard.sequence ? guard.sequence : 1,
                                bytes, sizeof(bytes));
  if (n)
    board_send(NULL, wire, n);
}
#include "media_board.inc"
static void pump(void) {
  for (unsigned budget = 0; budget < 4096; budget++) {
    int c = board_receive();
    if (c == -1)
      break;
    if (c == -2) {
      screen_update_abort(&maintenance.update);
      maintenance_init(&maintenance);
      continue;
    }
    /* Factory OTA owns raw bytes exclusively; the zero-delimited media
     * decoder consumes every binary byte before the maintenance text parser. */
    if(maintenance.state||!media_feed(&media_uart,(unsigned)c,board_ms))
      maintenance_feed(&maintenance, c, board_ms);
  }
  media_board_link_poll();
  maintenance_tick(&maintenance, board_ms);
  /* The recovery service runs without SDRAM or the normal key owner. Keep
   * long-press shutdown available, but ignore presses during a transfer. */
  if (!normal_ready && (uint32_t)(board_ms - recovery_input_at) >= 33) {
    recovery_input_at = board_ms;
    recovery_power.ota = maintenance.state != 0;
    if (power_tick(&recovery_power, !GPIO_GetBit(GPIOD, 11), false,
                   GPIO_GetBit(GPIOM, 9) != 0, false) & POWER_OFF)
      power_off();
  }
  if ((int32_t)(board_ms - guard_announce_at) >= 0) {
    char notice[192];
    unsigned mode = normal_ready ? 0 : guard.forced ? 3 : guard.held ? 2 : 1;
    uint32_t left = guard_waiting(&guard, board_ms, false) && !guard.held &&
                            !guard.forced ? guard.deadline - board_ms : 0;
    int n = snprintf(notice, sizeof(notice),
        "screen_recovery 3 %08lx %lu %u %lu %08lx %08lx %08lx %08lx\r\n",
        (unsigned long)guard.session, (unsigned long)guard.sequence, mode,
        (unsigned long)left,
        (unsigned long)board_inherited_watchdog.control,
        (unsigned long)board_inherited_watchdog.reset_value,
        (unsigned long)board_inherited_watchdog.clock,
        (unsigned long)board_inherited_watchdog.reset_status);
    board_send(NULL, notice, (size_t)n);
    if(!maintenance.state){
      n=snprintf(notice,sizeof(notice),"screen_media 1 %08lx %u 15 %lu\r\n",(unsigned long)guard.session,mode,(unsigned long)media_link_state.rate);
      board_send(NULL,notice,(size_t)n);
    }
    if (running_md5[0]) {
      n = snprintf(notice, sizeof(notice), "screen_image 1 %s\r\n", running_md5);
      board_send(NULL, notice, (size_t)n);
    }
    diagnostic_notice();
    guard_announce_at = board_ms + 5000;
  }
}
void board_recovery(void) {
  /* Do not dereference potentially damaged SDRAM during fault recovery. */
  board_lvgl_tick_stop();
  board_module_write(NULL, false);
  normal_ready = false;
  guard.forced = guard.held = true;
  guard_announce_at = board_ms;
  for (;;) {
    pump();
    board_watchdog_feed();
    board_idle();
  }
}
#include "display_dma.inc"
static void display_monitor(lv_disp_drv_t *d,uint32_t ms,uint32_t pixels){
  (void)d;screen_diag_monitor(&display_diagnostics,ms,pixels,board_time_us());
}
static void display_diag_poll(void){
  screen_diag_poll(&display_diagnostics,board_time_us());
  if(!runtime.diag_pending)return;
  const uint8_t *p=runtime.diag_request;
  unsigned status=0;
  if(p[2]==1){
    if(maintenance.state||power.asleep||!normal_ready)status=1;
    else if(p[3]==2&&(p[11]>runtime.model.scene.pages||
              !runtime.model.scene.page[p[11]-1].enabled))status=2;
    else if(p[3]==1){display_diagnostics.waiting=true;display_diagnostics.enabled=true;}
    else {
      if(p[3]==2)runtime.model.page=p[11]-1;
      screen_diag_begin(&display_diagnostics,runtime.model.page+1,board_time_us());
      diag_redraw=true;
    }
  } else if(p[2]==3){
    display_diagnostics.active=display_diagnostics.waiting=display_diagnostics.enabled=false;
  }
  uint8_t reply[SCREEN_DIAG_REPLY_MAX];
  size_t n=screen_diag_encode(&display_diagnostics,scene_u32(p+4),status,
                  p[8]|p[9]<<8,p[10],reply,sizeof(reply));
  if(n){
    board_display_diag(reply);
    scene_put32(reply+84,(uint32_t)drawbuffer);scene_put32(reply+88,480*40);
    char wire[768];size_t bytes=screen_link_encode(wire,sizeof(wire),runtime.caps.session,
                             runtime.tx_sequence+1,reply,n);
    if(bytes&&board_send(&factory,wire,bytes)){
      ++runtime.tx_sequence;runtime.diag_pending=false;
    }
  }
}
static bool sensor_send(UART_TypeDef *u, const uint8_t *p, size_t n) {
#ifdef SCREEN_UART2_QUIET
  /* Also reject calibration commands: no owner may transmit to this module
   * during the power-versus-protocol comparison. Keep its board pin setup. */
  if (u == UART2)
    return false;
#endif
  uint32_t start = board_ms;
  for (size_t i = 0; i < n; i++) {
    while (UART_IsTXFIFOFull(u))
      if ((uint32_t)(board_ms - start) > 100)
        return false;
    UART_WriteByte(u, p[i]);
  }
  return true;
}
bool factory_calibration(void *ctx, unsigned channel, unsigned event,
                         const uint8_t *p, size_t n) {
  (void)ctx;
  if (event == CAL_COMMAND)
    return sensor_send(channel == CAL_CO2 ? UART1 : UART2, p, n);
  if (event == CAL_SNAPSHOT)
    factory_snapshot(&factory, NULL, false);
  return true;
}
/* These callbacks must survive every peripheral initialization and render.
 * Read their actual SDRAM storage even with LTO: a damaged pointer must never
 * become a branch into unrelated boot flash. A failed check leaves the UART
 * recovery owner in internal SRAM and does not rewrite or repair SDRAM. */
static void control_check(void) {
  volatile screen_rtc *r = &rtc;
  volatile screen_runtime *s = &runtime;
  volatile factory_owner *f = &factory;
  uintptr_t actual = 0, expected = 0, address = 0;
  unsigned bad = 0;
  /* Capture the same volatile read that failed comparison. Reading it again
   * would lose evidence if the SDRAM error is intermittent. */
#define CHECK_CONTROL(member, target, error) do { \
    actual = (uintptr_t)(member); \
    expected = (uintptr_t)(target); \
    if (actual != expected) { \
      address = (uintptr_t)&(member); \
      bad = (error); \
      goto damaged; \
    } \
  } while (0)
  CHECK_CONTROL(r->read, board_rtc_read, 5);
  CHECK_CONTROL(r->write, board_rtc_write, 6);
  CHECK_CONTROL(r->context, NULL, 7);
  CHECK_CONTROL(s->send, board_send, 8);
  CHECK_CONTROL(s->factory, factory_line, 9);
  CHECK_CONTROL(s->context, &factory, 10);
  CHECK_CONTROL(s->calibration_event, factory_calibration, 11);
  CHECK_CONTROL(f->runtime, &runtime, 12);
  CHECK_CONTROL(f->maintenance, &maintenance, 13);
  CHECK_CONTROL(f->power, &power, 14);
  if (normal_ready) {
    CHECK_CONTROL(s->module.write, board_module_write, 15);
    CHECK_CONTROL(s->module.read, board_module_read, 16);
    CHECK_CONTROL(s->module.context, NULL, 17);
  }
#undef CHECK_CONTROL
  return;
damaged:
  {
    control_actual = actual;
    control_expected = expected;
    control_address = address;
    board_sdram_error = bad;
    recovery_reason = 6;
    if (panel_ready) {
      board_backlight(0);
      LCD_Stop(LCD);
    }
    board_recovery();
  }
}
void board_sensor_checkpoint(unsigned step) {
  sensor_step = step;
  control_check();
}
static void startup_checkpoint(void) {
  control_check();
#ifdef SCREEN_STARTUP_DIAGNOSTIC
  uint32_t until = board_ms + 5000;
  while ((int32_t)(board_ms - until) < 0 || maintenance.state || guard.held) {
    pump();
    control_check();
    board_watchdog_feed();
  }
#endif
}
static sensor_rx_frame frames[3];
static void sensors_reset(void) {
  board_sensor_flush();memset(frames,0,sizeof(frames));
}
static uint8_t hcho_status = 0x59;
static uint16_t tvoc_raw;
static void sensor_frame_read(unsigned index, uint32_t now) {
  sensor_rx_frame *f = &frames[index];
#ifdef SCREEN_UART2_QUIET
  /* UART2 IRQs remain disabled by board_sensor_init. Leave its FIFO alone so
   * attached-module data cannot reach the application parser in this build. */
  if (index == 1) {
    f->used = 0;
    return;
  }
#endif
  unsigned budget = SENSOR_RX_SIZE;
  while (budget--) {
    int entry=board_sensor_receive(index);
    if(entry==-1)break;
    unsigned n=sensor_rx_parse(f,entry);
    if (n) {
      uint16_t v;
      static const uint8_t calibration_ack[] = {0x16, 1, 3, 0xe6};
      if (index == 0 && n == sizeof(calibration_ack) &&
          !memcmp(f->bytes, calibration_ack, sizeof(calibration_ack)))
        screen_runtime_calibration_ack(&runtime);
      else if (index == 0 && sensor_uart_value(f->bytes, n, 1, &v) &&
          v <= 5000) {
        /* CM1106 status DF3: warm-up, sensor error, over/under range. The
         * uncalibrated flag itself must not prevent a manual calibration. */
        runtime.co2_not_ready = n >= 8 && (f->bytes[5] & 0x0f);
        screen_sample_set(&runtime.model, MET_CO2, v, true, now);
      }
      else if (index == 1 && n >= 12 &&
               sensor_uart_value(f->bytes, n, 0x16, &v)) {
        runtime.module.frames++;
        tvoc_raw = v;
        screen_sample_set(&runtime.model, MET_TVOC, fminf(v, 2000), true, now);
        unsigned raw = (unsigned)f->bytes[9] << 8 | f->bytes[10];
        unsigned coefficient = factory.settings[12] | factory.settings[13] << 8;
        if (coefficient >= 1 && coefficient <= 1000)
          screen_sample_set(&runtime.model, MET_PM25,
                            fminf(raw * 0.01f * coefficient, 300), true, now);
      } else if (index == 2 && sensor_hcho(f->bytes, n, &v, &hcho_status))
        screen_sample_set(&runtime.model, MET_HCHO, v, true, now);
      f->used = 0;
    }
  }
}
static void sensors_tick(uint32_t now) {
  static uint32_t next, read_at;
  static unsigned slot;
  static bool pending;
  for (unsigned i = 0; i < 3; i++)
    sensor_frame_read(i, now);
  if (pending && (int32_t)(now - read_at) >= 0) {
    uint8_t raw[6];
    if (board_sht_read(raw))
      screen_runtime_sht(&runtime, raw, hcho_status, true, false, now);
    pending = false;
  }
  if ((int32_t)(now - next) < 0)
    return;
  next = now + 400;
  static const uint8_t measure[] = {0x11, 1, 1, 0xed},
                       air[] = {0x11, 1, 0x16, 0xd8};
  switch (slot++ % 4) {
  case 0:
    sensor_send(UART3, measure, sizeof(measure));
    break;
  case 1:
    sensor_send(UART1, measure, sizeof(measure));
    break;
  case 2:
    pending = board_sht_command(0x2400);
    read_at = now + 20;
    break;
  case 3:
    /* Continue normal read requests during a manual SET-low lease, so missing
     * replies reflect the module hardware rather than a software pause. */
    sensor_send(UART2, air, sizeof(air));
    break;
  }
}
static void power_tick_board(void) {
  /* Stock PM9 is high on external power; low selects battery operation. */
  runtime.model.charging = GPIO_GetBit(GPIOM, 9) != 0;
  unsigned start = factory.settings[20] | factory.settings[21] << 8,
           end = factory.settings[22] | factory.settings[23] << 8;
  runtime.model.dnd =
      runtime.model.clock_valid &&
      power_in_dnd(runtime.model.hour * 60 + runtime.model.minute,
                   start / 100 * 60 + start % 100, end / 100 * 60 + end % 100,
                   (factory.settings[14] | factory.settings[15] << 8) != 0);
  power.ota = maintenance.state != 0;
  runtime.model.ota = power.ota;
  bool extended = runtime.display.configured && runtime.caps.ready &&
                  (runtime.caps.capabilities & SCREEN_CAP_DISPLAY);
  power.extended = extended;
  bool reminder = runtime.reminder_wake;
  unsigned reminder_page = runtime.model.page;
  runtime.reminder_wake = false;
  if (!extended && power.asleep && !runtime.model.asleep && !media_current(&media)) {
    power.asleep = false;
    power.idle = power.dnd_idle = 0;
  }
  bool front_down = !GPIO_GetBit(GPIOD, 11), rear_down = !GPIO_GetBit(GPIOD, 13);
  bool activity = front_down || rear_down || power.front || power.rear;
  bool notification=media_current(&media)&&media.presented&&!media.suspended;
  bool released=power.front&&!front_down&&!power.front_long;
  bool consumed=notification||media.tap_pending;
  if(notification)activity=false;
  if (extended) {
    uint8_t pages = 0;
    for (unsigned i = 0; i < runtime.model.scene.pages; i++)
      if (runtime.model.scene.page[i].enabled) pages |= 1u << i;
    display_step(&runtime.display, board_ms,
                  runtime.model.clock_valid ? runtime.clock_epoch : 0,
                  runtime.timezone, !runtime.model.charging, power.brightness,
                  pages, &runtime.model.page, false, false, power.ota);
    power.asleep = runtime.display.sleeping;
  }
  unsigned event =
      power_tick(&power, !GPIO_GetBit(GPIOD, 11), !GPIO_GetBit(GPIOD, 13),
                 runtime.model.charging, runtime.model.dnd);
  unsigned page_tap=media_button(&media,board_ms,front_down,released,
                                 (event & POWER_PAGE)!=0,power.ota||power.front_long);
  if (page_tap)
    screen_event(&runtime.model, 1);
  if (event & POWER_PROVISION)
    factory_provision(&factory);
  if (event & POWER_RESET)
    recovery_latch(false);
  if (event & POWER_RESET)
    NVIC_SystemReset();
  if (event & POWER_OFF) {
    power_off();
  }
  if (extended) {
    uint8_t pages = 0;
    for (unsigned i = 0; i < runtime.model.scene.pages; i++)
      if (runtime.model.scene.page[i].enabled) pages |= 1u << i;
    display_step(&runtime.display, board_ms,
                  runtime.model.clock_valid ? runtime.clock_epoch : 0,
                  runtime.timezone, !runtime.model.charging, power.brightness,
                  pages, &runtime.model.page, ((!notification&&!consumed)&&(event & POWER_WAKE)) || reminder,
                  activity, power.ota);
    if (reminder) runtime.model.page = reminder_page;
    power.asleep = runtime.display.sleeping;
  }
  runtime.model.asleep = power.ota ? false : power.asleep;
  unsigned target = extended ? runtime.display.target : power.asleep ? 0 : power.brightness;
  unsigned ceiling = extended ? runtime.display.policy.ceiling : 100;
  const media_notice *notice=media_current(&media);
  if(notice&&!media.suspended&&!power.ota){
    runtime.model.asleep=false;
    if(target<notice->brightness)target=notice->brightness;
    board_backlight(power_backlight_target(target,ceiling,false,false));
    return;
  }
  board_backlight(power_backlight_target(target, ceiling, power.asleep, power.ota));
}
static void resource_service(void *c) {
  (void)c; pump(); board_watchdog_feed();
}
void screen_start(void) {
  uint32_t *to = &__data_start__, *from = &__data_load__;
  while (to < &__data_end__)
    *to++ = *from++;
  for (to = &__iram_start__; to < &__iram_end__;)
    *to++ = 0;
  bool latched_fault = fault_latch == 0x52454356u &&
                       fault_latch_check == ~0x52454356u;
  uint32_t old_stage = fault_record[0];
  if ((latched_fault || (SYS->RSTSR & SYS_RSTSR_WDT_Msk)) &&
      old_stage >= STAGE_EARLY && old_stage <= STAGE_RENDER)
    for (unsigned i = 0; i < 11; i++)
      previous_fault[i] = fault_record[i];
  for (unsigned i = 0; i < 11; i++)
    fault_record[i] = 0;
  stage(STAGE_EARLY);
  board_early();
  recovery_power.front = !GPIO_GetBit(GPIOD, 11);
  recovery_power.front_long = recovery_power.front;
  board_nor_init();
  maintenance_setup();
  bool forced = latched_fault ||
                (board_inherited_watchdog.reset_status & SYS_RSTSR_WDT_Msk);
  recovery_reason = latched_fault ? 2 :
      (board_inherited_watchdog.reset_status & SYS_RSTSR_WDT_Msk) ? 3 : 0;
#ifdef SCREEN_RECOVERY_ONLY
  forced = true;
  recovery_reason = 4;
#endif
  memset(&guard, 0, sizeof(guard));
  guard.send = board_send;
  guard.read_boot = board_boot_read;
  guard.reset = guard_reset;
  guard.install = guard_install;
  guard.restart = guard_restart;
  guard.nor_mode = board_nor_mode;
  guard_init(&guard, board_nonce(NULL), board_ms, forced);
  media_link_state.rate=115200;media_uart.packet=media_board_packet;
  /* Hold the original power latch throughout the UART recovery window. No
   * LCD, SDRAM, sensor or factory-business initialization has run yet. */
  GPIO_SetBit(GPIOM, 7);
  stage(STAGE_WINDOW);
  pump();
  screen_md5 image_hash;
  uint8_t image_digest[16];
  screen_md5_init(&image_hash);
  for (uint32_t at = 0; at < SCREEN_APP_LIMIT; at += 1024) {
    screen_md5_update(&image_hash, (const void *)(SCREEN_APP_BASE + at),
                      SCREEN_APP_LIMIT - at < 1024 ? SCREEN_APP_LIMIT - at : 1024);
    board_watchdog_feed();
  }
  screen_md5_final(&image_hash, image_digest);
  for (unsigned i = 0; i < 16; i++)
    snprintf(running_md5 + i * 2, 3, "%02x", image_digest[i]);
#ifdef SCREEN_RECOVERY_ONLY
  board_recovery();
#endif
  while (guard_waiting(&guard, board_ms, maintenance.state != 0)) {
    pump();
    board_watchdog_feed();
    board_idle();
  }
  /* Let the ESP select a diagnostic bus before the first NOR access. */
  if (!screen_boot_ack(&boot_control))
    recovery_reason = 5;
  guard.normal = true;
  /* Only an actual exception or explicit recovery request sets this latch.
   * Marking every normal entry as a fault strands healthy resets when SRAM
   * survives. A stalled foreground still enters recovery via the WDT flag. */
  recovery_latch(false);
  stage(STAGE_SDRAM);
  if (!board_sdram()) {
    recovery_reason = 6;
    board_recovery();
  }
  stage(STAGE_BSS);
  for (to = &__bss_start__; to < &__bss_end__;)
    *to++ = 0;
  memset(framebuffer, 0, sizeof(framebuffer));
  board_jpeg_idle();
  screen_runtime_init(&runtime);
  media_init(&media);media.decode=board_jpeg_decode;
  stream_init(&stream);stream.decode=board_jpeg_decode;stream.send=media_board_socket;
  media_source_init(&media_source);
  runtime.send = board_send;
  runtime.factory = factory_line;
  runtime.context = &factory;
  runtime.calibration_event = factory_calibration;
  factory.runtime = &runtime;
  factory.maintenance = &maintenance;
  factory.power = &power;
  factory.version = screen_version;
  factory.nonce = board_nonce;
  factory.timezone = 480;
  stage(STAGE_FACTORY);
  factory_init(&factory, (const uint8_t *)0x7e000);
  stage(STAGE_RTC);
  rtc_start();
  startup_checkpoint();
  screen_font_resources = (screen_resources){.read=board_nor_read,
      .now=resource_now,.service=resource_service,.cache=resource_cache,
      .capacity=sizeof(resource_cache)};
  resources_start(&screen_font_resources);
  font_bank=(screen_font_bank){.read=board_nor_read,.write=board_resource_write,
      .erase=board_resource_erase,.now=resource_now,.activity=wallpaper_activity,
      .font=&screen_font_resources};
  font_bank_init(&font_bank);
  wallpaper=(screen_wallpaper){.read=board_nor_read,.write=board_resource_write,
      .erase=board_resource_erase,.now=resource_now,.service=resource_service,
      .activity=wallpaper_activity,.pixels=wallpaper_cache};
  wallpaper_init(&wallpaper);
  stage(STAGE_LVGL);
  lv_init();
  board_lvgl_tick_start();
  display_dma_init();
  /* Serialize pixel drawing and DMA reads to avoid three concurrent SDRAM
   * users (CPU, DMA2D and continuous LCD). The second strip is startup scratch. */
  lv_disp_draw_buf_init(&draw, drawbuffer[0], NULL, 480 * 40);
  lv_disp_drv_init(&display);
  display.hor_res = 480;
  display.ver_res = 854;
  display.draw_buf = &draw;
  display.flush_cb = flush;
  display.wait_cb = display_flush_wait;
  display.draw_ctx_init = screen_draw_ctx_init;
  display.monitor_cb = display_monitor;
  lv_disp_drv_register(&display);
  startup_checkpoint();
  stage(STAGE_PANEL);
  board_display();
  panel_ready = true;
  display_diagnostics.enabled=true;
  startup_checkpoint();
  /* Finish NOR reads and font validation before enabling sensor UARTs or
   * raising AM1008L SET. The loader services recovery UART and the watchdog;
   * absent/invalid fonts settle to the built-in font before sensors start. */
  resources_load(&screen_font_resources);
  while (maintenance.state || guard.held) {
    pump();
    board_watchdog_feed();
    board_idle();
  }
  stage(STAGE_SENSORS);
  board_sensor_init();
  board_sht_command(0x30a2);
  board_sensor_checkpoint(20);
  startup_checkpoint();
  screen_module_init(&runtime.module, NULL, board_module_write, board_module_read);
  normal_ready = true;
  screen_startup_begin(board_ms);
  stage(STAGE_NORMAL);
  /* The bootloader already entered this application. Applying the stock
   * PD11 cold-start gate here would power off every software OTA restart. */
  GPIO_SetBit(GPIOM, 7);
  board_backlight(power.brightness < 10 ? power.brightness : 10);
  startup_checkpoint();
  uint32_t next_input = board_ms, next_render = board_ms,
           next_sample = board_ms, next_cal = board_ms, next_status = board_ms;
  unsigned reported_page=~0u;bool reported_asleep=true;
  unsigned rendered_page = ~0u;
  bool rendered_asleep = true;
  uint32_t rendered_layout = 0, rendered_appearance = 0;
  bool rendered_wallpaper = false;
  unsigned rendered_font_state=~0u;uint32_t rendered_font_crc=0;
  for (;;) {
    control_check();
    pump();
    control_check();
    display_diag_poll();
    uint32_t now = board_ms;
    screen_runtime_tick(&runtime, now);
    bool media_busy=maintenance.state||wallpaper.active||font_bank.active;
    media_tick(&media,now,power.asleep||
      (runtime.display.configured?runtime.display.target==0:power.brightness==0),media_busy);
    bool stream_visible=false;
    for(unsigned i=0;i<runtime.model.scene.count;i++)
      if(runtime.model.scene.item[i].page==runtime.model.page&&runtime.model.scene.item[i].type==SC_IMAGE)stream_visible=true;
    media_board_socket_poll();
    media_source_poll(&media_source,&media,&stream,now,stream_visible,
      board_backlight_percent()&&!runtime.model.asleep&&!media_busy,media_link_state.ready);
    control_check();
    if (!maintenance.state) {
      font_bank_poll(&font_bank);
      if(!font_bank.active&&!wallpaper.active){resources_start(&screen_font_resources);resources_poll(&screen_font_resources);}
      wallpaper_poll(&wallpaper);
      stage(STAGE_RTC_TICK);
      rtc_tick(now);
      factory_tick(&factory, now);
      stage(STAGE_SENSOR_TICK);
      sensors_tick(now);
    }
    if ((int32_t)(now - next_input) >= 0) {
      next_input = now + 33;
      stage(STAGE_POWER);
      power_tick_board();
      if(!maintenance.state && (reported_page!=runtime.model.page||reported_asleep!=runtime.model.asleep) &&
          screen_runtime_page_report(&runtime,0,0)){
        reported_page=runtime.model.page;reported_asleep=runtime.model.asleep;
      }
    }
    if (!maintenance.state && (int32_t)(now - next_cal) >= 0) {
      next_cal = now + 100;
      screen_runtime_calibration_poll(&runtime, now / 100,
                                      runtime.model.samples[MET_CO2].value,
                                      tvoc_raw);
    }
    if ((int32_t)(now - next_status) >= 0) {
      /* Leave an idle gap after the stock one-second ESP wake tail; module
       * status remains younger than the ESP's five-second freshness limit.
       * Page changes and command responses still report immediately. */
      next_status = now + 2000;
      if (!maintenance.state){screen_runtime_module_report(&runtime);screen_runtime_page_report(&runtime,0,0);}
      if (!maintenance.state && (runtime.caps.capabilities & SCREEN_CAP_SCENE_V3) && runtime.caps.ready) {
        bool font_ready=screen_font_resources.state==RESOURCE_READY;
        bool io=(runtime.caps.capabilities&SCREEN_CAP_RESOURCE_IO)!=0;
        uint8_t report[24] = {0x2a,io?2:1,(!io&&screen_font_resources.state>5)?0:screen_font_resources.state,
          (font_ready&&(runtime.caps.capabilities&SCREEN_CAP_FONT_BANK))?screen_font_resources.bpp:0,
          font_ready?(screen_font_resources.count&255):0,font_ready?(screen_font_resources.count>>8):0};
        scene_put32(report+8,font_ready?screen_font_resources.bytes:0);
        scene_put32(report+12,font_ready?screen_font_resources.crc:0);
        if(io) {
          report[6]=board_nor_read_mode();
          scene_put32(report+16,screen_font_resources.elapsed_ms);
          scene_put32(report+20,screen_font_resources.received);
        }
        char line[128];
        size_t n = screen_link_encode(line, sizeof(line), runtime.caps.session,
                  ++runtime.tx_sequence, report, io?sizeof(report):16);
        if (n) board_send(&factory, line, n);
      }
      const media_notice *display_notice=media_current(&media);
      unsigned temporary_target=display_notice&&!media.suspended&&!power.ota ?
                                    display_notice->brightness : 0;
      if (runtime.display.configured)
        screen_runtime_display_report(&runtime, power.brightness,
             runtime.display.policy.ceiling < SCREEN_BACKLIGHT_MAX_PERCENT ?
                 runtime.display.policy.ceiling : SCREEN_BACKLIGHT_MAX_PERCENT,
             board_backlight_percent(), temporary_target);
      else
        screen_runtime_backlight(&runtime, power.brightness < temporary_target ?
             temporary_target : power.brightness,
             SCREEN_BACKLIGHT_MAX_PERCENT,
             board_backlight_percent(), power.asleep);
    }
    if (!maintenance.state && (int32_t)(now - next_sample) >= 0) {
      next_sample = now + 5000;
      unsigned mv = board_battery();
      runtime.model.battery_valid = mv > 0;
      runtime.model.battery_percent = battery_permille(mv) / 10;
    }
    static uint32_t rendered_media,rendered_stream;
    if (rendered_media!=media.revision||rendered_stream!=stream.revision||diag_redraw || (int32_t)(now - next_render) >= 0 ||
        rendered_page != runtime.model.page ||
        rendered_asleep != runtime.model.asleep ||
        rendered_layout != runtime.model.scene.revision ||
        rendered_appearance != runtime.model.appearance.revision ||
        rendered_wallpaper != wallpaper.ready ||
        rendered_font_state != screen_font_resources.state ||
        rendered_font_crc != screen_font_resources.crc) {
      if(rendered_page!=runtime.model.page && !diag_redraw && display_diagnostics.enabled)
        screen_diag_begin(&display_diagnostics,runtime.model.page+1,board_time_us());
      if(diag_redraw){lv_obj_invalidate(lv_scr_act());diag_redraw=false;}
      next_render = now + 1000;
      rendered_media=media.revision;rendered_stream=stream.revision;
      rendered_page = runtime.model.page;
      rendered_asleep = runtime.model.asleep;
      rendered_layout = runtime.model.scene.revision;
      rendered_appearance = runtime.model.appearance.revision;
      rendered_wallpaper = wallpaper.ready;
      if(rendered_font_state!=screen_font_resources.state||rendered_font_crc!=screen_font_resources.crc)screen_ui_font_invalidate();
      rendered_font_state=screen_font_resources.state;rendered_font_crc=screen_font_resources.crc;
      stage(STAGE_RENDER);
      screen_ui(&runtime.model, now, false);
    }
    screen_startup_ui(&runtime.model,board_ms);
    lv_timer_handler();
    /* Complete the final strip before reporting presentation or starting the
     * next foreground media decode. Strip drawing and DMA remain serialized. */
    while (!display_flush_poll()) { board_watchdog_feed(); board_idle(); }
    if(media_current(&media)&&!runtime.model.asleep&&!runtime.model.ota&&board_backlight_percent())media_present(&media);
    control_check();
    stage(STAGE_NORMAL);
    board_watchdog_feed();
    board_idle();
  }
}
