/* Factory NOR handoff. Internal flash and protection registers are untouched. */
#pragma once
#include "update.h"
#define SCREEN_BOOT_FLAG 0x7f0000u
#define SCREEN_BOOT_FLAG_BLOCK 0x10000u
/* Recovered factory boot SHA-256 ccb9d414...bebdf09: its copy loop reads
 * 0x7e000 bytes, although the application is only 0x67000 bytes. No data
 * outside the application may be programmed by this handoff. */
#define SCREEN_BOOT_COPY_BYTES 0x7e000u
typedef struct {
  void *context;
  bool (*read)(void *, uint32_t, void *, size_t);
  bool (*erase)(void *, uint32_t, size_t);
  bool (*write)(void *, uint32_t, const void *, size_t);
} screen_boot_control;
bool screen_boot_ack(const screen_boot_control *);
bool screen_boot_tail_blank(const screen_boot_control *);
bool screen_boot_commit(const screen_boot_control *, screen_update *);
