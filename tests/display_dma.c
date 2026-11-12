/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lvgl.h"
#include "display_diag.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

static lv_color_t framebuffer[2][480 * 854], drawbuffer[2][480 * 40];
static lv_color_t reference[480 * 854];
static bool check_reference;
static unsigned preserved_pixels;
static screen_display_diag display_diagnostics;
static bool display_dma_ready, display_dma_fault;
static uint32_t display_dma_flushes;
static unsigned display_front, scanned_front, requested_front, reload_delay;
static lv_area_t display_dirty;
static bool display_dirty_valid, reload_stuck;
static jmp_buf scan_failure;
static uint32_t board_ms, micros;
static struct { lv_disp_drv_t *driver; lv_area_t area; lv_color_t *pixels;
  uint32_t started, started_ms; bool last; unsigned phase; } pending_flush;
static struct { unsigned PRSTEN, PRSTR0, CLKEN0; } system_registers;
#define SYS (&system_registers)
#define SYS_PRSTR0_DMA2D_Msk 1u
#define SYS_CLKEN0_DMA2D_Msk 2u
typedef struct { uint32_t Address, LineOffset; unsigned ColorMode, LineCount, LinePixel; } DMA2D_LayerSetting;
typedef struct { unsigned Interval, IntEOTEn; } DMA2D_InitStructure;
#define DMA2D_FMT_RGB565 2
static unsigned remaining, latency = 2, completions;
static bool last = true;
static DMA2D_LayerSetting input, output;
static void barrier(void) { if (SYS->PRSTR0) remaining = 0; }
#define __DSB() barrier()
static void board_watchdog_feed(void) { board_ms++; }
static void board_idle(void) { board_ms++; }
static uint32_t board_time_us(void) { return micros += 100; }
static void board_display_present(const void *pixels) {
  assert(!remaining && pixels == framebuffer[display_front ^ 1u]);
  if (check_reference) assert(!memcmp(pixels, reference, sizeof(reference)));
  requested_front = display_front ^ 1u; reload_delay = 3;
}
static bool board_display_present_pending(void) {
  if (reload_stuck) return true;
  if (reload_delay && !--reload_delay) scanned_front = requested_front;
  return reload_delay != 0;
}
static void display_scan_failed(void) { longjmp(scan_failure, 1); }
static void DMA2D_Init(DMA2D_InitStructure *config) { assert(config->Interval); }
static void DMA2D_PixelMove(DMA2D_LayerSetting *a, DMA2D_LayerSetting *b) {
  assert(SYS->CLKEN0 & SYS_CLKEN0_DMA2D_Msk);
  assert(!remaining); input = *a; output = *b; remaining = latency;
  /* No DMA transfer may target the framebuffer owned by LCD scanout. */
  uint32_t delta = b->Address - (uint32_t)(uintptr_t)framebuffer[scanned_front];
  assert(delta >= sizeof(framebuffer[0]));
}
static lv_color_t *address(uint32_t p) {
  lv_color_t *arenas[] = {framebuffer[0], drawbuffer[0], drawbuffer[1]};
  unsigned sizes[] = {sizeof(framebuffer), sizeof(drawbuffer[0]), sizeof(drawbuffer[1])};
  for (unsigned i = 0; i < 3; i++) {
    uint32_t delta = p - (uint32_t)(uintptr_t)arenas[i];
    if (delta < sizes[i]) return arenas[i] + delta / 2;
  }
  assert(false); return NULL;
}
static unsigned DMA2D_IsBusy(void) {
  if (remaining && !--remaining) {
    lv_color_t *a = address(input.Address), *b = address(output.Address);
    for (unsigned y = 0; y < output.LineCount; y++) {
      memcpy(b, a, output.LinePixel * 2u);
      a += output.LinePixel + input.LineOffset;
      b += output.LinePixel + output.LineOffset;
    }
  }
  return remaining != 0;
}
void lv_disp_flush_ready(lv_disp_drv_t *driver) { assert(driver && !remaining); completions++; }
bool lv_disp_flush_is_last(lv_disp_drv_t *driver) { assert(driver); return last; }
static void *guarded_copy(void *destination, const void *source, size_t size) {
  uintptr_t begin=(uintptr_t)framebuffer[scanned_front], end=begin+sizeof(framebuffer[0]);
  assert((uintptr_t)destination+size<=begin || (uintptr_t)destination>=end);
  uintptr_t frames_begin=(uintptr_t)framebuffer, frames_end=frames_begin+sizeof(framebuffer);
  if ((uintptr_t)source>=frames_begin && (uintptr_t)source<frames_end)
    preserved_pixels += size / sizeof(lv_color_t);
  return lv_memcpy(destination,source,size);
}
#define lv_memcpy guarded_copy
#include "../platform/swm34s/display_dma.inc"
#undef lv_memcpy

static void refresh_strip(lv_disp_drv_t *driver, lv_area_t area, unsigned seed, bool final) {
  unsigned width=lv_area_get_width(&area), height=lv_area_get_height(&area);
  assert(width*height<=480*40);
  for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
    lv_color_t color={.full=(seed+x*13+y*7937)&0xffff};
    drawbuffer[0][y*width+x]=color;
    reference[(area.y1+y)*480+area.x1+x]=color;
  }
  last=final;
  flush(driver,&area,drawbuffer[0]);
  while(!display_flush_poll()) display_flush_wait(driver);
  if(final) assert(display_front==scanned_front);
}

