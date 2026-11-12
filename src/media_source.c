/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_source.h"
#include <string.h>
media_source_owner media_source;
void media_source_init(media_source_owner *o) {
  memset(o, 0, sizeof(*o));
  o->page[0] = MEDIA_STREAM_CONFIG;
  o->page[1] = 64; o->page[2] = 1; o->page[3] = 240;
  o->page[5] = 1; o->page[6] = 2;
}
unsigned media_source_config(media_source_owner *o, screen_stream *s,
                             const uint8_t *p, size_t n) {
  if (!stream_config_valid(p, n)) return MEDIA_RANGE;
  memcpy(o->page, p, n);
  if (!o->notice_id) {
    unsigned result = stream_config(s, p, n);
    if (result) return result;
    memcpy(o->current, p, n);
    s->width = s->height = 0;
  }
  return MEDIA_OK;
}
void media_source_poll(media_source_owner *o, screen_media *m, screen_stream *s,
                       uint32_t now, bool page_visible, bool display_visible,
                       bool link_ready) {
  const media_notice *notice = media_current(m);
  uint32_t id = notice && notice->source[0] ? notice->id : 0;
  const uint8_t *configuration = id ? notice->source : o->page;
  uint32_t elapsed = o->last_tick ? now - o->last_tick : 0;
  o->last_tick = now;
  if (o->notice_id != id) {
    o->waiting_ms = elapsed = 0;
    m->source_ready = false;
  }
  if (memcmp(o->current, configuration, sizeof(o->current))) {
    /* Close the previous connection before changing ownership. Connection IDs
     * remain monotonic, so delayed packets cannot reach the next source. */
    stream_config(s, configuration, sizeof(o->current));
    memcpy(o->current, configuration, sizeof(o->current));
    s->width = s->height = 0;
    if (id) m->presented = false;
    m->source_ready = false;
    o->waiting_ms = elapsed = 0;
  }
  o->notice_id = id;
  bool visible = display_visible && !m->suspended && (id || (page_visible && !notice));
  stream_poll(s, now, visible, link_ready);
  if (id) {
    if (s->width && s->height) m->source_ready = true;
    if (!m->source_ready && visible) {
      o->waiting_ms += elapsed;
      if (o->waiting_ms >= MEDIA_SOURCE_TIMEOUT_MS)
        media_source_failed(m, s->error ? s->error : 822);
    }
  }
}
