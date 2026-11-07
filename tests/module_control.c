#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool level, stuck;
static unsigned writes, sends;
static uint8_t reply[SCREEN_MODULE_STATUS_BYTES];
static void write_pin(void *ctx, bool high) {
  (void)ctx; writes++; if (!stuck) level = high;
}
static bool read_pin(void *ctx) { (void)ctx; return level; }
static bool send_line(void *ctx, const char *p, size_t n) {
  (void)ctx;
  if (n > 11 && !memcmp(p, "screen_ext ", 11)) {
    uint32_t session, sequence; size_t size = sizeof(reply);
    assert(screen_link_decode(p, n - 2, &session, &sequence, reply, &size));
    assert(size == sizeof(reply) && session == 123 && sequence);
    sends++;
  }
  return true;
}
static void request(uint8_t *p, unsigned action, uint32_t duration) {
  memset(p, 0, SCREEN_MODULE_REQUEST_BYTES);
  p[0] = SCREEN_MSG_MODULE_REQUEST; p[1] = 1; p[2] = action;
  scene_put32(p + 4, 7); scene_put32(p + 8, duration);
}
int main(void) {
  screen_module m; uint8_t p[12], out[20];
  /* A low first read must not permanently disable OTA's GPIO interlock. */
  stuck = true; level = false;
  screen_module_init(&m, NULL, write_pin, read_pin);
  assert(m.initialized && !level);
  stuck = false;
  screen_module_inhibit(&m, true);
  assert(!level && m.inhibited);
  screen_module_inhibit(&m, false);
  assert(level && !m.inhibited);
  writes = 0;
  screen_module_init(&m, NULL, write_pin, read_pin);
  assert(level && m.initialized && !m.duration && writes == 1);
  request(p, SCREEN_MODULE_LOW, 1000);
  for (unsigned n = 0; n < sizeof(p); n++)
    assert(!screen_module_request(&m, p, n, 0, out) && level);
  for (unsigned bad = 0; bad < 5; bad++) {
    request(p, SCREEN_MODULE_LOW, 1000);
    if (bad == 0) scene_put32(p + 8, 0);
    if (bad == 1) scene_put32(p + 8, SCREEN_MODULE_MAX_MS + 1);
    if (bad == 2) p[3] = 1;
    if (bad == 3) p[2] = 3;
    if (bad == 4) p[2] = SCREEN_MODULE_HIGH;
    assert(screen_module_request(&m, p, 12, 0, out));
    assert(out[2] == 1 && level && !m.duration);
  }
  request(p, SCREEN_MODULE_LOW, 1000);
  assert(screen_module_request(&m, p, 12, UINT32_MAX - 499, out));
  assert(out[2] == 0 && out[3] == 5 && scene_u32(out + 8) == 1000 && !level);
  assert(!screen_module_tick(&m, 499) && !level);
  assert(screen_module_tick(&m, 500) && level && !m.duration && m.transitions == 2);
  assert(!screen_module_tick(&m, 600));
  assert(screen_module_request(&m, p, 12, 700, out) && !level);
  screen_module_restore(&m); assert(level && !m.duration);
  stuck = true;
  assert(screen_module_request(&m, p, 12, 800, out) && out[2] == 2 && level && !m.duration);
  stuck = false;
  /* An OTA interlock cancels a manual lease and survives reset/expiry/high. */
  screen_module_inhibit(&m, true);
  assert(!level && m.inhibited && !m.duration);
  unsigned before = writes;
  screen_module_inhibit(&m, true); assert(writes == before);
  request(p, SCREEN_MODULE_HIGH, 0);
  assert(screen_module_request(&m, p, 12, 100000, out) && out[2] == 2 && !level);
  screen_module_restore(&m); assert(!level);
  assert(!screen_module_tick(&m, 200000) && !level);
  request(p, SCREEN_MODULE_QUERY, 0);
  assert(screen_module_request(&m, p, 12, 200001, out) && !out[2] && out[3] == 1);
  screen_module_inhibit(&m, false); assert(level && !m.inhibited);
  request(p, SCREEN_MODULE_LOW, 1000);
  static screen_runtime r; screen_runtime_init(&r);
  r.send = send_line;
  screen_module_init(&r.module, NULL, write_pin, read_pin);
  char wire[256], ack[80];
  size_t n = screen_link_encode(wire, sizeof(wire), 123, 1, p, sizeof(p));
  screen_runtime_feed(&r, wire, n, 0); assert(level && !sends);
  assert(screen_runtime_offer(&r, 123));
  size_t a = screen_caps_ack(ack, sizeof(ack), 123, SCREEN_CAP_MODULE_CONTROL);
  screen_runtime_feed(&r, ack, a, 0);
  n = screen_link_encode(wire, sizeof(wire), 124, 1, p, sizeof(p));
  screen_runtime_feed(&r, wire, n, 0); assert(level && !sends);
  n = screen_link_encode(wire, sizeof(wire), 123, 1, p, sizeof(p));
  screen_runtime_feed(&r, wire, n, 100); assert(!level && sends == 1);
  assert(scene_u32(reply + 4) == 7 && reply[3] == 5);
  screen_runtime_feed(&r, wire, n, 200); assert(sends == 1 && r.module.started == 100);
  screen_runtime_tick(&r, 1100); assert(level && sends == 2 && reply[3] == 3);
  assert(!scene_u32(reply + 4) && !scene_u32(reply + 8));
  n = screen_link_encode(wire, sizeof(wire), 123, 2, p, sizeof(p));
  screen_runtime_feed(&r, wire, n, 1200); assert(!level);
  screen_runtime_feed(&r, "+screen_link_reset:1\r\n", 22, 1300);
  assert(level && !r.module.duration && !r.caps.ready);
  screen_module_inhibit(&r.module, true);
  screen_runtime_feed(&r, "+screen_link_reset:1\r\n", 22, 1301);
  assert(!level && r.module.inhibited);
  screen_module_inhibit(&r.module, false);
  /* A stock peer never receives module status or changes PD12. */
  assert(screen_runtime_offer(&r, 456));
  a = screen_caps_ack(ack, sizeof(ack), 456, SCREEN_CAP_WIDGETS);
  screen_runtime_feed(&r, ack, a, 0);
  n = screen_link_encode(wire, sizeof(wire), 456, 1, p, sizeof(p));
  screen_runtime_feed(&r, wire, n, 1400); assert(level && !r.module.duration);
  assert(!screen_runtime_module_report(&r));
  puts("module: bounded lease, wrap, invalid requests, pin readback, session/replay, auto-restore and stock fallback PASS");
}
