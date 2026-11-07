/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Factory RTC stores local UTC+8 calendar values, independently of the UI's
 * configured timezone. Register layout is PCF8563-compatible; not a part ID. */
#define SCREEN_RTC_OFFSET 28800u
typedef enum {
  SCREEN_RTC_UNKNOWN,
  SCREEN_RTC_READY,
  SCREEN_RTC_INVALID,
  SCREEN_RTC_IO_ERROR,
  SCREEN_RTC_VERIFY_ERROR
} screen_rtc_status;
typedef struct {
  void *context;
  bool (*read)(void *, uint8_t, uint8_t *, size_t);
  bool (*write)(void *, uint8_t, const uint8_t *, size_t);
  screen_rtc_status status;
  bool pending;
  unsigned attempts;
  uint32_t epoch, at, next;
} screen_rtc;

/* Nine bytes: control/status 1 and 2, then seconds through years. */
bool screen_rtc_decode(const uint8_t[9], uint32_t *utc);
bool screen_rtc_encode(uint32_t utc, uint8_t calendar[7]);
bool screen_rtc_load(screen_rtc *, uint32_t *utc);
/* Network time is authoritative. Tick retries at most three times and checks
 * readback; an already accurate RTC is not rewritten. No alarm/CLKOUT writes. */
void screen_rtc_request(screen_rtc *, uint32_t utc, uint32_t now);
void screen_rtc_tick(screen_rtc *, uint32_t now);
