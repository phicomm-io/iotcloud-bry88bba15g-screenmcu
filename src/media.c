/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media.h"
#include "scene.h"
#include <string.h>
screen_media media;
void media_init(screen_media *m) { memset(m, 0, sizeof(*m)); }
static media_image *image(screen_media *m, uint32_t id) {
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
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
static void remember(screen_media *m, const media_notice *q) {
  for (unsigned i = 0; i < 8; i++)
    if (m->history[i].id == q->id) {
      m->history[i].state = q->state;
      return;
    }
  unsigned i = m->history_next++ % 8;
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
  }
  memset(q, 0, sizeof(*q));
  m->revision++;
}
bool media_touch(screen_media *m) {
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->active && m->queue[i].id == m->active && m->presented) {
      finish(m, &m->queue[i], PUSH_CONFIRMED);
      return true;
    }
  return false;
}
bool media_wake(const screen_media *m) {
  const media_notice *q = media_current(m);
  return q && !m->suspended && q->wake;
}
void media_present(screen_media *m) {
  media_notice *q = (media_notice *)media_current(m);
  if (q && !m->suspended && !m->presented) {
    m->presented = true;
    q->state = PUSH_DISPLAYED;
    remember(m, q);
    m->revision++;
  }
}
unsigned media_command(screen_media *m, const uint8_t *p, size_t n) {
  if (!n)
    return MEDIA_RANGE;
  if (p[0] == MEDIA_QUERY)
    return n == 1 ? MEDIA_OK : MEDIA_RANGE;
  if (m->suspended)
    return MEDIA_BUSY;
  if (n < 5 || !media_u32(p + 1))
    return MEDIA_RANGE;
  uint32_t id = media_u32(p + 1);
  media_image *im = image(m, id);
  switch (p[0]) {
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
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
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
  case MEDIA_PUSH: {
    if (n < 16 || n > MEDIA_PACKET_MAX)
      return MEDIA_RANGE;
    uint32_t fingerprint = media_crc32(p, n), image_id = media_u32(p + 5),
             duration = media_u32(p + 9);
    unsigned title = p[15], text = n - 16 - title;
    if (title > 64 || 16u + title > n || text > 512 || p[13] > 3 || !p[14] ||
        p[14] > 100 || duration > 3600000 || (!(p[13] & 2) && !duration))
      return MEDIA_RANGE;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id == id)
        return m->queue[i].signature == fingerprint ? MEDIA_OK : MEDIA_RANGE;
    for (unsigned i = 0; i < 8; i++)
      if (m->history[i].id == id)
        return m->history[i].signature == fingerprint ? MEDIA_OK : MEDIA_RANGE;
    im = image(m, image_id);
    if (image_id && (!im || im->state != 2))
      return MEDIA_NOT_FOUND;
    if (!image_id && !title && !text)
      return MEDIA_RANGE;
    media_notice *q = NULL;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (!m->queue[i].id) {
        q = &m->queue[i];
        break;
      }
    if (!q)
      return MEDIA_BUSY;
    /* Keep FIFO order by shifting into the first empty tail, including when
     * the current item was dismissed out of order. */
    unsigned slot = 0;
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id)
        m->queue[slot++] = m->queue[i];
    for (unsigned i = slot; i < MEDIA_QUEUE; i++)
      memset(&m->queue[i], 0, sizeof(m->queue[i]));
    q = &m->queue[slot];
    memset(q, 0, sizeof(*q));
    q->id = id;
    q->image = image_id;
    q->signature = fingerprint;
    q->remaining = duration;
    q->wake = p[13] & 1;
    q->confirm = (p[13] >> 1) & 1;
    q->brightness = p[14];
    q->state = PUSH_QUEUED;
    memcpy(q->title, p + 16, title);
    memcpy(q->text, p + 16 + title, text);
    if (!scene_utf8(q->title, sizeof(q->title)) ||
        !scene_text_utf8(q->text, sizeof(q->text))) {
      memset(q, 0, sizeof(*q));
      return MEDIA_RANGE;
    }
    remember(m, q);
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
  media_notice *q = (media_notice *)media_current(m);
  if (q && m->presented && !suspended && !was_suspended && !q->confirm) {
    if (elapsed >= q->remaining)
      finish(m, q, PUSH_TIMEDOUT);
    else
      q->remaining -= elapsed;
  }
  if (!m->active && !suspended) {
    for (unsigned i = 0; i < MEDIA_QUEUE; i++)
      if (m->queue[i].id) {
        q = &m->queue[i];
        if (asleep && !q->wake)
          break;
        media_image *im = image(m, q->image);
        m->width = m->height = 0;
        if (q->image) {
          media_jpeg j;
          if (!im || !media_jpeg_parse(&j, im->bytes, im->size) || !m->decode ||
              !m->decode(m->context, im->bytes, im->size, &j, m->pixels,
                         (j.width + 15u) & ~15u)) {
            m->decode_errors++;
            finish(m, q, PUSH_FAILED);
            break;
          }
          unsigned stride = (j.width + 15u) & ~15u;
          for (unsigned row = 1; row < j.height; row++)
            memmove(m->pixels + row * j.width, m->pixels + row * stride,
                    j.width * 2u);
          m->width = j.width;
          m->height = j.height;
          m->decoded++;
          m->pixel_crc = media_crc32(m->pixels, j.width * j.height * 2u);
        }
        m->active = q->id;
        m->was_asleep = asleep;
        m->presented = false;
        m->revision++;
        break;
      }
  }
  for (unsigned i = 0; i < MEDIA_QUEUE; i++)
    if (m->images[i].id && !referenced(m, m->images[i].id) &&
        (uint32_t)(now - m->images[i].at) > 120000)
      m->images[i].id = 0;
}
size_t media_status(const screen_media *m, uint8_t *p, size_t cap) {
  const size_t bytes = 24 + MEDIA_QUEUE * 17 + 8 * 5;
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
  size_t at = 24;
  for (unsigned i = 0; i < MEDIA_QUEUE; i++, at += 17) {
    const media_image *im = &m->images[i];
    media_put32(p + at, im->id);
    media_put32(p + at + 4, im->size);
    media_put32(p + at + 8, im->received);
    media_put32(p + at + 12, im->crc);
    p[at + 16] = im->state;
  }
  for (unsigned i = 0; i < 8; i++, at += 5) {
    media_put32(p + at, m->history[i].id);
    p[at + 4] = m->history[i].state;
  }
  return bytes;
}
