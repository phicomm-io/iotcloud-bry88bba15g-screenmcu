/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "jpeg_image.h"
typedef struct {
  uint32_t id, size, crc, received, at;
  uint16_t width, height;
  uint8_t state;
  uint8_t bytes[MEDIA_JPEG_MAX] __attribute__((aligned(4)));
} media_image;
typedef struct {
  uint32_t id, image, signature, remaining;
  uint8_t state, wake, brightness, confirm;
  char title[65], text[513];
} media_notice;
typedef struct {
  media_image images[MEDIA_IMAGE_SLOTS];
  media_notice queue[MEDIA_QUEUE];
  struct {
    uint32_t id, signature;
    uint8_t state;
  } history[MEDIA_HISTORY];
  unsigned history_next;
  uint32_t active, revision, decoded, decode_errors, pixel_crc, now, last_tick;
  uint16_t width, height;
  bool presented, suspended, was_asleep, parked;
  bool tap_pending, tap_page;
  uint32_t tap_at, tap_target;
  uint16_t pixels[MEDIA_WIDTH_MAX * 864u] __attribute__((aligned(64)));
  media_decode_fn decode;
  void *context;
} screen_media;
extern screen_media media;
void media_init(screen_media *);
unsigned media_command(screen_media *, const uint8_t *, size_t);
/* actual_visible is set only after the renderer has presented the overlay. */
void media_tick(screen_media *, uint32_t, bool asleep, bool suspended);
const media_notice *media_current(const screen_media *);
bool media_touch(screen_media *);
unsigned media_count(const screen_media *);
unsigned media_view(const screen_media *);
const media_notice *media_at(const screen_media *, unsigned index);
unsigned media_select(screen_media *, unsigned index);
/* Returns one only when an ordinary page tap has survived the double-tap
 * window. Notification confirmation and double-tap navigation are consumed. */
unsigned media_button(screen_media *, uint32_t now, bool down, bool released,
                      bool page_tap, bool blocked);
void media_present(screen_media *);
bool media_wake(const screen_media *);
size_t media_status(const screen_media *, uint8_t *, size_t);
