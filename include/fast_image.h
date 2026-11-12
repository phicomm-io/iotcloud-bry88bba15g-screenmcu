/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "lvgl.h"
#include "src/draw/sw/lv_draw_sw.h"

/* Opaque, unrotated RGB565 camera/notification images use a row scaler.
 * All other image styles keep LVGL's normal renderer. */
void screen_draw_ctx_init(lv_disp_drv_t *, lv_draw_ctx_t *);
