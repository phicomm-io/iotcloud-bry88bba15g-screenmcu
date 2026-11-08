#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SCREEN_OTA_BRIGHTNESS 10u
enum {
  POWER_WAKE = 1,
  POWER_PAGE = 2,
  POWER_OFF = 4,
  POWER_PROVISION = 8,
  POWER_RESET = 16,
  POWER_SLEEP = 32
};
typedef struct {
  uint16_t front_count, rear_count;
  uint32_t idle, dnd_idle;
  bool front, rear, wake_only, front_long, rear_long, asleep, ota;
  uint8_t power_timeout, battery_timeout;
  bool dnd, extended;
  unsigned brightness;
} screen_power;
/* Original UI cadence: 33 ms. Active low PD11/PD13, PM9 high = external power.
 * OTA wins over sleep, poweroff and reset. No factory reset gesture. */
unsigned power_tick(screen_power *, bool front_down, bool rear_down,
                    bool pm9_high, bool in_dnd);
bool power_in_dnd(unsigned minute, unsigned start, unsigned end, bool enabled);
unsigned battery_trimmed_mv(const uint16_t samples[40], bool pm9_high);
/* OTA temporarily overrides idle/scheduled darkness without changing settings. */
unsigned power_backlight_target(unsigned target, unsigned ceiling, bool asleep,
                                bool ota);
