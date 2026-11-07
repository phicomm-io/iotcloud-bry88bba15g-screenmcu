#include "power.h"
unsigned power_backlight_target(unsigned target, unsigned ceiling, bool asleep,
                                bool ota) {
  unsigned value = ota ? SCREEN_OTA_BRIGHTNESS : asleep ? 0 : target;
  if (ceiling > 100) ceiling = 100;
  return value < ceiling ? value : ceiling;
}
bool power_in_dnd(unsigned minute, unsigned start, unsigned end, bool enabled) {
  if (!enabled || minute >= 1440 || start >= 1440 || end >= 1440 ||
      start == end)
    return false;
  return start < end ? (minute >= start && minute < end)
                     : (minute >= start || minute < end);
}
unsigned power_tick(screen_power *p, bool f, bool r, bool pm9, bool dnd) {
  unsigned event = 0;
  if (p->ota) {
    p->idle = p->dnd_idle = 0;
    p->front_count = p->rear_count = 0;
    p->front = f;
    p->rear = r;
    p->wake_only = f;
    p->front_long = f;
    p->rear_long = r;
    return 0;
  }
  if (f) {
    if (!p->front) {
      p->idle = p->dnd_idle = 0;
      p->front_count = 0;
      p->front_long = false;
      p->wake_only = p->asleep;
      if (p->asleep) {
        p->asleep = false;
        event |= POWER_WAKE;
      }
    }
    if (p->front_count < UINT16_MAX)
      p->front_count++;
    if (p->front_count > 91 && !p->front_long && !p->wake_only) {
      p->front_long = true;
      event |= POWER_OFF;
    }
  } else if (p->front) {
    if (!p->front_long && !p->wake_only)
      event |= POWER_PAGE;
    p->front_count = 0;
    p->wake_only = false;
  }
  if (r) {
    if (!p->rear) {
      p->rear_count = 0;
      p->rear_long = false;
    }
    if (p->rear_count < UINT16_MAX)
      p->rear_count++;
    if (p->rear_count >= 50 && !p->rear_long) {
      p->rear_long = true;
      event |= POWER_RESET;
    }
  } else if (p->rear) {
    if (!p->rear_long)
      event |= POWER_PROVISION;
    p->rear_count = 0;
  }
  p->front = f;
  p->rear = r;
  if (!p->extended && !f && !r && !p->asleep) {
    if (p->idle < UINT32_MAX)
      p->idle++;
    p->dnd_idle = dnd ? p->dnd_idle + 1 : 0;
    unsigned timeout = pm9 ? p->battery_timeout : p->power_timeout;
    if ((timeout && p->idle > timeout * 0x71au) ||
        (dnd && p->dnd_idle > 0x38cu)) {
      p->asleep = true;
      event |= POWER_SLEEP;
    }
  }
  return event;
}
unsigned battery_trimmed_mv(const uint16_t v[40], bool pm9) {
  uint16_t a[40];
  for (unsigned i = 0; i < 40; i++) {
    unsigned j = i;
    while (j && a[j - 1] > v[i]) {
      a[j] = a[j - 1];
      j--;
    }
    a[j] = v[i];
  }
  unsigned sum = 0;
  for (unsigned i = 5; i < 35; i++)
    sum += a[i];
  unsigned mv = (unsigned)((sum / 30.0) * 1.0410256410256409);
  if (!pm9)
    mv += 160;
  return mv;
}
