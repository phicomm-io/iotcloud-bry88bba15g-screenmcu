#include "module_control.h"
#include "scene.h"
#include <string.h>
static void output(screen_module *s, bool high) {
  bool before = s->read(s->context);
  s->write(s->context, high);
  if (before != s->read(s->context)) s->transitions++;
}
void screen_module_init(screen_module *s, void *context,
                        void (*write)(void *, bool), bool (*read)(void *)) {
  memset(s, 0, sizeof(*s));
  s->context = context; s->write = write; s->read = read;
  if (write && read) {
    write(context, true);
    /* The pad may still read low immediately after its first rising edge.
     * Availability describes installed GPIO callbacks, not that one sample.
     * Status and individual requests retain the actual pin readback. */
    s->initialized = true;
  }
}
void screen_module_restore(screen_module *s) {
  if (s->initialized) output(s, !s->inhibited);
  s->duration = 0;
}
void screen_module_inhibit(screen_module *s, bool inhibited) {
  if (s->inhibited == inhibited) return;
  s->inhibited = inhibited;
  screen_module_restore(s);
}
bool screen_module_tick(screen_module *s, uint32_t now) {
  if (!s->duration || (uint32_t)(now - s->started) < s->duration) return false;
  screen_module_restore(s);
  return true;
}
void screen_module_status(screen_module *s, uint32_t now, uint32_t request,
                          unsigned result, uint8_t out[SCREEN_MODULE_STATUS_BYTES]) {
  screen_module_tick(s, now);
  memset(out, 0, SCREEN_MODULE_STATUS_BYTES);
  out[0] = SCREEN_MSG_MODULE_STATUS; out[1] = 1; out[2] = result;
  out[3] = (s->initialized ? 1 : 0) |
           (s->read && s->read(s->context) ? 2 : 0) | (s->duration ? 4 : 0);
  scene_put32(out + 4, request);
  scene_put32(out + 8, s->duration ? s->duration - (uint32_t)(now - s->started) : 0);
  scene_put32(out + 12, s->frames); scene_put32(out + 16, s->transitions);
}
bool screen_module_request(screen_module *s, const uint8_t *p, size_t n,
                           uint32_t now, uint8_t out[SCREEN_MODULE_STATUS_BYTES]) {
  if (!p || n != SCREEN_MODULE_REQUEST_BYTES || p[0] != SCREEN_MSG_MODULE_REQUEST ||
      p[1] != 1 || !scene_u32(p + 4)) return false;
  screen_module_tick(s, now);
  uint32_t duration = scene_u32(p + 8);
  unsigned result = 0;
  if (p[3] || p[2] > SCREEN_MODULE_LOW ||
      (p[2] == SCREEN_MODULE_LOW ? !duration || duration > SCREEN_MODULE_MAX_MS : duration != 0))
    result = 1;
  else if (!s->initialized || (s->inhibited && p[2] != SCREEN_MODULE_QUERY)) result = 2;
  else if (p[2] != SCREEN_MODULE_QUERY) {
    bool high = p[2] == SCREEN_MODULE_HIGH;
    output(s, high);
    s->started = now; s->duration = high ? 0 : duration;
    if (s->read(s->context) != high) {
      screen_module_restore(s); result = 2;
    }
  }
  screen_module_status(s, now, scene_u32(p + 4), result, out);
  return true;
}
