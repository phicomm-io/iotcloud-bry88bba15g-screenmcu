#include "runtime.h"
#include "power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned sent;
static bool fail_send;
static screen_backlight last;
static bool send(void *context, const char *p, size_t n) {
  (void)context;
  if (fail_send)
    return false;
  uint8_t bytes[SCREEN_LINK_CHUNK];
  size_t length = sizeof(bytes);
  uint32_t session, sequence;
  assert(n >= 2 && p[n - 2] == '\r' && p[n - 1] == '\n');
  assert(screen_link_decode(p, n - 2, &session, &sequence, bytes, &length));
  assert(session == 123 && sequence == sent + 1);
  assert(screen_backlight_decode(&last, bytes, length));
  sent++;
  return true;
}
int main(void) {
  static screen_runtime s;
  screen_runtime_init(&s);
  s.send = send;
  assert(!screen_runtime_backlight(&s, 60, 10, 10, false));
  s.caps = (screen_caps){.session = 123, .ready = true,
                         .capabilities = SCREEN_CAP_SERVICES};
  assert(!screen_runtime_backlight(&s, 60, 10, 10, false));
  s.caps.capabilities |= SCREEN_CAP_BACKLIGHT;
  assert(screen_runtime_backlight(&s, 60, 10, 10, false));
  assert(last.requested == 60 && last.ceiling == 10 && last.applied == 10);
  assert(screen_runtime_backlight(&s, 5, 10, 5, false));
  assert(last.applied == 5 && !last.asleep);
  assert(screen_runtime_backlight(&s, 0, 10, 0, false));
  assert(screen_runtime_backlight(&s, 60, 10, 0, true));
  assert(last.asleep && !last.applied);
  assert(!screen_runtime_backlight(&s, 60, 10, 10, true));
  assert(!screen_runtime_backlight(&s, 101, 10, 10, false));
  assert(!screen_runtime_backlight(&s, 5, 10, 6, false));
  assert(!screen_runtime_backlight(&s, 60, 10, 11, false));
  assert(!screen_runtime_backlight(&s, 60, 101, 10, false));
  fail_send = true;
  uint32_t sequence = s.tx_sequence;
  assert(!screen_runtime_backlight(&s, 60, 10, 10, false));
  assert(s.tx_sequence == sequence);
  fail_send = false;
  assert(screen_runtime_backlight(&s, 60, 10, 10, false));
  s.caps.capabilities |= SCREEN_CAP_DISPLAY;
  display_policy policy; display_policy_default(&policy);
  display_configure(&s.display, &policy, 0);
  s.display.sleeping = true; s.display.reason = 1; s.display.target = 0;
  s.model.ota = true;
  assert(power_backlight_target(54, 100, false, true) == 10);
  assert(power_backlight_target(0, 100, true, true) == 10);
  assert(power_backlight_target(54, 5, false, true) == 5);
  assert(screen_runtime_display_report(&s, 54, 100, 10, 0));
  assert(last.requested == 54 && last.target == 10 && last.applied == 10 &&
         !last.asleep && last.reason == 0);
  assert(s.display.sleeping && s.display.target == 0 && s.display.reason == 1);
  s.model.ota = false;
  assert(screen_runtime_display_report(&s, 54, 100, 0, 0));
  assert(last.asleep && last.target == 0 && last.reason == 1);
  /* Notification wake reports the real PWM without changing saved policy. */
  assert(screen_runtime_display_report(&s, 0, 100, 30, 30));
  assert(last.requested == 0 && last.target == 30 && last.applied == 30 &&
         !last.asleep && last.reason == 0);
  assert(s.display.sleeping && s.display.target == 0 && s.display.reason == 1);
  assert(screen_runtime_display_report(&s, 0, 10, 10, 30));
  assert(last.target == 30 && last.ceiling == 10 && last.applied == 10);
  assert(!screen_runtime_display_report(&s, 0, 10, 11, 30));
  assert(screen_runtime_display_report(&s, 0, 100, 0, 0));
  assert(last.asleep && last.target == 0 && last.applied == 0);
  s.model.ota = true;
  assert(screen_runtime_display_report(&s, 0, 100, 10, 30));
  assert(last.target == 10 && last.applied == 10);
  s.model.ota = false;
  assert(power_backlight_target(54, 100, true, false) == 0);
  assert(power_backlight_target(54, 100, false, false) == 54);
  assert(power_backlight_target(54, 40, false, false) == 40);
  s.tx_sequence = UINT32_MAX;
  assert(!screen_runtime_backlight(&s, 60, 10, 10, false));

  uint8_t payload[SCREEN_BACKLIGHT_BYTES];
  assert(screen_backlight_encode(payload, 60, 10, 0, true));
  screen_backlight before = last;
  payload[5] = 2;
  assert(!screen_backlight_decode(&last, payload, sizeof(payload)));
  assert(!memcmp(&last, &before, sizeof(last)));
  payload[5] = 1;
  assert(!screen_backlight_decode(&last, payload, sizeof(payload) - 1));
  payload[1] = 2;
  assert(!screen_backlight_decode(&last, payload, sizeof(payload)));
  screen_power p = {.asleep = true, .brightness = 60,
                    .power_timeout = 2, .battery_timeout = 1};
  assert(power_tick(&p, true, false, true, false) & POWER_WAKE);
  assert(!(power_tick(&p, false, false, true, false) & POWER_PAGE));
  assert(p.brightness == 60 && p.power_timeout == 2 && p.battery_timeout == 1);
  puts("Backlight: negotiated-only telemetry, bounds, sleep/wake, zero, failed "
       "TX, sequence exhaustion and malformed frames passed");
}
