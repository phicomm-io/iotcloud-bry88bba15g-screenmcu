#pragma once
#include "display_policy.h"
typedef struct {
  display_policy policy;
  bool configured, sleeping;
  uint8_t reason, target, saved_page;
  uint32_t activity, occurrence, wake_occurrence;
} display_state;
void display_configure(display_state *, const display_policy *, uint32_t);
/* A tick-wrap-safe monotonic clock drives idle time. UTC is used only for
 * weekly schedules. Page bits describe the current, committed scene. */
void display_step(display_state *, uint32_t now, uint32_t utc, int16_t timezone,
                  bool battery, unsigned normal, uint8_t enabled_pages,
                  unsigned *page, bool wake, bool activity, bool ota);
