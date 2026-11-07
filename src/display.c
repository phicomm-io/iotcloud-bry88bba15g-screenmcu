#include "display.h"
#include <string.h>
void display_configure(display_state *s, const display_policy *p, uint32_t now) {
  /* Keep the pre-sleep page when a live policy edit ends/replaces sleep. */
  s->policy = *p; s->configured = true; s->activity = now;
  s->wake_occurrence = 0;
}
static uint32_t occurrence(const display_policy *p, uint32_t utc, int16_t tz,
                           unsigned *index) {
  if (utc < 1700000000u || tz < -720 || tz > 840) return 0;
  uint32_t local = (uint32_t)((int64_t)utc + tz * 60);
  uint32_t day = local / 86400; unsigned minute = local / 60 % 1440;
  for (unsigned i = 0; i < p->count; i++) {
    const display_plan *r = &p->plans[i];
    if (!r->enabled) continue;
    uint32_t start_day = day;
    if (r->start < r->end) {
      if (minute < r->start || minute >= r->end) continue;
    } else {
      if (minute < r->end) start_day--;
      else if (minute < r->start) continue;
    }
    if (!(r->days & (1u << ((start_day + 3) % 7)))) continue;
    *index = i; return start_day * DISPLAY_PLANS + i + 1;
  }
  return 0;
}
void display_step(display_state *s, uint32_t now, uint32_t utc, int16_t tz,
                  bool battery, unsigned normal, uint8_t pages, unsigned *page,
                  bool wake, bool activity, bool ota) {
  if (!s->configured || !page) return;
  if (ota) { s->activity = now; return; }
  unsigned index = 0;
  uint32_t key = occurrence(&s->policy, utc, tz, &index);
  unsigned wait = battery ? s->policy.battery_minutes : s->policy.external_minutes;
  if (activity || wake) s->activity = now;
  if (wake) s->wake_occurrence = key;
  uint32_t elapsed = now - s->activity;
  bool planned = key && (key != s->wake_occurrence ||
                         elapsed >= (wait ? wait : 1) * 60000u);
  bool idle = wait && elapsed >= wait * 60000u;
  bool sleep = planned || idle;
  unsigned destination = DISPLAY_KEEP_PAGE;
  if (sleep && !s->sleeping) s->saved_page = *page;
  s->reason = planned ? 2 : idle ? 1 : 0;
  s->target = normal > 100 ? 100 : normal;
  if (planned) {
    s->target = s->policy.plans[index].brightness;
    destination = s->policy.plans[index].page;
  } else if (idle) {
    s->target = s->policy.idle_brightness;
    destination = s->policy.idle_page;
  }
  if (sleep && destination < SCENE_PAGES && (pages & (1u << destination)))
    *page = destination;
  else if (!sleep && s->sleeping && s->saved_page < SCENE_PAGES &&
           (pages & (1u << s->saved_page))) *page = s->saved_page;
  s->sleeping = sleep; s->occurrence = key;
}
