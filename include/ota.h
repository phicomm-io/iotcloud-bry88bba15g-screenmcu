#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SCREEN_APP_BASE 0x14000u
#define SCREEN_APP_LIMIT 0x67000u
#define SCREEN_OTA_STAGE 0x780000u
#define SCREEN_OTA_FLAGS 0x7f0000u
typedef struct { uint32_t size,received; bool active,verified; } screen_ota;
bool screen_ota_begin(screen_ota *,uint32_t);
bool screen_ota_accept(screen_ota *,uint32_t,const void *,size_t);
bool screen_ota_verify(screen_ota *,bool digest_matches);
/* Deliberately no commit operation until factory boot handoff is recovered. */
