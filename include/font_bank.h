#pragma once
#include "resources.h"
/* Same bounded, acknowledged operations as wallpaper, distinct capability
 * and opcodes. The entire IOTFNT02 file is transferred in contiguous chunks. */
typedef struct {
  void *context;
  bool (*read)(void *,uint32_t,void *,size_t);
  bool (*write)(void *,uint32_t,const void *,size_t);
  bool (*erase)(void *,uint32_t,size_t);
  uint32_t (*now)(void *);
  void (*activity)(void *,bool);
  screen_resources *font;
  uint32_t bank,generation,target,received,total,crc,deadline,prepare_at,started,erased;
  uint8_t header[32],state;
  bool active,preparing;
} screen_font_bank;
extern screen_font_bank font_bank;
void font_bank_init(screen_font_bank *);
void font_bank_poll(screen_font_bank *);
bool font_bank_receive(screen_font_bank *,const uint8_t *,size_t,uint8_t[20]);
