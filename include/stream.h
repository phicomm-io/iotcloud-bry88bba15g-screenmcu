/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "jpeg_image.h"
#include "mjpeg.h"
#define MEDIA_STREAM_CONFIG_BYTES 519u
enum {
  STREAM_OFF,
  STREAM_PAUSED,
  STREAM_CONNECTING,
  STREAM_READING,
  STREAM_BACKOFF
};
typedef struct {
  char url[384], username[64], password[64];
  unsigned target_width, target_height, fps;
  bool enabled, verify_tls;
  unsigned state, error, failures, auth_attempt;
  uint32_t connection, rx_sequence, now, at, retry_at, frame_at, frame_bytes;
  uint32_t frame_due;
  uint32_t frames, dropped, bytes, pixel_crc, revision, period_start,
      period_frames, actual_millifps;
  bool ready, tls, digest;
  unsigned port, request_at, request_size;
  char host[128], path[512], request[1536], challenge[512];
  mjpeg_parser parser;
  uint8_t jpeg[MEDIA_JPEG_MAX] __attribute__((aligned(4)));
  uint16_t pixels[MEDIA_WIDTH_MAX * 864u] __attribute__((aligned(64)));
  uint16_t width, height;
  void *context;
  bool (*send)(void *, const uint8_t *, size_t);
  media_decode_fn decode;
} screen_stream;
extern screen_stream stream;
void stream_init(screen_stream *);
unsigned stream_config(screen_stream *, const uint8_t *, size_t);
void stream_poll(screen_stream *, uint32_t, bool visible, bool link_ready);
void stream_socket(screen_stream *, uint32_t, const uint8_t *, size_t);
size_t stream_status(const screen_stream *, uint8_t *, size_t);
