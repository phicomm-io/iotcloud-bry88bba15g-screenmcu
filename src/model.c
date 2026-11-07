#include "screen.h"
#include <math.h>
#include <string.h>
unsigned screen_tvoc_level(unsigned previous, unsigned raw) {
  /* Stock 0x17cf4..0x17d62, including the floor(raw/2)<=254 boundary. */
  if (previous == 3)
    return raw <= 509 ? 1 : raw <= 1690 ? 2 : 3;
  if (previous == 2)
    return raw >= 1991 ? 3 : raw <= 509 ? 1 : 2;
  return raw >= 1991 ? 3 : raw >= 691 ? 2 : 1;
}
const char *screen_tvoc_text(unsigned level) {
  static const char *labels[] = {"--", "优", "良", "差"};
  return level <= 3 ? labels[level] : labels[0];
}
void screen_init(screen_model *m) {
  memset(m, 0, sizeof(*m));
  scene_defaults(&m->scene);
  appearance_default(&m->appearance);
}
void screen_event(screen_model *m, unsigned clicks) {
  if (m->asleep) {
    m->asleep = false;
    return;
  }
  if (clicks == 2)
    m->page = 0;
  else if (clicks == 1)
    for (unsigned i = 0; i < m->scene.pages; i++) {
      m->page = (m->page + 1) % m->scene.pages;
      if (m->scene.page[m->page].enabled)
        break;
    }
}
void screen_sample_set(screen_model *m, unsigned i, float v, bool valid,
                       uint32_t now) {
  if (i >= SCREEN_METRICS)
    return;
  if (valid && isfinite(v))
    m->present |= 1u << i;
  m->samples[i] = (screen_sample){v, now, valid && isfinite(v)};
  if (i == MET_TVOC && valid && isfinite(v) && v >= 0 && v <= 65535)
    m->tvoc_level = screen_tvoc_level(m->tvoc_level, (unsigned)v);
}
bool screen_sample_valid(const screen_model *m, unsigned i, uint32_t now) {
  return i < SCREEN_METRICS && m->samples[i].valid &&
         (uint32_t)(now - m->samples[i].at) < 20000;
}
static void append(screen_model *m, uint32_t minute, uint32_t now,
                   bool missing) {
  screen_point *p = &m->history[m->head];
  memset(p, 0, sizeof(*p));
  p->minute = minute;
  if (!missing)
    for (unsigned i = 0; i < SCREEN_METRICS; i++)
      if (screen_sample_valid(m, i, now)) {
        p->valid |= 1u << i;
        p->values[i] = m->samples[i].value;
      }
  m->head = (m->head + 1) % SCREEN_HISTORY;
  if (m->count < SCREEN_HISTORY)
    m->count++;
}
void screen_record(screen_model *m, uint32_t now) {
  if (!m->have_minute) {
    m->have_minute = true;
    m->last_minute = now / 60000;
    m->record_at = now - now % 60000;
    append(m, m->last_minute, now, false);
    return;
  }
  uint32_t steps = (uint32_t)(now - m->record_at) / 60000;
  if (!steps)
    return;
  /* Bound catch-up work and preserve missing intervals rather than joining two
   * readings across an absent sample window. Uptime wrap keeps cadence. */
  uint32_t first = steps > SCREEN_HISTORY ? steps - SCREEN_HISTORY + 1 : 1;
  for (uint32_t i = first; i <= steps; i++)
    append(m, m->last_minute + i, now, i != steps);
  m->last_minute += steps;
  m->record_at += steps * 60000;
}
