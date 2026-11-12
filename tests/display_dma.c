/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lvgl.h"
#include "display_diag.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static lv_color_t framebuffer[480 * 854], drawbuffer[2][480 * 40];
static screen_display_diag display_diagnostics;
static bool display_dma_ready, display_dma_fault;
static uint32_t display_dma_flushes;
static uint32_t board_ms, micros;
static struct { lv_disp_drv_t *driver; lv_area_t area; lv_color_t *pixels;
  uint32_t started, started_ms; bool last; } pending_flush;
static struct { unsigned PRSTEN, PRSTR0; } system_registers;
#define SYS (&system_registers)
#define SYS_PRSTR0_DMA2D_Msk 1u
typedef struct { uint32_t Address, LineOffset; unsigned ColorMode, LineCount, LinePixel; } DMA2D_LayerSetting;
typedef struct { unsigned Interval; } DMA2D_InitStructure;
#define DMA2D_FMT_RGB565 2
static unsigned remaining, latency = 2, completions;
static bool last = true;
static DMA2D_LayerSetting input, output;
static void barrier(void) { if (SYS->PRSTR0) remaining = 0; }
#define __DSB() barrier()
static void board_watchdog_feed(void) { board_ms++; }
static uint32_t board_time_us(void) { return micros += 100; }
static void DMA2D_Init(DMA2D_InitStructure *config) { assert(config->Interval); }
static void DMA2D_PixelMove(DMA2D_LayerSetting *a, DMA2D_LayerSetting *b) {
  assert(!remaining); input = *a; output = *b; remaining = latency;
}
static lv_color_t *address(uint32_t p) {
  lv_color_t *arenas[] = {framebuffer, drawbuffer[0], drawbuffer[1]};
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
#include "../platform/swm34s/display_dma.inc"

int main(void) {
  display_dma_init(); assert(display_dma_ready && !display_dma_fault);
  lv_disp_drv_t driver = {0};
  lv_area_t area = {.x1 = 17, .y1 = 20, .x2 = 40, .y2 = 29};
  for (unsigned i = 0; i < 240; i++) drawbuffer[0][i].full = i + 1;
  flush(&driver, &area, drawbuffer[0]);
  assert(!completions && pending_flush.driver == &driver);
  /* The second strip is independent while the first remains borrowed. */
  memset(drawbuffer[1], 0xff, sizeof(drawbuffer[1]));
  while (!display_flush_poll()) {}
  assert(completions == 1 && !pending_flush.driver);
  for (unsigned y = 0; y < 854; y++) for (unsigned x = 0; x < 480; x++) {
    unsigned expected = y >= 20 && y <= 29 && x >= 17 && x <= 40 ? (y - 20) * 24 + x - 16 : 0;
    assert(framebuffer[y * 480 + x].full == expected);
  }
  assert(display_flush_poll() && completions == 1);
  /* A stalled peripheral is reset, its exact rectangle copied by the CPU,
   * and the borrowed strip released exactly once. Future flushes use CPU. */
  latency = 100000; area.y1 = 30; area.y2 = 39;
  flush(&driver, &area, drawbuffer[0]);
  while (!display_flush_poll()) display_flush_wait(&driver);
  assert(completions == 2 && !display_dma_ready && display_dma_fault && !remaining);
  for (unsigned y = 30; y <= 39; y++) for (unsigned x = 17; x <= 40; x++)
    assert(framebuffer[y * 480 + x].full == (y - 30) * 24 + x - 16);
  area.y1 = 40; area.y2 = 49; flush(&driver, &area, drawbuffer[0]);
  assert(completions == 3 && !pending_flush.driver);
  assert(display_dma_flushes == 2);
  puts("PASS: asynchronous buffer ownership, stride, completion and bounded DMA failure fallback");
}
