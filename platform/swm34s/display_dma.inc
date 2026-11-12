/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Two LVGL strips alternate while DMA2D copies to RGB565 scanout. */
static void copy_flush(const lv_area_t *a, const lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) {
    if (y >= 0 && y < 854 && a->x1 >= 0 && a->x2 < 480)
      memcpy(framebuffer + y * 480 + a->x1, p,
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
static void display_dma_move(const lv_color_t *source, lv_color_t *destination,
                             unsigned width, unsigned height, unsigned offset) {
  DMA2D_LayerSetting input = {.Address = (uint32_t)source, .ColorMode = DMA2D_FMT_RGB565};
  DMA2D_LayerSetting output = {.Address = (uint32_t)destination,
      .LineCount = height, .LinePixel = width, .LineOffset = offset};
  __DSB();
  DMA2D_PixelMove(&input, &output);
}
static void display_dma_init(void) {
  display_dma_fault = true;
  display_dma_reset();
  DMA2D_InitStructure configuration = {.Interval = 8};
  DMA2D_Init(&configuration);
  /* Check real RGB565 order and destination stride before allowing DMA to
   * touch scanout. Both scratch strips are unused until LVGL is registered. */
  for (unsigned i = 0; i < 24; i++) {
    drawbuffer[0][i].full = 0x1234u + i * 7919u;
    drawbuffer[1][i].full = 0xa55a;
  }
  display_dma_move(drawbuffer[0], drawbuffer[1] + 1, 8, 2, 3);
  uint32_t started = board_ms;
  while (DMA2D_IsBusy() && (uint32_t)(board_ms - started) < 50) board_watchdog_feed();
  if (DMA2D_IsBusy()) { display_dma_reset(); return; }
  __DSB();
  for (unsigned i = 0; i < 24; i++) {
    unsigned row = i >= 12;
    bool written = (i >= 1 && i <= 8) || (i >= 12 && i <= 19);
    uint16_t expected = written ? drawbuffer[0][i - 1 - row * 3].full : 0xa55a;
    if (drawbuffer[1][i].full != expected) return;
  }
  display_dma_ready = true;
  display_dma_fault = false;
}
static bool display_flush_poll(void) {
  if (!pending_flush.driver) return true;
  if (DMA2D_IsBusy()) {
    if ((uint32_t)(board_ms - pending_flush.started_ms) < 50) return false;
    display_dma_reset();
    display_dma_ready = false;
    display_dma_fault = true;
    copy_flush(&pending_flush.area, pending_flush.pixels);
  }
  __DSB();
  lv_area_t *a = &pending_flush.area;
  screen_diag_flush(&display_diagnostics, a->x1, a->y1, a->x2, a->y2,
                    pending_flush.started, board_time_us(), pending_flush.last);
  lv_disp_drv_t *driver = pending_flush.driver;
  pending_flush.driver = NULL;
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
  if (display_dma_ready && a->x1 >= 0 && a->x2 < 480 &&
      a->y1 >= 0 && a->y2 < 854) {
    display_dma_flushes++;
    pending_flush.driver = d; pending_flush.area = *a;
    pending_flush.pixels = p; pending_flush.started = start;
    pending_flush.started_ms = board_ms;
    pending_flush.last = lv_disp_flush_is_last(d);
    display_dma_move(p, framebuffer + a->y1 * 480 + a->x1,
                     lv_area_get_width(a), lv_area_get_height(a),
                     480 - lv_area_get_width(a));
  } else {
    copy_flush(a, p);
    screen_diag_flush(&display_diagnostics, a->x1, a->y1, a->x2, a->y2,
                      start, board_time_us(), lv_disp_flush_is_last(d));
    lv_disp_flush_ready(d);
  }
}
