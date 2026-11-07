#pragma once
#include "../protocol/display_diag.h"
typedef struct {
  uint32_t offset_us,duration_us,pixels;
  int16_t x1,y1,x2,y2;
  uint8_t kind,last;
} screen_diag_record;
typedef struct {
  uint32_t generation,started_us,last_us,elapsed_us,page,flush_count,flush_us,
      flush_max_us,monitor_count,monitor_ms,monitor_pixels,stored,dropped;
  bool active,waiting,enabled;
  screen_diag_record trace[SCREEN_DIAG_TRACE];
} screen_display_diag;
void screen_diag_begin(screen_display_diag *,unsigned page,uint32_t now_us);
void screen_diag_flush(screen_display_diag *,int x1,int y1,int x2,int y2,
                        uint32_t start_us,uint32_t end_us,bool last);
void screen_diag_monitor(screen_display_diag *,uint32_t ms,uint32_t pixels,uint32_t now_us);
void screen_diag_poll(screen_display_diag *,uint32_t now_us);
/* Independent of LVGL, IRQ state and UART. Caller adds LCD data to bytes 64..127.
 * bytes: 0 op,1 version,2 status,3 active/wait flags,4 request id,8 generation,
 * 12 page,16 duration us,20 flush count,24 flush us,28 max flush us,
 * 32 monitor count,36 monitor ms,40 monitor pixels,44 stored,48 dropped,
 * 52 offset,56 returned,60 draw rows; 64..127 board sample. */
size_t screen_diag_encode(screen_display_diag *,uint32_t request,unsigned status,
                          unsigned offset,unsigned limit,uint8_t *out,size_t cap);
