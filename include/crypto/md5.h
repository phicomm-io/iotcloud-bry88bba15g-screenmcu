#pragma once
#include <stdint.h>
#include <stddef.h>
/* Factory image checksum compatibility only; not image authentication. */
typedef struct {uint32_t state[4];uint64_t bytes;uint8_t block[64];size_t used;} screen_md5;
void screen_md5_init(screen_md5 *);
void screen_md5_update(screen_md5 *,const void *,size_t);
void screen_md5_final(screen_md5 *,uint8_t[16]);
