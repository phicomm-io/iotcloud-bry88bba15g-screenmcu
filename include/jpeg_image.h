/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "media_wire.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint16_t code[162];
  uint8_t length[162], value[162];
} media_huffman;
typedef struct {
  uint16_t width, height;
  uint8_t sampling, quant[3], dc[3], ac[3];
  uint8_t qtable[3][64], qmask, hmask;
  media_huffman huffman[2][2];
  uint32_t scan_offset, scan_bytes;
} media_jpeg;
/* Baseline, 8-bit, interleaved YCbCr 4:2:0/4:2:2/4:4:4. The hardware driver
 * has no restart interval input; reject DRI and restart markers explicitly. */
bool media_jpeg_parse(media_jpeg *, const uint8_t *, size_t);
/* RGB stride is padded to 16 pixels; buffer includes the padded bottom MCU.
 * Implemented by BSP. Never decode into the LCD scanout buffer. */
typedef bool (*media_decode_fn)(void *, const uint8_t *, size_t,
                                const media_jpeg *, uint16_t *, unsigned);
