/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fast_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static lv_color_t strips[480 * 40], pixels[320 * 240];
static uint16_t frame[480 * 854], reference[480 * 854];
LV_FONT_DECLARE(zh32);
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++) {
    memcpy(frame + y * 480 + a->x1, p, lv_area_get_width(a) * 2u);
    p += lv_area_get_width(a);
  }
  lv_disp_flush_ready(d);
}
int main(void) {
  lv_init();
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, strips, NULL, 480 * 40);
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
  driver.hor_res = 480; driver.ver_res = 854;
  driver.draw_buf = &buffer; driver.flush_cb = flush;
  lv_disp_t *display = lv_disp_drv_register(&driver);
  lv_obj_set_style_text_font(lv_scr_act(), &zh32, 0);
  for (unsigned y = 0; y < 240; y++) for (unsigned x = 0; x < 320; x++)
    pixels[y * 320 + x].full = (x * 7919 + y * 5807) & 65535;
  lv_img_dsc_t descriptor = {.header = {.cf = LV_IMG_CF_TRUE_COLOR, .w = 320, .h = 240},
      .data_size = sizeof(pixels), .data = (const uint8_t *)pixels};
  lv_obj_t *clip = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(clip);
  lv_obj_set_pos(clip, 12, 150); lv_obj_set_size(clip, 456, 640);
  lv_obj_t *image = lv_img_create(clip);
  lv_img_set_src(image, &descriptor);
  lv_img_set_antialias(image, false);
  const unsigned zooms[] = {1, 83, 128, 255, 256, 364, 700, 1365};
  for (unsigned mode = 0; mode < 4; mode++) for (unsigned z = 0; z < sizeof(zooms) / sizeof(*zooms); z++) {
    lv_img_set_zoom(image, zooms[z]);
    lv_img_set_angle(image, mode == 3 ? 150 : 0);
    lv_obj_center(image);
    lv_obj_set_style_img_opa(image, mode == 1 ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(clip, mode == 2 ? 40 : 0, 0);
    lv_obj_set_style_clip_corner(clip, mode == 2, 0);
    display->driver->draw_ctx->draw_img_decoded = lv_draw_sw_img_decoded;
    lv_obj_invalidate(lv_scr_act()); lv_refr_now(display);
    memcpy(reference, frame, sizeof(frame));
    screen_draw_ctx_init(display->driver, display->driver->draw_ctx);
    lv_obj_invalidate(lv_scr_act()); lv_refr_now(display);
    if (memcmp(reference, frame, sizeof(frame))) {
      fprintf(stderr, "renderer mismatch mode=%u zoom=%u\n", mode, zooms[z]);
      for (unsigned i = 0; i < sizeof(frame) / sizeof(*frame); i++) if (frame[i] != reference[i]) {
        fprintf(stderr, "first pixel (%u,%u): %04x != %04x\n", i % 480, i / 480, frame[i], reference[i]); break;
      }
      assert(false);
    }
  }
  puts("PASS: 32 complete native frames match LVGL, including clipping and fallback styles");
}
