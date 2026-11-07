/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtc.h"
#include <limits.h>

static bool leap(unsigned year) { return year % 4 == 0; }
static unsigned month_days(unsigned year, unsigned month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  return days[month - 1] + (month == 2 && leap(year));
}
static bool unbcd(uint8_t b, unsigned max, unsigned *v) {
  if ((b & 15) > 9 || (b >> 4) > 9)
    return false;
  *v = (b >> 4) * 10 + (b & 15);
  return *v <= max;
}
static uint8_t bcd(unsigned v) { return (uint8_t)(v / 10 * 16 + v % 10); }

bool screen_rtc_decode(const uint8_t r[9], uint32_t *utc) {
  unsigned sec, min, hour, day, week, month, year;
  /* STOP/test mode, oscillator voltage-low flag and 1900s century are invalid.
   * Other reserved read bits are masked as prescribed by the register map. */
  if (!r || !utc || (r[0] & 0xa8) || (r[2] & 0x80) || (r[7] & 0x80) ||
      !unbcd(r[2] & 0x7f, 59, &sec) ||
      !unbcd(r[3] & 0x7f, 59, &min) ||
      !unbcd(r[4] & 0x3f, 23, &hour) ||
      !unbcd(r[5] & 0x3f, 31, &day) ||
      !unbcd(r[6] & 7, 6, &week) ||
      !unbcd(r[7] & 0x1f, 12, &month) ||
      !unbcd(r[8], 99, &year) || !month || !day)
    return false;
  year += 2000;
  if (day > month_days(year, month))
    return false;
  uint32_t days = 0;
  for (unsigned y = 2000; y < year; y++)
    days += leap(y) ? 366 : 365;
  for (unsigned m = 1; m < month; m++)
    days += month_days(year, m);
  uint32_t value = 946684800u + (days + day - 1) * 86400u + hour * 3600u +
                   min * 60u + sec - SCREEN_RTC_OFFSET;
  /* Match the existing UART time acceptance floor; reject power-on defaults. */
  if (value < 1700000000u)
    return false;
  *utc = value;
  return true;
}

bool screen_rtc_encode(uint32_t utc, uint8_t r[7]) {
  /* Last UTC second representable as a factory 20xx local calendar. */
  if (!r || utc < 1700000000u || utc > 4102415999u)
    return false;
  uint32_t local = utc + SCREEN_RTC_OFFSET;
  uint32_t days = (local - 946684800u) / 86400u;
  unsigned weekday = (days + 6) % 7, year = 2000, month = 1;
  while (days >= (leap(year) ? 366u : 365u)) {
    days -= leap(year) ? 366u : 365u;
    year++;
  }
  while (days >= month_days(year, month)) {
    days -= month_days(year, month);
    month++;
  }
  r[0] = bcd(local % 60);
  r[1] = bcd(local / 60 % 60);
  r[2] = bcd(local / 3600 % 24);
  r[3] = bcd(days + 1);
  r[4] = bcd(weekday);
  r[5] = bcd(month);
  r[6] = bcd(year - 2000);
  return true;
}

static bool registers(screen_rtc *r, uint8_t bytes[9]) {
  if (!r->read || !r->read(r->context, 0, bytes, 9)) {
    r->status = SCREEN_RTC_IO_ERROR;
    return false;
  }
  return true;
}
bool screen_rtc_load(screen_rtc *r, uint32_t *utc) {
  uint8_t bytes[9];
  if (!registers(r, bytes))
    return false;
  if (!screen_rtc_decode(bytes, utc)) {
    r->status = SCREEN_RTC_INVALID;
    return false;
  }
  r->status = SCREEN_RTC_READY;
  return true;
}
void screen_rtc_request(screen_rtc *r, uint32_t utc, uint32_t now) {
  uint8_t calendar[7];
  r->pending = screen_rtc_encode(utc, calendar);
  r->epoch = utc;
  r->at = r->next = now;
  r->attempts = 0;
  if (!r->pending)
    r->status = SCREEN_RTC_INVALID;
}
static bool synchronize(screen_rtc *r, uint32_t utc) {
  uint8_t bytes[9], calendar[7];
  uint32_t before;
  if (!screen_rtc_encode(utc, calendar)) {
    r->status = SCREEN_RTC_INVALID;
    return false;
  }
  if (!registers(r, bytes))
    return false; /* No response: do not blindly write to an absent device. */
  if (screen_rtc_decode(bytes, &before) &&
      (before > utc ? before - utc : utc - before) <= 1) {
    r->status = SCREEN_RTC_READY;
    return true;
  }
  if (!r->write || !r->write(r->context, 2, calendar, sizeof(calendar))) {
    r->status = SCREEN_RTC_IO_ERROR;
    return false;
  }
  /* A stopped/test-mode device is restarted only after a complete trusted
   * calendar write. Control/status 2, alarms and CLKOUT remain untouched. */
  uint8_t normal = 0;
  if ((bytes[0] & 0xa8) && !r->write(r->context, 0, &normal, 1)) {
    r->status = SCREEN_RTC_IO_ERROR;
    return false;
  }
  uint32_t after;
  if (!screen_rtc_load(r, &after))
    return false;
  if (after != utc && after != utc + 1) {
    r->status = SCREEN_RTC_VERIFY_ERROR;
    return false;
  }
  return true;
}
void screen_rtc_tick(screen_rtc *r, uint32_t now) {
  if (!r->pending || (int32_t)(now - r->next) < 0)
    return;
  uint32_t elapsed = (now - r->at) / 1000;
  if (elapsed > UINT32_MAX - r->epoch) {
    r->status = SCREEN_RTC_INVALID;
    r->pending = false;
    return;
  }
  if (synchronize(r, r->epoch + elapsed) || ++r->attempts == 3)
    r->pending = false;
  else
    r->next = now + 1000;
}
