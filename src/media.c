/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media.h"
#include "scene.h"
#include "stream.h"
#include <string.h>
screen_media media;
void media_init(screen_media *m) { memset(m, 0, sizeof(*m)); }
static media_image *image(screen_media *m, uint32_t id) {
  for (unsigned i = 0; i < MEDIA_IMAGE_SLOTS; i++)
    if (id && m->images[i].id == id)
      return &m->images[i];
  return NULL;
}
static bool referenced(const screen_media *m, uint32_t id) {
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->queue[i].id && m->queue[i].image == id)
      return true;
  return false;
}
const media_notice *media_current(const screen_media *m) {
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->active && m->queue[i].id == m->active)
      return &m->queue[i];
  return NULL;
}
unsigned media_count(const screen_media *m) {
  unsigned count = 0;
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    count += m->queue[i].id != 0;
  return count;
}
const media_notice *media_at(const screen_media *m, unsigned index) {
  if (!index)
    return NULL;
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->queue[i].id && !--index)
      return &m->queue[i];
  return NULL;
}
unsigned media_view(const screen_media *m) {
  unsigned index = 0;
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->queue[i].id) {
      index++;
      if (m->queue[i].id == m->active)
        return index;
    }
  return 0;
}
static void remember(screen_media *m, const media_notice *q) {
  for (unsigned i = 0; i < MEDIA_HISTORY; i++)
    if (m->history[i].id == q->id) {
      m->history[i].state = q->state;
      /* Keep the newest terminal event readable when an eviction is followed
       * immediately by accepting its replacement into a full history ring. */
      if (q->state >= PUSH_CONFIRMED) m->history_next = i + 1;
      return;
    }
  unsigned i = m->history_next++ % MEDIA_HISTORY;
  m->history[i].id = q->id;
  m->history[i].signature = q->signature;
  m->history[i].state = q->state;
}
static void finish(screen_media *m, media_notice *q, unsigned state) {
  q->state = state;
  remember(m, q);
  if (m->active == q->id) {
    m->active = 0;
    m->presented = false;
    m->source_ready = false;
    m->parked = false;
  }
  memset(q, 0, sizeof(*q));
  m->revision++;
}
/* Decode only when selecting or replacing an uploaded image. The foreground
 * owner serializes this buffer with LVGL; LCD scanout uses separate frames. */
