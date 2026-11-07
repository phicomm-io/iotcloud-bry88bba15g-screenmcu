#pragma once
#include "guard.h"
typedef struct {
  uint32_t session, sequence, deadline, exported;
  bool held, forced, normal, released;
  void *context;
  bool (*send)(void *, const char *, size_t);
  bool (*read_boot)(void *, uint32_t, void *, size_t);
  void (*reset)(void *);
  bool (*install)(void *);
  void (*restart)(void *);
  bool (*nor_mode)(void *, unsigned);
} screen_guard;
void guard_init(screen_guard *, uint32_t session, uint32_t now, bool forced);
bool guard_waiting(const screen_guard *, uint32_t now, bool ota);
/* Same checked framing as screen_ext, with its own boot nonce and sequence.
 * Runs before the capability/UI/SDRAM owners exist. */
bool guard_receive(screen_guard *, const char *, size_t, bool ota);
