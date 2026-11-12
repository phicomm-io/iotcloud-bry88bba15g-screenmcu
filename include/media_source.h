/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "media.h"
#include "stream.h"
/* One network/decode arena, with a saved page source underneath notifications. */
typedef struct {
  uint8_t page[MEDIA_STREAM_CONFIG_BYTES], current[MEDIA_STREAM_CONFIG_BYTES];
  uint32_t notice_id, last_tick, waiting_ms;
} media_source_owner;
extern media_source_owner media_source;
void media_source_init(media_source_owner *);
unsigned media_source_config(media_source_owner *, screen_stream *, const uint8_t *, size_t);
void media_source_poll(media_source_owner *, screen_media *, screen_stream *, uint32_t,
                       bool page_visible, bool display_visible, bool link_ready);