static bool decode_image(screen_media *m, uint32_t id) {
  media_image *im = image(m, id);
  media_jpeg j;
  if (!im || !media_jpeg_parse(&j, im->bytes, im->size) || !m->decode ||
      !m->decode(m->context, im->bytes, im->size, &j, m->pixels,
                 (j.width + 15u) & ~15u)) {
    m->decode_errors++;
    return false;
  }
  unsigned stride = (j.width + 15u) & ~15u;
  for (unsigned row = 1; row < j.height; row++)
    memmove(m->pixels + row * j.width, m->pixels + row * stride, j.width * 2u);
  m->width = j.width;
  m->height = j.height;
  m->decoded++;
  m->pixel_crc = media_crc32(m->pixels, j.width * j.height * 2u);
  return true;
}
static unsigned update_notice(screen_media *m, const uint8_t *p, size_t n) {
  if (n < 18 || n > MEDIA_PACKET_MAX) return MEDIA_RANGE;
  unsigned mask = p[16] | (unsigned)p[17] << 8;
  unsigned source_at = 18 + ((mask & MEDIA_UPDATE_TTL) ? 4 : 0);
  unsigned body = source_at + ((mask & MEDIA_UPDATE_SOURCE) ? MEDIA_STREAM_CONFIG_BYTES : 0);
  unsigned title = p[15];
  if (!mask || (mask & ~MEDIA_UPDATE_ALL) || body + title > n || title > 64 ||
      n - body - title > 512 || (!(mask & MEDIA_UPDATE_TITLE) && title) ||
      (!(mask & MEDIA_UPDATE_TEXT) && n != body + title) || p[13] > 3 ||
      ((mask & MEDIA_UPDATE_IMAGE) && (mask & MEDIA_UPDATE_URL))) return MEDIA_RANGE;
  media_notice *q = NULL;
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->queue[i].id == media_u32(p + 1)) q = &m->queue[i];
  if (!q) return MEDIA_NOT_FOUND;
  /* Validate the complete result before publishing any metadata. Creation's
   * fingerprint is retained so a late send retry never creates a second item. */
  media_notice next = *q;
  if (mask & MEDIA_UPDATE_TITLE) {
    memset(next.title, 0, sizeof(next.title));memcpy(next.title, p + body, title);
  }
  if (mask & MEDIA_UPDATE_TEXT) {
    memset(next.text, 0, sizeof(next.text));memcpy(next.text, p + body + title, n - body - title);
  }
  if (!scene_utf8(next.title, sizeof(next.title)) ||
      !scene_text_utf8(next.text, sizeof(next.text))) return MEDIA_RANGE;
  if (mask & MEDIA_UPDATE_MODE) next.confirm = (p[13] >> 1) & 1;
  if (mask & MEDIA_UPDATE_WAKE) next.wake = p[13] & 1;
  if (mask & MEDIA_UPDATE_BRIGHTNESS) {
    if (!p[14] || p[14] > 100) return MEDIA_RANGE;
    next.brightness = p[14];
  }
  if (mask & MEDIA_UPDATE_DURATION) {
    uint32_t duration = media_u32(p + 9);
    if (!duration || duration > 3600000) return MEDIA_RANGE;
    if (duration > next.remaining) next.remaining = duration;
  }
  if (mask & MEDIA_UPDATE_TTL) {
    if (media_u32(p + 18) > MEDIA_TTL_MAX_MS) return MEDIA_RANGE;
    next.ttl_remaining = media_u32(p + 18);
  }
  if (!next.confirm && !next.remaining) return MEDIA_RANGE;
  if (mask & MEDIA_UPDATE_IMAGE) {
    next.image = media_u32(p + 5);
    media_image *im = image(m, next.image);
    if (next.image && (!im || im->state != 2)) return MEDIA_NOT_FOUND;
    memset(next.source, 0, sizeof(next.source));
  }
  if (mask & MEDIA_UPDATE_SOURCE) {
    const uint8_t *source = p + source_at;
    if ((mask & MEDIA_UPDATE_URL) && !source[7]) {
      if (mask & (MEDIA_UPDATE_SOURCE & ~MEDIA_UPDATE_URL)) return MEDIA_RANGE;
      memset(next.source, 0, sizeof(next.source));next.image = 0;
    } else {
      if (!next.source[0]) {
        if (!(mask & MEDIA_UPDATE_URL)) return MEDIA_RANGE;
        next.source[0] = MEDIA_STREAM_CONFIG;
        next.source[1] = 64;next.source[2] = 1;next.source[3] = 240;
        next.source[5] = 1;next.source[6] = 3;
      }
      if (mask & MEDIA_UPDATE_URL) {
        if (!memchr(source + 7, 0, 384)) return MEDIA_RANGE;
        if (strcmp((const char *)next.source + 7, (const char *)source + 7))
          memset(next.source + 391, 0, 128); /* Do not forward old credentials. */
        memcpy(next.source + 7, source + 7, 384);next.image = 0;
      }
      if (mask & MEDIA_UPDATE_USERNAME) memcpy(next.source + 391, source + 391, 64);
      if (mask & MEDIA_UPDATE_PASSWORD) memcpy(next.source + 455, source + 455, 64);
      if (mask & MEDIA_UPDATE_WIDTH) memcpy(next.source + 1, source + 1, 2);
      if (mask & MEDIA_UPDATE_HEIGHT) memcpy(next.source + 3, source + 3, 2);
      if (mask & MEDIA_UPDATE_FPS) next.source[5] = source[5];
      if (mask & MEDIA_UPDATE_TLS) next.source[6] = (next.source[6] & ~2u) | (source[6] & 2u);
      if (!next.source[7] || !stream_config_valid(next.source, sizeof(next.source))) return MEDIA_RANGE;
    }
  }
  if (!next.image && !next.source[0] && !next.title[0] && !next.text[0]) return MEDIA_RANGE;
  bool active = m->active == q->id;
  bool image_changed = next.image != q->image;
  bool source_changed = memcmp(next.source, q->source, sizeof(next.source)) != 0;
  if (active && image_changed && next.image && !decode_image(m, next.image)) {
    /* A hardware error may have partially written the RGB buffer. Restore the
     * original JPEG before any renderer can access it again. */
    if (q->image && !decode_image(m, q->image)) finish(m, q, PUSH_FAILED);
    return MEDIA_DECODE;
  }
  if (active && (image_changed || source_changed)) {
    m->presented = false;m->source_ready = false;m->source_error = 0;
    if (!next.image) m->width = m->height = 0;
    next.state = PUSH_QUEUED;
  }
  if (memcmp(q, &next, sizeof(next))) { *q = next;m->revision++; }
  m->last_push_id = q->id;
  return MEDIA_OK;
}
unsigned media_select(screen_media *m, unsigned index) {
  if (m->suspended)
    return MEDIA_BUSY;
  if (index > MEDIA_STACK || (index && !media_at(m, index)))
    return MEDIA_RANGE;
  m->tap_pending = false;
  if (!index) {
    m->active = 0;
    m->presented = false;
    m->source_ready = false;
    m->parked = true;
    m->revision++;
    return MEDIA_OK;
  }
  media_notice *q = (media_notice *)media_at(m, index);
  if (m->active == q->id)
    return MEDIA_OK;
  m->active = 0;
  m->presented = false;
  m->source_ready = false;
  m->source_error = 0;
  m->width = m->height = 0;
  if (q->image && !decode_image(m, q->image)) {
    finish(m, q, PUSH_FAILED);
    return MEDIA_DECODE;
  }
  m->active = q->id;
  m->parked = false;
  m->revision++;
  return MEDIA_OK;
}
unsigned media_button(screen_media *m, uint32_t now, bool down, bool released,
                      bool page_tap, bool blocked) {
  if (blocked) {
    m->tap_pending = false;
    return 0;
  }
  unsigned page = 0;
  if (m->tap_pending && (uint32_t)(now - m->tap_at) > MEDIA_TAP_WINDOW_MS) {
    m->tap_pending = false;
    if (!down) {
      if (m->tap_target && m->active == m->tap_target)
        media_touch(m);
      else if (!m->tap_target && !m->active && m->parked)
        page = m->tap_page;
    }
  }
  if (!released)
    return page;
  if (m->tap_pending) {
    m->tap_pending = false;
    unsigned limit = media_count(m);
    if (limit > MEDIA_STACK)
      limit = MEDIA_STACK;
    if (limit) {
      unsigned next = media_view(m) + 1;
      media_select(m, next > limit ? 0 : next);
    }
    return page;
  }
  if (!media_count(m))
    return page || page_tap;
  m->tap_pending = true;
  m->tap_page = page_tap;
  m->tap_target = m->active;
  m->tap_at = now;
  return page;
}
bool media_touch(screen_media *m) {
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->active && m->queue[i].id == m->active && m->presented) {
      finish(m, &m->queue[i], PUSH_CONFIRMED);
      return true;
    }
  return false;
}
void media_present(screen_media *m) {
  media_notice *q = (media_notice *)media_current(m);
  if (q && (!q->source[0] || m->source_ready) && !m->suspended && !m->presented) {
    m->presented = true;
    q->state = PUSH_DISPLAYED;
    remember(m, q);
    m->revision++;
  }
}
void media_source_failed(screen_media *m, unsigned error) {
  media_notice *q = (media_notice *)media_current(m);
  if (q && q->source[0]) {
    m->source_error = error;
    finish(m, q, PUSH_FAILED);
  }
}
unsigned media_command(screen_media *m, const uint8_t *p, size_t n) {
  if (!n)
    return MEDIA_RANGE;
  if (p[0] == MEDIA_QUERY)
    return n == 1 ? MEDIA_OK : MEDIA_RANGE;
  if (m->suspended)
    return MEDIA_BUSY;
  if (p[0] == MEDIA_SELECT)
    return n == 2 ? media_select(m, p[1]) : MEDIA_RANGE;
  if (n < 5 || !media_u32(p + 1))
    return MEDIA_RANGE;
  uint32_t id = media_u32(p + 1);
  media_image *im = image(m, id);
  switch (p[0]) {
  case MEDIA_PUSH_UPDATE:
    return update_notice(m, p, n);
  case MEDIA_BEGIN: {
    if (n != 17)
      return MEDIA_RANGE;
    uint32_t size = media_u32(p + 5), crc = media_u32(p + 9);
    unsigned w = (unsigned)p[13] | (unsigned)p[14] << 8,
             h = (unsigned)p[15] | (unsigned)p[16] << 8;
    if (!size || size > MEDIA_JPEG_MAX || !w || w > MEDIA_WIDTH_MAX || !h ||
        h > MEDIA_HEIGHT_MAX)
      return MEDIA_RANGE;
    if (im)
      return im->size == size && im->crc == crc && im->width == w &&
                     im->height == h
                 ? MEDIA_OK
                 : MEDIA_RANGE;
    for (unsigned i = 0; i < MEDIA_IMAGE_SLOTS; i++)
      if (!m->images[i].id) {
        im = &m->images[i];
        break;
      }
    if (!im)
      return MEDIA_BUSY;
    im->id = id;
    im->size = size;
    im->crc = crc;
    im->width = w;
    im->height = h;
    im->received = 0;
    im->state = 1;
    im->at = m->now;
    m->revision++;
    return MEDIA_OK;
  }
  case MEDIA_WRITE: {
    if (n <= 9 || n > MEDIA_CHUNK + 9)
      return MEDIA_RANGE;
    if (!im)
      return MEDIA_NOT_FOUND;
    uint32_t at = media_u32(p + 5);
    size_t bytes = n - 9;
    if (at > im->received || at > im->size || bytes > im->size - at)
      return MEDIA_RANGE;
    if (at < im->received)
      return bytes <= im->received - at && !memcmp(im->bytes + at, p + 9, bytes)
                 ? MEDIA_OK
                 : MEDIA_RANGE;
    if (im->state != 1)
      return MEDIA_BUSY;
    memcpy(im->bytes + at, p + 9, bytes);
    im->received += bytes;
    im->at = m->now;
    return MEDIA_OK;
  }
  case MEDIA_COMMIT: {
    if (n != 5)
      return MEDIA_RANGE;
    if (!im)
      return MEDIA_NOT_FOUND;
    if (im->state == 2)
      return MEDIA_OK;
    if (im->received != im->size)
      return MEDIA_RANGE;
    if (media_crc32(im->bytes, im->size) != im->crc)
      return MEDIA_CRC;
    media_jpeg j;
    if (!media_jpeg_parse(&j, im->bytes, im->size) || j.width != im->width ||
        j.height != im->height)
      return MEDIA_FORMAT;
    im->state = 2;
    im->at = m->now;
    m->revision++;
    return MEDIA_OK;
  }
  case MEDIA_CANCEL:
    if (n != 5)
      return MEDIA_RANGE;
    if (!im)
      return MEDIA_OK;
    if (referenced(m, id))
      return MEDIA_BUSY;
    im->id = 0;
    m->revision++;
    return MEDIA_OK;
  case MEDIA_PUSH:
  case MEDIA_PUSH_URL:
  case MEDIA_PUSH_POLICY: {
    bool policy = p[0] == MEDIA_PUSH_POLICY;
    if (policy && (n < 22 || p[20] > MEDIA_OVERFLOW_DROP_OLDEST || p[21] > 1 ||
                   media_u32(p + 16) > MEDIA_TTL_MAX_MS)) return MEDIA_RANGE;
    bool remote = policy ? p[21] != 0 : p[0] == MEDIA_PUSH_URL;
    unsigned source_at = policy ? 22 : 16;
    unsigned body = source_at + (remote ? MEDIA_STREAM_CONFIG_BYTES : 0);
    uint32_t ttl = policy ? media_u32(p + 16) : 0;
    unsigned overflow = policy ? p[20] : MEDIA_OVERFLOW_REJECT;
    if (n < body || n > MEDIA_PACKET_MAX)
      return MEDIA_RANGE;
    uint32_t fingerprint = media_crc32(p, n), image_id = media_u32(p + 5),
             duration = media_u32(p + 9);
    unsigned title = p[15], text = n - body - title;
    if (title > 64 || body + title > n || text > 512 || p[13] > 3 || !p[14] ||
        p[14] > 100 || duration > 3600000 || (!(p[13] & 2) && !duration))
      return MEDIA_RANGE;
    const uint8_t *source = remote ? p + source_at : NULL;
    if (remote && (image_id || !(source[6] & 1) || !source[7] ||
                   !stream_config_valid(source, MEDIA_STREAM_CONFIG_BYTES)))
      return MEDIA_RANGE;
    char title_value[65] = {0}, text_value[513] = {0};
    memcpy(title_value, p + body, title);
    memcpy(text_value, p + body + title, text);
    if (!scene_utf8(title_value, sizeof(title_value)) ||
        !scene_text_utf8(text_value, sizeof(text_value))) return MEDIA_RANGE;
    media_notice *known = NULL;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id == id) known = &m->queue[i];
    if (known && (!remote || !known->source[0] ||
                  strcmp((const char *)known->source + 7, (const char *)source + 7))) {
      if (known->signature != fingerprint) return MEDIA_RANGE;
      m->last_push_id = known->id;
      return MEDIA_OK;
    }
    if (!known) for (unsigned i = 0; i < MEDIA_HISTORY; i++)
      if (m->history[i].id == id) {
        if (m->history[i].signature != fingerprint) return MEDIA_RANGE;
        m->last_push_id = id;
        return MEDIA_OK;
      }
    if (remote) for (unsigned i = 0; i < MEDIA_QUEUE; i++) {
      media_notice *q = &m->queue[i];
      if (!q->id || !q->source[0] ||
          strcmp((const char *)q->source + 7, (const char *)source + 7)) continue;
      /* Renew the same live source in its existing stack position. A fresh
       * UART sequence is an event; transport retransmissions are deduplicated
       * by the owner before reaching here. Shorter events never shorten it. */
      if (duration > q->remaining) q->remaining = duration;
      if (policy && ttl > q->ttl_remaining) q->ttl_remaining = ttl;
      bool changed = strcmp(q->title, title_value) || strcmp(q->text, text_value);
      memcpy(q->title, title_value, sizeof(q->title));
      memcpy(q->text, text_value, sizeof(q->text));
      memcpy(q->source, source, sizeof(q->source));
      q->wake = p[13] & 1;
      q->confirm = (p[13] >> 1) & 1;
      q->brightness = p[14];
      m->last_push_id = q->id;
      if (changed) m->revision++;
      return MEDIA_OK;
    }
    im = image(m, image_id);
    if (image_id && (!im || im->state != 2))
      return MEDIA_NOT_FOUND;
    if (!remote && !image_id && !title && !text)
      return MEDIA_RANGE;
    media_notice *q = NULL;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (!m->queue[i].id) {
        q = &m->queue[i];
        break;
      }
    if (!q) {
      if (overflow == MEDIA_OVERFLOW_DROP_NEWEST) {
        media_notice dropped = {0};
        dropped.id = id;dropped.signature = fingerprint;dropped.state = PUSH_DROPPED;
        remember(m, &dropped);m->last_push_id = id;m->revision++;
        return MEDIA_OK;
      }
      if (overflow == MEDIA_OVERFLOW_DROP_OLDEST) {
        for (unsigned i = 0; i < MEDIA_QUEUE; i++)
          if (m->queue[i].id != m->active) { q = &m->queue[i];break; }
        if (q) finish(m, q, PUSH_EVICTED);
      }
      if (!q) return MEDIA_BUSY;
    }
    /* Keep FIFO order by shifting into the first empty tail, including when
     * the current item was dismissed out of order. */
    unsigned slot = 0;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id)
        m->queue[slot++] = m->queue[i];
    for (unsigned i = slot; i < MEDIA_QUEUE; i++)
      memset(&m->queue[i], 0, sizeof(m->queue[i]));
    q = &m->queue[slot];
    q->id = id;
    q->image = image_id;
    q->signature = fingerprint;
    q->remaining = duration;
    q->ttl_remaining = ttl;
    q->wake = p[13] & 1;
    q->confirm = (p[13] >> 1) & 1;
    q->brightness = p[14];
    q->state = PUSH_QUEUED;
    memcpy(q->title, title_value, sizeof(q->title));
    memcpy(q->text, text_value, sizeof(q->text));
    if (remote) {
      memcpy(q->source, source, sizeof(q->source));
    }
    m->last_push_id = q->id;
    remember(m, q);
    if (!m->active)
      m->parked = false;
    m->revision++;
    return MEDIA_OK;
  }
  case MEDIA_DISMISS:
    if (n != 5)
      return MEDIA_RANGE;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id == id) {
        finish(m, &m->queue[i], PUSH_DISMISSED);
        return MEDIA_OK;
      }
    return MEDIA_OK;
  default:
    return MEDIA_RANGE;
  }
}
void media_tick(screen_media *m, uint32_t now, bool asleep, bool suspended) {
  uint32_t elapsed = m->last_tick ? now - m->last_tick : 0;
  m->now = now;
  m->last_tick = now;
  bool was_suspended = m->suspended;
  m->suspended = suspended;
  /* Hard lifetime runs while queued, hidden, suspended or awaiting confirm.
   * Unsigned elapsed remains correct across the millisecond clock wrap. */
  for (unsigned i = 0; i < MEDIA_QUEUE; i++) {
    media_notice *item = &m->queue[i];
    if (item->id && item->ttl_remaining) {
      if (elapsed >= item->ttl_remaining) finish(m, item, PUSH_EXPIRED);
      else item->ttl_remaining -= elapsed;
    }
  }
  media_notice *q = (media_notice *)media_current(m);
  if (q && m->presented && !suspended && !was_suspended && !q->confirm) {
    if (elapsed >= q->remaining)
      finish(m, q, PUSH_TIMEDOUT);
    else
      q->remaining -= elapsed;
  }
  if (!m->active && !m->parked && !suspended) {
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id) {
        q = &m->queue[i];
        if (asleep && !q->wake)
          break;
        media_select(m, 1);
        break;
      }
  }
  for (unsigned i = 0; i < MEDIA_IMAGE_SLOTS; i++)
    if (m->images[i].id && !referenced(m, m->images[i].id) &&
        (uint32_t)(now - m->images[i].at) > 120000)
      m->images[i].id = 0;
}
size_t media_status(const screen_media *m, uint8_t *p, size_t cap) {
  const size_t bytes = MEDIA_STATUS_BYTES;
  if (cap < bytes)
    return 0;
  memset(p, 0, bytes);
  media_put32(p, m->active);
  media_put32(p + 4, m->revision);
  media_put32(p + 8, m->decoded);
  media_put32(p + 12, m->decode_errors);
  media_put32(p + 16, m->pixel_crc);
  p[20] = m->presented;
  p[21] = m->suspended;
  p[22] = media_view(m);
  p[23] = media_count(m);
  p[28] = MEDIA_QUEUE;
  p[29] = MEDIA_IMAGE_SLOTS;
  p[30] = MEDIA_STACK;
  p[31] = 4;
  media_put32(p + 32, m->last_push_id);
  media_put32(p + 36, m->source_error);
  const media_notice *active = media_current(m);
  media_put32(p + 40, active ? active->remaining : 0);
  size_t at = MEDIA_STATUS_IMAGES;
  for (unsigned i = 0; i < MEDIA_IMAGE_SLOTS; i++, at += 17) {
    const media_image *im = &m->images[i];
    media_put32(p + at, im->id);
    media_put32(p + at + 4, im->size);
    media_put32(p + at + 8, im->received);
    media_put32(p + at + 12, im->crc);
    p[at + 16] = im->state;
  }
  for (unsigned i = 0; i < MEDIA_QUEUE; i++, at += MEDIA_STATUS_QUEUE_ITEM_BYTES) {
    const media_notice *q = media_at(m, i + 1);
    if (q) {
      media_put32(p + at, q->id);
      media_put32(p + at + 4, q->image);
      p[at + 8] = q->state;
      p[at + 9] = q->source[0] != 0;
      media_put32(p + at + 10, q->ttl_remaining);
    }
  }
  for (unsigned i = 0; i < MEDIA_HISTORY; i++, at += 5) {
    media_put32(p + at, m->history[i].id);
    p[at + 4] = m->history[i].state;
  }
  return bytes;
}
