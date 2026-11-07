/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"

/* Factory bit-banged RTC bus, separate from the PA0/PA1 SHT bus. Open-drain
 * output-high releases a line. No IRQ masking, SysTick waits or WDT feeding. */
static void pause_bus(void) {
  for (volatile unsigned i = 0; i < 5 * (CyclesPerUs / 4 + 1); i++)
    __NOP();
}
static bool clock_high(void) {
  GPIO_SetBit(GPIOD, 2);
  for (unsigned budget = 0; budget < 100; budget++) {
    if (GPIO_GetBit(GPIOD, 2))
      return true;
    pause_bus();
  }
  return false;
}
static void release_bus(void) {
  GPIO_SetBit(GPIOC, 7);
  GPIO_SetBit(GPIOD, 2);
}
static bool stop(void) {
  GPIO_ClrBit(GPIOD, 2);
  GPIO_ClrBit(GPIOC, 7);
  pause_bus();
  bool clock = clock_high();
  pause_bus();
  GPIO_SetBit(GPIOC, 7);
  pause_bus();
  return clock && GPIO_GetBit(GPIOC, 7);
}
static bool idle(void) {
  GPIO_SetBit(GPIOC, 7);
  if (!clock_high())
    return false;
  if (GPIO_GetBit(GPIOC, 7))
    return true;
  /* Finish an interrupted read, at most nine clocks, then issue STOP. */
  for (unsigned i = 0; i < 9; i++) {
    GPIO_ClrBit(GPIOD, 2);
    pause_bus();
    if (!clock_high())
      return false;
    pause_bus();
    if (GPIO_GetBit(GPIOC, 7))
      break;
  }
  return GPIO_GetBit(GPIOC, 7) && stop();
}
static bool start(void) {
  GPIO_SetBit(GPIOC, 7);
  pause_bus();
  if (!clock_high() || !GPIO_GetBit(GPIOC, 7))
    return false;
  pause_bus();
  GPIO_ClrBit(GPIOC, 7);
  pause_bus();
  GPIO_ClrBit(GPIOD, 2);
  return true;
}
static bool put(uint8_t byte) {
  for (unsigned i = 0; i < 8; i++, byte <<= 1) {
    if (byte & 0x80)
      GPIO_SetBit(GPIOC, 7);
    else
      GPIO_ClrBit(GPIOC, 7);
    pause_bus();
    if (!clock_high())
      return false;
    pause_bus();
    GPIO_ClrBit(GPIOD, 2);
  }
  GPIO_SetBit(GPIOC, 7);
  pause_bus();
  if (!clock_high())
    return false;
  pause_bus();
  bool ack = !GPIO_GetBit(GPIOC, 7);
  GPIO_ClrBit(GPIOD, 2);
  return ack;
}
static bool get(uint8_t *byte, bool more) {
  *byte = 0;
  GPIO_SetBit(GPIOC, 7);
  for (unsigned i = 0; i < 8; i++) {
    pause_bus();
    if (!clock_high())
      return false;
    pause_bus();
    *byte = (uint8_t)(*byte << 1 | GPIO_GetBit(GPIOC, 7));
    GPIO_ClrBit(GPIOD, 2);
  }
  if (more)
    GPIO_ClrBit(GPIOC, 7);
  pause_bus();
  if (!clock_high())
    return false;
  pause_bus();
  GPIO_ClrBit(GPIOD, 2);
  GPIO_SetBit(GPIOC, 7);
  return true;
}
void board_rtc_init(void) {
  SYS->CLKEN0 |= SYS_CLKEN0_GPIOC_Msk | SYS_CLKEN0_GPIOD_Msk;
  release_bus(); /* Preload both latches before enabling outputs. */
  /* SDK GPIO_Init sets DIR before OPEND: establish open-drain while input
   * first, so an externally held-low bus is never briefly driven high. */
  GPIO_INIT(GPIOD, 2, GPIO_OUTPUT_OpenDrain_PullUp & ~1u);
  GPIO_INIT(GPIOC, 7, GPIO_OUTPUT_OpenDrain_PullUp & ~1u);
  GPIO_INIT(GPIOD, 2, GPIO_OUTPUT_OpenDrain_PullUp);
  GPIO_INIT(GPIOC, 7, GPIO_OUTPUT_OpenDrain_PullUp);
}
bool board_rtc_read(void *context, uint8_t reg, uint8_t *out, size_t n) {
  (void)context;
  if (!out || !n || reg >= 9 || n > 9u - reg)
    return false;
  bool ok = idle() && start() && put(0xa2) && put(reg) && start() && put(0xa3);
  for (size_t i = 0; ok && i < n; i++)
    ok = get(out + i, i + 1 < n);
  bool stopped = stop();
  release_bus();
  return ok && stopped;
}
bool board_rtc_write(void *context, uint8_t reg, const uint8_t *data, size_t n) {
  (void)context;
  /* Only calendar and normal-mode recovery writes. No alarm, interrupt,
   * timer or CLKOUT modification through this driver. */
  if (!data || !((reg == 2 && n == 7) ||
                 (reg == 0 && n == 1 && data[0] == 0)))
    return false;
  bool ok = idle() && start() && put(0xa2) && put(reg);
  for (size_t i = 0; ok && i < n; i++)
    ok = put(data[i]);
  bool stopped = stop();
  release_bus();
  return ok && stopped;
}
