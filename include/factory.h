#pragma once
#include "maintenance.h"
#include "power.h"
#include "runtime.h"
/* Factory JSON exists only on the original UART boundary. No cloud transport.
 */
typedef struct {
  screen_runtime *runtime;
  screen_maintenance *maintenance;
  screen_power *power;
  char mac[13], gateway[13], esp_version[64];
  uint8_t settings[64];
  bool configured, connected, have_mac, dnd;
  unsigned phase;
  uint32_t next, snapshot_at, token, epoch, clock_at;
  int16_t timezone;
  const char *version;
  /* Fresh platform PRNG nonce, or zero to stay in factory fallback. */
  uint32_t (*nonce)(void *);
} factory_owner;
void factory_init(factory_owner *, const uint8_t settings[64]);
void factory_line(void *, const char *, size_t);
void factory_tick(factory_owner *, uint32_t);
void factory_snapshot(factory_owner *, const char *token, bool response);
void factory_provision(factory_owner *);
void factory_ota(factory_owner *);
bool factory_calibration(void *, unsigned, unsigned, const uint8_t *, size_t);
