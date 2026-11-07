#include "guard_service.h"
#include <string.h>
static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = v >> (8 * i);
}
void guard_init(screen_guard *g, uint32_t session, uint32_t now, bool forced) {
  g->session = session;
  g->sequence = g->exported = 0;
  g->deadline = now + SCREEN_GUARD_WINDOW;
  g->held = g->forced = forced;
  g->normal = false;
  g->released = false;
}
bool guard_waiting(const screen_guard *g, uint32_t now, bool ota) {
  return g->held || g->forced || ota ||
         (!g->released && (int32_t)(now - g->deadline) < 0);
}
bool guard_receive(screen_guard *g, const char *line, size_t size, bool ota) {
  uint8_t p[24], reply[11 + SCREEN_BOOT_CHUNK] = {SCREEN_GUARD_REPLY};
  uint32_t session, sequence;
  size_t n = sizeof(p);
  if (!g->session || !screen_link_decode(line, size, &session, &sequence, p, &n) ||
      session != g->session || !n || p[0] != SCREEN_GUARD_REQUEST)
    return false;
  if (sequence <= g->sequence)
    return true;
  g->sequence = sequence;
  reply[1] = n > 1 ? p[1] : 0;
  reply[2] = GUARD_RANGE;
  put32(reply + 7, SCREEN_BOOT_BYTES);
  uint32_t offset = n >= 8 ? u32(p + 2) : 0;
  unsigned count = n >= 8 ? (unsigned)p[6] | (unsigned)p[7] << 8 : 0;
  put32(reply + 3, offset);
  size_t bytes = 11;
  bool reset = false, install = false;
  if (n == 8 && ((!offset && !count) || p[1] == GUARD_READ_BOOT)) {
    reply[2] = ota ? GUARD_BUSY : GUARD_OK;
    if (!ota) {
      switch (p[1]) {
      case GUARD_HOLD:
        if (g->normal)
          reply[2] = GUARD_UNSUPPORTED;
        else
          g->held = true;
        break;
      case GUARD_RESUME:
        if (g->forced || g->normal)
          reply[2] = GUARD_UNSUPPORTED;
        else {
          g->held = false;
          g->released = true;
        }
        break;
      case GUARD_READ_BOOT:
        if (n != 8 || !count || count > SCREEN_BOOT_CHUNK ||
            offset >= SCREEN_BOOT_BYTES || count > SCREEN_BOOT_BYTES - offset)
          reply[2] = GUARD_RANGE;
        else if (!g->read_boot || !g->read_boot(g->context, offset, reply + 11, count))
          reply[2] = GUARD_IO;
        else {
          bytes += count;
          if (offset == g->exported)
            g->exported += count;
        }
        break;
      case GUARD_RESET:
        reset = g->reset != NULL;
        if (!reset)
          reply[2] = GUARD_UNSUPPORTED;
        break;
      case GUARD_INSTALL:
        /* Installation rechecks the staged application and factory-copy tail.
         * Boot exports are optional and never gate activation. */
        if (!g->install || !g->restart)
          reply[2] = GUARD_UNSUPPORTED;
        else if (!g->install(g->context))
          reply[2] = GUARD_IO;
        else
          install = true;
        break;
      default:
        reply[2] = GUARD_UNSUPPORTED;
      }
    }
  }
  char wire[440];
  size_t used = screen_link_encode(wire, sizeof(wire), g->session, sequence, reply, bytes);
  if (used && g->send && g->send(g->context, wire, used)) {
    if (reset)
      g->reset(g->context);
    if (install)
      g->restart(g->context);
  }
  return true;
}
