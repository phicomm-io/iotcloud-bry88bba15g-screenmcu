/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Draw only into the back frame. Latch it at a hardware frame boundary, then
 * synchronize the dirty rectangle into the released frame before reuse. */
static void copy_flush(const lv_area_t *a, const lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) {
    if (y >= 0 && y < 854 && a->x1 >= 0 && a->x2 < 480)
      memcpy(framebuffer[display_front ^ 1u] + y * 480 + a->x1, p,
             (a->x2 - a->x1 + 1) * sizeof(*p));
    p += a->x2 - a->x1 + 1;
  }
}
static void display_dma_reset(void) {
  SYS->PRSTEN = 0x55;
  SYS->PRSTR0 |= SYS_PRSTR0_DMA2D_Msk;
  __DSB();
  SYS->PRSTR0 &= ~SYS_PRSTR0_DMA2D_Msk;
  SYS->PRSTEN = 0;
  __DSB();
}
static void display_dma_clock(bool enabled) {
  if (enabled) SYS->CLKEN0 |= SYS_CLKEN0_DMA2D_Msk;
  else SYS->CLKEN0 &= ~SYS_CLKEN0_DMA2D_Msk;
  __DSB();
}
static void display_dma_move(const lv_color_t *source, lv_color_t *destination,
                             unsigned width, unsigned height,
                             unsigned source_offset, unsigned offset) {
  display_dma_clock(true);
  DMA2D_LayerSetting input = {.Address = (uint32_t)source,
      .LineOffset = source_offset, .ColorMode = DMA2D_FMT_RGB565};
  DMA2D_LayerSetting output = {.Address = (uint32_t)destination,
      .LineCount = height, .LinePixel = width, .LineOffset = offset};
  __DSB();
  DMA2D_PixelMove(&input, &output);
}
static void display_dma_init(void) {
  display_dma_fault = true;
  display_dma_reset();
  /* Yield between 64-word bursts so continuous LCD reads retain bandwidth. */
  DMA2D_InitStructure configuration = {.Interval = 64};
  DMA2D_Init(&configuration);
  /* Check real RGB565 order and destination stride before allowing DMA to
   * touch scanout. Both scratch strips are unused until LVGL is registered. */
  for (unsigned i = 0; i < 24; i++) {
    drawbuffer[0][i].full = 0x1234u + i * 7919u;
    drawbuffer[1][i].full = 0xa55a;
  }
  display_dma_move(drawbuffer[0], drawbuffer[1] + 1, 8, 2, 0, 3);
  uint32_t started = board_ms;
  while (DMA2D_IsBusy() && (uint32_t)(board_ms - started) < 50) board_watchdog_feed();
  if (DMA2D_IsBusy()) { display_dma_reset(); display_dma_clock(false); return; }
  __DSB();
  display_dma_clock(false);
  for (unsigned i = 0; i < 24; i++) {
    unsigned row = i >= 12;
    bool written = (i >= 1 && i <= 8) || (i >= 12 && i <= 19);
    uint16_t expected = written ? drawbuffer[0][i - 1 - row * 3].full : 0xa55a;
    if (drawbuffer[1][i].full != expected) return;
  }
  display_dma_ready = true;
  display_dma_fault = false;
}
static void display_mirror_copy(void) {
  for (int y = display_dirty.y1; y <= display_dirty.y2; y++)
    memcpy(framebuffer[display_front ^ 1u] + y * 480 + display_dirty.x1,
           framebuffer[display_front] + y * 480 + display_dirty.x1,
           lv_area_get_width(&display_dirty) * sizeof(lv_color_t));
}
static bool display_dma_done(void) {
  if (display_dma_ready && DMA2D_IsBusy()) {
    /* A mirror may cover all 854 rows, unlike a borrowed 40-row strip. */
    unsigned timeout = pending_flush.phase == 2 ? 250 : 50;
    if ((uint32_t)(board_ms - pending_flush.started_ms) < timeout) return false;
    display_dma_reset();
    display_dma_ready = false;
    display_dma_fault = true;
    if (pending_flush.phase == 2) display_mirror_copy();
    else copy_flush(&pending_flush.area, pending_flush.pixels);
  }
  __DSB();
  return true;
}
static bool display_flush_poll(void) {
  if (!pending_flush.driver) return true;
  if (pending_flush.phase == 0) {
    if (!display_dma_done()) return false;
    if (pending_flush.last) {
      board_display_present(framebuffer[display_front ^ 1u]);
      pending_flush.phase = 1;
      pending_flush.started_ms = board_ms;
    }
  }
  if (pending_flush.phase == 1) {
    if (board_display_present_pending()) {
      if ((uint32_t)(board_ms - pending_flush.started_ms) >= 200)
        display_scan_failed();
      return false;
    }
    /* VBPRELOAD has cleared: the old front is no longer owned by LCD. */
    __DSB();
    display_front ^= 1u;
    pending_flush.phase = 2;
    pending_flush.started_ms = board_ms;
    if (display_dma_ready) {
      unsigned width = lv_area_get_width(&display_dirty);
      unsigned at = display_dirty.y1 * 480 + display_dirty.x1;
      display_dma_move(framebuffer[display_front] + at,
                       framebuffer[display_front ^ 1u] + at,
                       width, lv_area_get_height(&display_dirty),
                       480 - width, 480 - width);
    } else display_mirror_copy();
  }
  if (pending_flush.phase == 2) {
    if (!display_dma_done()) return false;
    display_dirty_valid = false;
  }
  lv_area_t *a = &pending_flush.area;
  screen_diag_flush(&display_diagnostics, a->x1, a->y1, a->x2, a->y2,
                    pending_flush.started, board_time_us(), pending_flush.last);
  lv_disp_drv_t *driver = pending_flush.driver;
  pending_flush.driver = NULL;
  display_dma_clock(false);
  lv_disp_flush_ready(driver);
  return true;
}
static void display_flush_wait(lv_disp_drv_t *driver) {
  (void)driver;
  display_flush_poll();
  board_watchdog_feed();
}
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  uint32_t start = board_time_us();
  if (!display_dirty_valid) { display_dirty = *a; display_dirty_valid = true; }
  else {
    if (a->x1 < display_dirty.x1) display_dirty.x1 = a->x1;
    if (a->x2 > display_dirty.x2) display_dirty.x2 = a->x2;
    if (a->y1 < display_dirty.y1) display_dirty.y1 = a->y1;
    if (a->y2 > display_dirty.y2) display_dirty.y2 = a->y2;
  }
  pending_flush.driver = d; pending_flush.area = *a;
  pending_flush.pixels = p; pending_flush.started = start;
  pending_flush.started_ms = board_ms; pending_flush.phase = 0;
  pending_flush.last = lv_disp_flush_is_last(d);
  if (display_dma_ready && a->x1 >= 0 && a->x2 < 480 &&
      a->y1 >= 0 && a->y2 < 854) {
    display_dma_flushes++;
    display_dma_move(p, framebuffer[display_front ^ 1u] + a->y1 * 480 + a->x1,
                     lv_area_get_width(a), lv_area_get_height(a), 0,
                     480 - lv_area_get_width(a));
  } else {
    copy_flush(a, p);
  }
}
