#pragma once
#include "ota.h"
#include "crypto/md5.h"
/* Only NOR staging. There is intentionally no erase/write of internal flash,
 * protection bits, settings or boot flags in this module. */
typedef struct {
 screen_ota transfer;screen_md5 md5;uint8_t expected[16],vectors[8];
 void *context;
 bool (*erase)(void *,uint32_t,size_t);
 bool (*write)(void *,uint32_t,const void *,size_t);
 bool (*read)(void *,uint32_t,void *,size_t);
 bool failed;
} screen_update;
bool screen_update_begin(screen_update *,uint32_t,const uint8_t md5[16]);
bool screen_update_chunk(screen_update *,uint32_t,const void *,size_t);
/* Re-read NOR and hash it before declaring staged_verified; boot commit remains
 * separate until the original bootloader handoff is recovered. */
bool screen_update_finish(screen_update *);
bool screen_update_recheck(screen_update *);
void screen_update_abort(screen_update *);
