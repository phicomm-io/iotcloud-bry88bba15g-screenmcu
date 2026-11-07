/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtc.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t leap_end[9] = {0, 0, 0x59, 0x59, 0x23, 0x29, 4, 2, 0x24};
typedef struct {
  uint8_t bytes[16];
  unsigned reads, writes, fail_read, fail_write;
  bool absent, corrupt;
} chip;
static bool read_chip(void *context, uint8_t reg, uint8_t *out, size_t n) {
  chip *c = context;
  assert(reg == 0 && n == 9);
  if (++c->reads == c->fail_read || c->absent)
    return false;
  memcpy(out, c->bytes + reg, n);
  return true;
}
static bool write_chip(void *context, uint8_t reg, const uint8_t *in, size_t n) {
  chip *c = context;
  assert((reg == 2 && n == 7) || (reg == 0 && n == 1 && *in == 0));
  if (++c->writes == c->fail_write)
    return false;
  memcpy(c->bytes + reg, in, n);
  if (c->corrupt)
    c->bytes[4] = 0;
  return true;
}
static screen_rtc setup(chip *c) {
  memset(c, 0, sizeof(*c));
  memcpy(c->bytes, leap_end, 9);
  /* Alarm/status/CLKOUT/timer sentinels must survive every sync. */
  c->bytes[1] = 0x1b;
  for (unsigned i = 9; i < 16; i++)
    c->bytes[i] = (uint8_t)(0xa0 + i);
  return (screen_rtc){.context = c, .read = read_chip, .write = write_chip};
}
static void reserved(const chip *c) {
  assert(c->bytes[1] == 0x1b);
  for (unsigned i = 9; i < 16; i++)
    assert(c->bytes[i] == (uint8_t)(0xa0 + i));
}
static void calendar(void) {
  static const struct {uint32_t epoch; uint8_t b[7];} vectors[] = {
    {1700000000, {0x20, 0x13, 6, 0x15, 3, 0x11, 0x23}},
    {1709222399, {0x59, 0x59, 0x23, 0x29, 4, 2, 0x24}},
    {1709222400, {0, 0, 0, 1, 5, 3, 0x24}},
    {1735660800, {0, 0, 0, 1, 3, 1, 0x25}},
    {4102415999u, {0x59, 0x59, 0x23, 0x31, 4, 0x12, 0x99}}
  };
  for (unsigned i = 0; i < sizeof(vectors) / sizeof(*vectors); i++) {
    uint8_t b[9] = {0};
    uint32_t utc = 0;
    assert(screen_rtc_encode(vectors[i].epoch, b + 2));
    assert(!memcmp(b + 2, vectors[i].b, 7));
    assert(screen_rtc_decode(b, &utc) && utc == vectors[i].epoch);
  }
  uint8_t b[9];
  uint32_t utc = 123;
  assert(!screen_rtc_encode(1699999999, b));
  assert(!screen_rtc_encode(4102416000u, b));
  assert(!screen_rtc_encode(UINT32_MAX, b));
  static const struct {unsigned reg; uint8_t value;} invalid[] = {
    {0, 0x20}, {0, 0x80}, {0, 8}, {2, 0x80}, {2, 0x6a}, {2, 0x60},
    {3, 0x60}, {4, 0x24}, {5, 0}, {5, 0x30}, {6, 7}, {7, 0},
    {7, 0x13}, {7, 0x82}, {8, 0x23}, {8, 0x2a}
  };
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
    memcpy(b, leap_end, 9);
    b[invalid[i].reg] = invalid[i].value;
    assert(!screen_rtc_decode(b, &utc) && utc == 123);
  }
  memset(b, 0, 9);
  assert(!screen_rtc_decode(b, &utc));
  assert(!screen_rtc_decode(NULL, &utc));
  assert(!screen_rtc_decode(leap_end, NULL));
  /* Reserved bits read high are ignored, not accidentally treated as BCD. */
  memcpy(b, leap_end, 9);
  b[3] |= 0x80; b[4] |= 0xc0; b[5] |= 0xc0; b[6] |= 0xf8; b[7] |= 0x60;
  assert(screen_rtc_decode(b, &utc) && utc == 1709222399);
}
static void transactions(void) {
  chip c;
  screen_rtc r = setup(&c);
  uint32_t utc = 99;
  assert(screen_rtc_load(&r, &utc) && utc == 1709222399);
  assert(r.status == SCREEN_RTC_READY && !c.writes);
  screen_rtc_request(&r, utc + 1, 100);
  screen_rtc_tick(&r, 100);
  assert(!r.pending && !c.writes); /* Avoid unnecessary writes. */
  r = setup(&c);
  c.bytes[0] = 0xa8; c.bytes[2] |= 0x80;
  assert(!screen_rtc_load(&r, &utc) && r.status == SCREEN_RTC_INVALID);
  assert(!c.writes);
  screen_rtc_request(&r, 1735660800, 500);
  screen_rtc_tick(&r, 500);
  assert(r.status == SCREEN_RTC_READY && !r.pending && c.writes == 2);
  assert(!c.bytes[0] && screen_rtc_load(&r, &utc) && utc == 1735660800);
  reserved(&c);
  r = setup(&c); c.absent = true; utc = 99;
  assert(!screen_rtc_load(&r, &utc) && utc == 99);
  assert(r.status == SCREEN_RTC_IO_ERROR);
  c.reads = 0;
  screen_rtc_request(&r, 1735660800, UINT32_MAX - 499);
  screen_rtc_tick(&r, UINT32_MAX - 499);
  screen_rtc_tick(&r, 499); /* 999 ms elapsed, not due yet. */
  assert(c.reads == 1 && r.pending);
  screen_rtc_tick(&r, 500);
  assert(c.reads == 2 && r.pending);
  c.absent = false;
  screen_rtc_tick(&r, 1500);
  assert(!r.pending && r.status == SCREEN_RTC_READY && c.reads == 4);
  assert(screen_rtc_load(&r, &utc) && utc == 1735660802);
  reserved(&c);
  r = setup(&c); c.absent = true;
  screen_rtc_request(&r, 1735660800, 0);
  for (unsigned n = 0; n < 20000; n++) screen_rtc_tick(&r, n);
  assert(c.reads == 3 && !c.writes && !r.pending && r.attempts == 3);
  r = setup(&c); c.fail_write = 1;
  screen_rtc_request(&r, 1735660800, 0);
  screen_rtc_tick(&r, 0);
  assert(r.pending && r.status == SCREEN_RTC_IO_ERROR);
  screen_rtc_tick(&r, 1000);
  assert(!r.pending && r.status == SCREEN_RTC_READY);
  assert(screen_rtc_load(&r, &utc) && utc == 1735660801);
  r = setup(&c); c.fail_read = 2;
  screen_rtc_request(&r, 1735660800, 0);
  screen_rtc_tick(&r, 0);
  assert(r.pending && r.status == SCREEN_RTC_IO_ERROR);
  screen_rtc_tick(&r, 1000);
  assert(!r.pending && r.status == SCREEN_RTC_READY && c.writes == 1);
  r = setup(&c); c.corrupt = true;
  screen_rtc_request(&r, 1735664400, 0);
  for (unsigned n = 0; n <= 2000; n += 1000) screen_rtc_tick(&r, n);
  assert(!r.pending && r.status == SCREEN_RTC_VERIFY_ERROR && c.writes == 3);
  reserved(&c);
  screen_rtc_request(&r, 1, 0);
  assert(!r.pending && r.status == SCREEN_RTC_INVALID);
}
static void clock_runtime(void) {
  static screen_runtime s;
  screen_runtime_init(&s);
  assert(!s.model.clock_valid);
  assert(screen_runtime_clock(&s, 1709222399, 480, UINT32_MAX - 499, false));
  assert(s.model.hour == 23 && s.model.minute == 59 && !s.clock_revision);
  screen_runtime_tick(&s, 499);
  assert(s.clock_epoch == 1709222399);
  screen_runtime_tick(&s, 500);
  assert(s.clock_epoch == 1709222400 && !s.model.hour && !s.model.minute);
  screen_runtime_tick(&s, 3499);
  assert(s.clock_epoch == 1709222402 && s.clock_at == 2500);
  assert(screen_runtime_clock(&s, 1735660800, -300, 3500, true));
  assert(s.model.hour == 11 && s.model.minute == 0);
  assert(s.clock_revision == 1 && s.network_clock && s.network_clock_at == 3500);
  assert(!screen_runtime_clock(&s, 1, 480, 4000, true));
  assert(!screen_runtime_clock(&s, 1735660800, 841, 4000, true));
  assert(!screen_runtime_clock(&s, 1735660800, -721, 4000, true));
  assert(s.clock_revision == 1 && s.clock_epoch == 1735660800);
  assert(screen_runtime_clock(&s, 1735660801, -300, 4500, false));
  assert(s.clock_revision == 1 && s.network_clock_at == 3500);
  assert(screen_runtime_clock(&s, UINT32_MAX, 480, 5000, true));
  screen_runtime_tick(&s, 6000);
  assert(!s.model.clock_valid && !s.clock_epoch);
  screen_runtime_init(&s);
  uint8_t payload[7] = {SC_MSG_CLOCK};
  char frame[128];
  scene_put32(payload + 1, 1735660800);
  payload[5] = 0xe0; payload[6] = 1; /* UTC+8, little endian. */
  size_t n = screen_link_encode(frame, sizeof(frame), 17, 1, payload, 7);
  assert(n);
  screen_runtime_feed(&s, frame, n, 100);
  assert(!s.model.clock_valid && !s.clock_revision); /* No negotiation. */
  s.caps = (screen_caps){.session = 17, .capabilities = SCREEN_CAP_SERVICES,
                         .ready = true};
  screen_runtime_feed(&s, frame, n, 200);
  assert(s.model.clock_valid && s.clock_revision == 1 && s.clock_at == 200);
  assert(!s.model.hour && !s.model.minute && s.last_sequence == 1);
  screen_runtime_feed(&s, frame, n, 300);
  assert(s.clock_revision == 1 && s.clock_at == 200); /* Replay rejected. */
  payload[5] = 0xff; payload[6] = 0x7f;
  n = screen_link_encode(frame, sizeof(frame), 17, 2, payload, 7);
  screen_runtime_feed(&s, frame, n, 400);
  assert(s.clock_revision == 1 && s.last_sequence == 1); /* Invalid offset. */
}
int main(void) {
  calendar(); transactions(); clock_runtime();
  puts("RTC UTC+8 calendar, invalid power/BCD/date, preserved control registers, "
       "verified sync, bounded retries and wrap-safe unified clock passed");
}
