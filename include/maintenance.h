#pragma once
#include "update.h"
/* Internal-SRAM UART owner. +bin raw bytes bypass all text/extension parsers.
 */
typedef struct {
  screen_update update;
  uint8_t block[1024];
  size_t block_size, block_at, prefix_at;
  uint32_t offset, deadline;
  unsigned state;
  bool receiving, staged;
  void *context;
  uint32_t (*clock)(void *);
  bool (*send)(void *, const char *, size_t);
  void (*line)(void *, const char *, size_t);
  /* Runs before the first metadata/erase and on every exit, including failure. */
  void (*activity)(void *, bool);
  char text[4608];
  size_t used;
  bool discard;
  const char *version;
} screen_maintenance;
void maintenance_init(screen_maintenance *);
bool maintenance_query(screen_maintenance *, uint32_t);
void maintenance_feed(screen_maintenance *, uint8_t, uint32_t);
void maintenance_tick(screen_maintenance *, uint32_t);
