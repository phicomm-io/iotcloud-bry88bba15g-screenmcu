#pragma once
#include "scene.h"
#include "appearance.h"
#include "services.h"
#include "widgets.h"
#include <stdbool.h>
#include <stdint.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 854
#define SCREEN_METRICS 6
#define SCREEN_HISTORY 1440
#define SCREEN_PAGES 7
enum { MET_TEMP, MET_HUM, MET_CO2, MET_PM25, MET_TVOC, MET_HCHO };
typedef struct {
  float value;
  uint32_t at;
  bool valid;
} screen_sample;
typedef struct {
  uint32_t minute;
  float values[SCREEN_METRICS];
  uint8_t valid;
} screen_point;
typedef struct {
  screen_widgets external;
  screen_scene scene;
  screen_appearance appearance;
  scene_data data[SCENE_ITEMS];
  screen_weather weather;
  reminder_notice reminders[REMINDER_MAX];
  bool dnd, ota;
  uint8_t present;
  screen_sample samples[SCREEN_METRICS];
  /* Factory category, 1=优, 2=良, 3=差. Raw TVOC remains available for calibration. */
  uint8_t tvoc_level;
  screen_point history[SCREEN_HISTORY];
  unsigned head, count, page;
  bool asleep, clock_valid, network;
  unsigned hour, minute, battery_percent;
  uint32_t local_day;
  bool battery_valid, charging;
  uint32_t last_minute, record_at;
  bool have_minute;
} screen_model;
void screen_init(screen_model *);
void screen_event(screen_model *, unsigned clicks);
void screen_sample_set(screen_model *, unsigned, float, bool, uint32_t);
bool screen_sample_valid(const screen_model *, unsigned, uint32_t);
void screen_record(screen_model *, uint32_t);
unsigned screen_tvoc_level(unsigned previous, unsigned raw);
const char *screen_tvoc_text(unsigned level);
/* True when visible content changed. Stable value/clock labels are updated
 * in place; page/style/layout changes rebuild only the active page. */
bool screen_ui(const screen_model *, uint32_t, bool demo);
bool sensor_uart_value(const uint8_t *, unsigned, uint8_t, uint16_t *);
bool sensor_sht3x(const uint8_t[6], float *, float *);
unsigned battery_permille(unsigned millivolts);
