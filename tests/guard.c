#include "guard_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t boot[SCREEN_BOOT_BYTES], exported[SCREEN_BOOT_BYTES];
static char response[440];
static size_t response_size;
static unsigned resets;
static bool read_fail, send_fail;
static unsigned installs, restarts;
static bool install_fail;
static bool install(void *ctx) { (void)ctx; installs++; return !install_fail; }
static void restart(void *ctx) { (void)ctx; restarts++; }
static bool send_wire(void *ctx, const char *s, size_t n) {
  (void)ctx;
  if (send_fail)
    return false;
  assert(n <= sizeof(response));
  memcpy(response, s, n);
  response_size = n;
  return true;
}
static bool read_boot(void *ctx, uint32_t at, void *p, size_t n) {
  (void)ctx;
  assert(n && n <= SCREEN_BOOT_CHUNK && at + n <= sizeof(boot));
  if (read_fail)
    return false;
  memcpy(p, boot + at, n);
  return true;
}
static void reset(void *ctx) { (void)ctx; resets++; }
static size_t request(screen_guard *g, uint32_t seq, unsigned op,
                      uint32_t offset, unsigned count, bool ota,
                      unsigned result, uint8_t *out) {
  uint8_t p[8] = {SCREEN_GUARD_REQUEST, op};
  for (unsigned i = 0; i < 4; i++) p[2 + i] = offset >> (8 * i);
  p[6] = count; p[7] = count >> 8;
  char wire[80];
  size_t n = screen_link_encode(wire, sizeof(wire), g->session, seq, p, 8);
  assert(n && guard_receive(g, wire, n - 2, ota));
  uint8_t reply[11 + SCREEN_BOOT_CHUNK];
  size_t size = sizeof(reply);
  uint32_t sid, serial;
  assert(screen_link_decode(response, response_size - 2, &sid, &serial, reply, &size));
  assert(sid == g->session && serial == seq && size >= 11);
  assert(reply[0] == SCREEN_GUARD_REPLY && reply[1] == op && reply[2] == result);
  assert(reply[7] == 0 && reply[8] == 0x40 && reply[9] == 1 && reply[10] == 0);
  if (out) memcpy(out, reply + 11, size - 11);
  return size - 11;
}
int main(void) {
  /* Resume must end the window immediately, including across tick wrap. */
  screen_guard early = {.send = send_wire};
  guard_init(&early, 77, UINT32_MAX - 100, false);
  assert(guard_waiting(&early, 0, false));
  request(&early, 1, GUARD_RESUME, 0, 0, true, GUARD_BUSY, NULL);
  assert(guard_waiting(&early, 0, false));
  request(&early, 2, GUARD_RESUME, 0, 0, false, GUARD_OK, NULL);
  assert(!guard_waiting(&early, 0, false));
  assert(guard_waiting(&early, 0, true));
  request(&early, 3, GUARD_HOLD, 0, 0, false, GUARD_OK, NULL);
  assert(guard_waiting(&early, 0, false));
  request(&early, 4, GUARD_RESUME, 0, 0, false, GUARD_OK, NULL);
  assert(!guard_waiting(&early, 0, false));
  guard_init(&early, 78, 0, false);
  assert(!early.released && guard_waiting(&early, 1, false));
  screen_guard g = {.send = send_wire, .read_boot = read_boot, .reset = reset};
  guard_init(&g, 0x12345678, UINT32_MAX - 100, false);
  assert(guard_waiting(&g, 0, false));
  assert(!guard_waiting(&g, 29899, false));
  assert(guard_waiting(&g, 30000, true));
  request(&g, 1, GUARD_HOLD, 0, 0, false, GUARD_OK, NULL);
  assert(guard_waiting(&g, 60000, false));
  request(&g, 2, GUARD_RESUME, 0, 0, false, GUARD_OK, NULL);
  assert(!guard_waiting(&g, 60000, false));
  request(&g, 3, GUARD_READ_BOOT, SCREEN_BOOT_BYTES - 1, 2, false, GUARD_RANGE, NULL);
  request(&g, 4, GUARD_READ_BOOT, 0, 0, false, GUARD_RANGE, NULL);
  request(&g, 5, GUARD_READ_BOOT, 0, 257, false, GUARD_RANGE, NULL);
  request(&g, 6, GUARD_READ_BOOT, UINT32_MAX, 1, false, GUARD_RANGE, NULL);
  read_fail = true;
  request(&g, 7, GUARD_READ_BOOT, 0, 1, false, GUARD_IO, NULL);
  read_fail = false;
  request(&g, 8, GUARD_RESET, 0, 0, true, GUARD_BUSY, NULL);
  assert(!resets);
  /* The real byte codec is used for every chunk of two complete exports. */
  for (unsigned i = 0; i < sizeof(boot); i++) boot[i] = (i * 137) ^ (i >> 8);
  uint32_t seq = 9;
  for (unsigned pass = 0; pass < 2; pass++) {
    for (unsigned at = 0; at < sizeof(boot); at += SCREEN_BOOT_CHUNK)
      assert(request(&g, seq++, GUARD_READ_BOOT, at, SCREEN_BOOT_CHUNK,
                     false, GUARD_OK, exported + at) == SCREEN_BOOT_CHUNK);
    assert(!memcmp(boot, exported, sizeof(boot)));
  }
  assert(g.exported == sizeof(boot));
  g.normal = true;
  request(&g, seq++, GUARD_HOLD, 0, 0, false, GUARD_UNSUPPORTED, NULL);
  request(&g, seq++, GUARD_RESUME, 0, 0, false, GUARD_UNSUPPORTED, NULL);
  request(&g, seq++, GUARD_INSTALL, 0, 0, false, GUARD_UNSUPPORTED, NULL);
  g.install = install;
  g.restart = restart;
  request(&g, seq++, GUARD_INSTALL, 0, 0, true, GUARD_BUSY, NULL);
  assert(!installs && !restarts);
  install_fail = true;
  request(&g, seq++, GUARD_INSTALL, 0, 0, false, GUARD_IO, NULL);
  assert(installs == 1 && !restarts);
  install_fail = false;
  request(&g, seq++, GUARD_INSTALL, 0, 0, false, GUARD_OK, NULL);
  assert(installs == 2 && restarts == 1);
  request(&g, seq++, GUARD_RESET, 0, 0, false, GUARD_OK, NULL);
  assert(resets == 1);
  /* A replay cannot repeat reset; corrupt or stale-session frames cannot act. */
  uint8_t p[8] = {SCREEN_GUARD_REQUEST, GUARD_RESET};
  char wire[80];
  size_t n = screen_link_encode(wire, sizeof(wire), g.session, seq - 1, p, 8);
  assert(guard_receive(&g, wire, n - 2, false) && resets == 1);
  n = screen_link_encode(wire, sizeof(wire), g.session, seq, p, 8);
  wire[31] = wire[31] == '0' ? '1' : '0';
  assert(!guard_receive(&g, wire, n - 2, false));
  n = screen_link_encode(wire, sizeof(wire), g.session + 1, seq, p, 8);
  assert(!guard_receive(&g, wire, n - 2, false) && resets == 1);
  guard_init(&g, 0x87654321, 0, true);
  request(&g, 1, GUARD_RESUME, 0, 0, false, GUARD_UNSUPPORTED, NULL);
  /* A new session reuses the host's backup. The installation callback must
   * still validate the live bootloader, staged image and NOR copy tail. */
  assert(!g.exported);
  install_fail = true;
  request(&g, 2, GUARD_INSTALL, 0, 0, false, GUARD_IO, NULL);
  assert(installs == 3 && restarts == 1);
  install_fail = false;
  request(&g, 3, GUARD_INSTALL, 0, 0, false, GUARD_OK, NULL);
  assert(installs == 4 && restarts == 2 && !g.exported);
  assert(guard_waiting(&g, 100000, false));
  send_fail = true;
  n = screen_link_encode(wire, sizeof(wire), g.session, 4, p, 8);
  assert(guard_receive(&g, wire, n - 2, false) && resets == 1);
  puts("30s window, wrap, hold, forced recovery, 2x80KiB, CRC, replay, ranges and OTA exclusion passed");
}
