#include "display.h"
#include "backlight.h"
#include <assert.h>
#include <string.h>
/* Fri 2026-11-06 local 23:00, UTC+8. */
#define FRIDAY_23 1793977200u
static void step(display_state *s, uint32_t ms, uint32_t utc, bool battery,
                  unsigned *page, bool wake) {
  display_step(s, ms, utc, 480, battery, 20, 0x3f, page, wake, wake, false);
}
int main(void) {
  display_policy p; display_policy_default(&p);
  p.idle_brightness = 5; p.idle_page = 0;
  display_state s = {0}; unsigned page = 2;
  display_configure(&s, &p, 0xffff0000u);
  step(&s, 0xffff0000u + 119999u, 0, false, &page, false);
  assert(!s.sleeping);
  step(&s, 0xffff0000u + 120000u, 0, false, &page, false);
  assert(s.sleeping && s.reason == 1 && s.target == 5 && page == 0);
  step(&s, 0xffff0000u + 120001u, 0, false, &page, true);
  assert(!s.sleeping && page == 2 && s.target == 20);
  display_configure(&s, &p, 0);
  step(&s, 60000, 0, true, &page, false);
  assert(s.sleeping); /* Battery timeout is independent. */
  p.external_minutes = 0;
  p.count = 2;
  p.plans[0] = (display_plan){true, 1u << 4, 8, 1, 1380, 420, "night"};
  p.plans[1] = (display_plan){true, 127, 3, 0, 0, 1439, "lower priority"};
  uint8_t wire[DISPLAY_POLICY_BYTES];
  assert(display_policy_encode(&p, wire, sizeof(wire)));
  display_policy copy;
  assert(display_policy_decode(&copy, wire, sizeof(wire)));
  assert(!memcmp(&copy, &p, sizeof(p)));
  wire[16 + 5] = 255;
  assert(!display_policy_decode(&copy, wire, sizeof(wire)));
  memset(&s, 0, sizeof(s)); display_configure(&s, &p, 0); page = 2;
  step(&s, 0, FRIDAY_23, false, &page, false);
  assert(s.sleeping && s.target == 8 && page == 1);
  step(&s, 1, FRIDAY_23, false, &page, true);
  assert(!s.sleeping && page == 2);
  step(&s, 60000, FRIDAY_23 + 60, false, &page, false);
  assert(!s.sleeping);
  step(&s, 60001, FRIDAY_23 + 60, false, &page, false);
  assert(s.sleeping && s.target == 8); /* Never-idle wake override lasts 1 min. */
  step(&s, 70000, FRIDAY_23 + 7 * 3600, false, &page, false);
  assert(s.sleeping && s.target == 8); /* Saturday uses Friday's weekday. */
  step(&s, 80000, FRIDAY_23 + 8 * 3600, false, &page, false);
  assert(s.sleeping && s.target == 3 && page == 0); /* End exclusive. */
  p.count = 1; display_configure(&s, &p, 90000);
  step(&s, 90000, FRIDAY_23 + 8 * 3600, false, &page, false);
  assert(!s.sleeping && page == 2);
  step(&s, 90001, FRIDAY_23 + 7 * 86400, false, &page, false);
  assert(s.sleeping && s.target == 8); /* Next occurrence ignores last week's wake. */
  unsigned saved = page;
  display_step(&s, 100000, FRIDAY_23 + 8 * 3600, 480, false, 20, 0x3f,
                &page, true, true, true);
  assert(s.sleeping && page == saved); /* OTA wins. */
  p.plans[0].days = 0; assert(!display_policy_valid(&p));
  p.plans[0].enabled = false; assert(display_policy_valid(&p));
  return 0;
}