int main(void) {
  display_dma_init(); assert(display_dma_ready == (bool)SCREEN_DMA2D && !display_dma_fault);
  assert(!(SYS->CLKEN0 & SYS_CLKEN0_DMA2D_Msk));
  lv_disp_drv_t driver = {0};
  lv_area_t area = {.x1 = 17, .y1 = 20, .x2 = 40, .y2 = 29};
  for (unsigned i = 0; i < 240; i++) drawbuffer[0][i].full = i + 1;
  flush(&driver, &area, drawbuffer[0]);
  assert(!completions && pending_flush.driver == &driver);
  /* The second strip is independent while the first remains borrowed. */
  memset(drawbuffer[1], 0xff, sizeof(drawbuffer[1]));
  while (!display_flush_poll()) {}
  assert(completions == 1 && !pending_flush.driver);
  assert(!(SYS->CLKEN0 & SYS_CLKEN0_DMA2D_Msk));
  for (unsigned y = 0; y < 854; y++) for (unsigned x = 0; x < 480; x++) {
    unsigned expected = y >= 20 && y <= 29 && x >= 17 && x <= 40 ? (y - 20) * 24 + x - 16 : 0;
    assert(framebuffer[display_front][y * 480 + x].full == expected);
  }
  assert(display_flush_poll() && completions == 1);
  assert(display_front == scanned_front && display_front == 1);
  /* A refresh can contain disjoint rectangles. Neither the earlier strip
   * nor the unused pixels between them may leak into the scanned frame. */
  last = false; area = (lv_area_t){.x1=10,.y1=60,.x2=19,.y2=64};
  flush(&driver, &area, drawbuffer[0]);
  while (!display_flush_poll()) {}
  assert(completions == 2 && display_front == 1);
  assert(framebuffer[1][60 * 480 + 10].full == 0);
  assert(framebuffer[0][60 * 480 + 10].full == 1);
  last = true; area = (lv_area_t){.x1=70,.y1=80,.x2=79,.y2=84};
  flush(&driver, &area, drawbuffer[0]);
  while (!display_flush_poll()) {}
  assert(completions == 3 && display_front == 0 && scanned_front == 0);
  assert(framebuffer[0][20 * 480 + 17].full == 1);
  assert(framebuffer[0][70 * 480 + 50].full == 0);
  memcpy(reference,framebuffer[display_front],sizeof(reference));check_reference=true;
  /* Every new scan frame must equal an independent complete image, including
   * untouched rows, overlapping rectangles and disjoint writes on one row. */
  unsigned random=0x341;
  for(unsigned round=0;round<150;round++) {
    for(unsigned part=0;part<3;part++) {
      random=random*1664525u+1013904223u;unsigned width=1+random%480;
      random=random*1664525u+1013904223u;unsigned height=1+random%40;
      random=random*1664525u+1013904223u;unsigned x=random%(481-width);
      random=random*1664525u+1013904223u;unsigned y=random%(855-height);
      refresh_strip(&driver,(lv_area_t){x,y,x+width-1,y+height-1},random,part==2);
    }
  }
  /* Complete redraws replace every stale pixel. Neither of two full pages
   * may copy any old framebuffer pixels, even across the frame latch. */
  unsigned before=preserved_pixels;
  for(unsigned round=0;round<2;round++) for(unsigned y=0;y<854;y+=40) {
    unsigned end=y+39<854?y+39:853;
    refresh_strip(&driver,(lv_area_t){0,y,479,end},round+y,end==853);
  }
  assert(preserved_pixels==before);
  /* Repeated camera bounds likewise need no frame-to-frame preservation. */
  area=(lv_area_t){17,200,460,239};refresh_strip(&driver,area,0x1234,true);
  before=preserved_pixels;refresh_strip(&driver,area,0x7890,true);
  assert(preserved_pixels==before);
  /* Two separated rectangles on the same stale row preserve the gap. */
  refresh_strip(&driver,(lv_area_t){20,200,30,220},0x4567,false);
  refresh_strip(&driver,(lv_area_t){440,200,450,220},0x89ab,true);
#if SCREEN_DMA2D
  /* A stalled peripheral is reset, its exact rectangle copied by the CPU,
   * and the borrowed strip released exactly once. Future flushes use CPU. */
  unsigned completed=completions, dma_count=display_dma_flushes;
  latency = 100000; area = (lv_area_t){.x1=17,.y1=30,.x2=40,.y2=39};
  refresh_strip(&driver,area,1,true);
  assert(completions == completed+1 && !display_dma_ready && display_dma_fault && !remaining);
  for (unsigned y = 30; y <= 39; y++) for (unsigned x = 17; x <= 40; x++)
    assert(framebuffer[display_front][y * 480 + x].full == reference[y * 480 + x].full);
  area.y1 = 40; area.y2 = 49; refresh_strip(&driver,area,2,true);
  assert(completions == completed+2 && !pending_flush.driver);
  assert(display_dma_flushes == dma_count+1);
#else
  assert(!display_dma_ready && !display_dma_fault && !display_dma_flushes && !remaining);
#endif
  /* A stuck frame reload must enter recovery without releasing the borrowed
   * strip or writing the buffer still owned by LCD. */
  unsigned old = display_front, completed_before_scan = completions; reload_stuck = true;
  check_reference=false;
  area.y1 = 100; area.y2 = 109; flush(&driver, &area, drawbuffer[0]);
  if (!setjmp(scan_failure)) {
    for (;;) display_flush_wait(&driver);
  }
  assert(display_front == old && scanned_front == old && completions == completed_before_scan);
  assert(pending_flush.driver && framebuffer[old][100 * 480 + 17].full == reference[100 * 480 + 17].full);
  puts("PASS: scan ownership, deferred preservation, repeated-area copy elimination, DMA fallback and stalled scan recovery");
}
