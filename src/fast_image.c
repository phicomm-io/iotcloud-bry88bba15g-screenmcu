/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fast_image.h"
#include <string.h>

static void image(lv_draw_ctx_t *ctx, const lv_draw_img_dsc_t *dsc,
                  const lv_area_t *coords, const uint8_t *bytes, lv_img_cf_t cf) {
  lv_area_t area = *ctx->clip_area;
  if (cf != LV_IMG_CF_TRUE_COLOR || dsc->angle || !dsc->zoom ||
      dsc->antialias || dsc->opa != LV_OPA_COVER ||
      dsc->recolor_opa != LV_OPA_TRANSP ||
      dsc->blend_mode != LV_BLEND_MODE_NORMAL ||
      lv_draw_mask_is_any(&area) || lv_area_get_width(&area) > 480) {
    lv_draw_sw_img_decoded(ctx, dsc, coords, bytes, cf);
    return;
  }
  unsigned width = lv_area_get_width(coords), height = lv_area_get_height(coords);
  unsigned count = lv_area_get_width(&area), stride = lv_area_get_width(ctx->buf_area);
  /* Match LVGL 8's reciprocal zoom and half-pixel rounding exactly. Compute
   * horizontal sampling once per strip, then reuse enlarged source rows. */
  int32_t inverse = 65536 / dsc->zoom;
  int16_t source_x[480];
  unsigned begin = count, end = 0;
  for (unsigned x = 0; x < count; x++) {
    int32_t local = area.x1 + (int)x - coords->x1 - dsc->pivot.x;
    int32_t sx = (local * inverse + dsc->pivot.x * 256 + 128) >> 8;
    source_x[x] = sx;
    if (sx >= 0 && (unsigned)sx < width) {
      if (begin == count) begin = x;
      end = x + 1;
    }
  }
  if (begin == count) return;
  const lv_color_t *pixels = (const lv_color_t *)bytes;
  lv_color_t *destination = (lv_color_t *)ctx->buf +
      (area.y1 - ctx->buf_area->y1) * stride + area.x1 - ctx->buf_area->x1;
  int32_t previous = -1;
  for (int y = area.y1; y <= area.y2; y++, destination += stride) {
    int32_t sy = ((y - coords->y1 - dsc->pivot.y) * inverse +
                   dsc->pivot.y * 256 + 128) >> 8;
    if (sy < 0 || (unsigned)sy >= height) continue;
    if (sy == previous) {
      lv_memcpy(destination + begin, destination - stride + begin,
             (end - begin) * sizeof(*destination));
    } else {
      const lv_color_t *row = pixels + sy * width;
      for (unsigned x = begin; x < end; x++) destination[x] = row[source_x[x]];
    }
    previous = sy;
  }
}

void screen_draw_ctx_init(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx) {
  lv_draw_sw_init_ctx(driver, ctx);
  ctx->draw_img_decoded = image;
}
