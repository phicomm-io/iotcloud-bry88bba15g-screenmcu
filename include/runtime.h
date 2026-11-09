#pragma once
#include "calibration.h"
#include "display.h"
#include "backlight.h"
#include "module_control.h"
#include "../protocol/display_diag.h"
#include "../protocol/page_control.h"
#include "screen.h"
#include "screen_caps.h"
#include "sensors.h"
/* Single UART-owner task. Feed no OTA binary bytes into the line parser.
 * Allocate in SDRAM: the 24-hour history exceeds the internal SRAM budget. */
typedef struct {
  screen_model model;
  display_state display;
  bool reminder_wake;
  screen_caps caps;
  screen_link parser;
  uint32_t last_sequence, now;
  bool offered;
  bool diag_pending;
  uint8_t diag_request[SCREEN_DIAG_REQUEST_BYTES];
  uint32_t clock_epoch, clock_at;
  uint32_t clock_revision, network_clock_at;
  bool network_clock;
  int16_t timezone;
  uint32_t tx_sequence;
  scene_transaction scene_transfer;
  sensor_sht_config sensor_config;
  sensor_sht_state sht;
  bool sensor_config_valid;
  sensor_calibration calibration;
  uint32_t calibration_valid_since[2];
  /* Result survives the stock executor returning to its idle stage. */
  uint8_t calibration_result[2];
  screen_module module;
  calibration_emit calibration_event;
  void *context;
  bool (*send)(void *, const char *, size_t);
  void (*factory)(void *, const char *, size_t);
} screen_runtime;
void screen_runtime_init(screen_runtime *);
/* Called once after normal version exchange; nonce must be freshly generated
 * for every boot/link reset. This CRC-framed UART is not an auth boundary. */
bool screen_runtime_offer(screen_runtime *, uint32_t nonce);
void screen_runtime_feed(screen_runtime *, const void *, size_t, uint32_t now);
void screen_runtime_tick(screen_runtime *, uint32_t now);
/* Read-only status; suppressed with a factory/unnegotiated ESP peer. */
bool screen_runtime_backlight(screen_runtime *, unsigned requested,
                              unsigned ceiling, unsigned applied, bool asleep);
bool screen_runtime_module_report(screen_runtime *);
bool screen_runtime_page_report(screen_runtime *,uint32_t request,unsigned status);
bool screen_runtime_display_report(screen_runtime *, unsigned normal,
                                   unsigned ceiling, unsigned applied);
/* UTC base, display offset in minutes. RTC reads do not count as a new
 * network synchronization and must never schedule a write back to themselves. */
bool screen_runtime_clock(screen_runtime *, uint32_t utc, int16_t offset,
                          uint32_t now, bool network);

/* BSP/owner integration: load validated board config explicitly before
 * sampling. */
void screen_runtime_sensor_config(screen_runtime *, const sensor_sht_config *);
bool screen_runtime_sht(screen_runtime *, const uint8_t frame[6],
                        uint8_t hcho_status, bool ramp, bool force,
                        uint32_t now);
/* Local request entry; shares the existing stock service execution engine.
 * calibration_event must route CAL_COMMAND to the corresponding sensor UART.
 * Stock JSON decoding remains with the factory UART owner. */
bool screen_runtime_calibrate(screen_runtime *, unsigned channels);
void screen_runtime_calibration_poll(screen_runtime *, uint32_t age,
                                     uint16_t co2, uint16_t tvoc);

/* Called by factory owner after validating JSON/client_token; response still
 * belongs to that owner. Does not advertise a new extension or imply done. */
bool screen_runtime_calibration_service(screen_runtime *, bool has_co2, int co2,
                                        bool has_tvoc, int tvoc);

/* Community UI uses the existing factory JSON service; readiness is measured
 * from continuous fresh sensor operation, including a new module warm-up. */
enum { SCREEN_CAL_IDLE, SCREEN_CAL_FAILED=2, SCREEN_CAL_SENT,
       SCREEN_CAL_ACKNOWLEDGED, SCREEN_CAL_CANCELLED, SCREEN_CAL_SENSOR_LOST };
uint32_t screen_runtime_calibration_warmup(const screen_runtime *, unsigned index);
bool screen_runtime_calibration_cancel(screen_runtime *);
void screen_runtime_calibration_ack(screen_runtime *);
