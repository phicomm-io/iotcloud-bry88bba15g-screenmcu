#pragma once
#include "lvgl.h"
/* Bounded 1bpp scaling of the built-in fonts; no external NOR reads or heap
 * allocations. Cache storage is ordinary .bss in SDRAM on SWM341. */
const lv_font_t *screen_ui_font(unsigned pixels, bool numeric);
typedef struct {uint32_t hits, misses, evictions;} screen_font_stats;
screen_font_stats screen_ui_font_stats(void);
void screen_ui_font_invalidate(void);
