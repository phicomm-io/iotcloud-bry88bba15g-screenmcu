#include "maintenance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool send(screen_maintenance *m, const char *s) {
  return m->send && m->send(m->context, s, strlen(s));
}
static void rearm(screen_maintenance *m, uint32_t fallback) {
  m->deadline = (m->clock ? m->clock(m->context) : fallback) + 30000;
}
static void abort_transfer(screen_maintenance *m) {
  bool active = m->state != 0;
  screen_update_abort(&m->update);
  m->receiving = false;
  m->staged = false;
  m->state = 0;
  m->block_at = m->block_size = m->prefix_at = 0;
  m->used = 0;
  if (active && m->activity) m->activity(m->context, false);
  /* Release the ESP UART owner on bounded NOR failures as well as timeouts. */
  if (active) send(m, "cancel_ota\r\n");
}
void maintenance_init(screen_maintenance *m) {
  if (m->state && m->activity) m->activity(m->context, false);
  m->state = 0;
  m->staged = false;
  m->receiving = false;
  m->used = 0;
  m->discard = false;
  m->block_at = m->block_size = m->prefix_at = 0;
}
bool maintenance_query(screen_maintenance *m, uint32_t now) {
  if (m->state)
    return false;
  m->state = 1;
  if (m->activity) m->activity(m->context, true);
  m->staged = false;
  m->deadline = now + 30000;
  char query[128];
  snprintf(query, sizeof(query), "bin_summary 1 %s\r\n",
           m->version ? m->version : "iotcloud-dev-dirty");
  if (!send(m, query)) {
    abort_transfer(m);
    return false;
  }
  return true;
}
static bool digest(const char *s, uint8_t out[16]) {
  if (strlen(s) != 32)
    return false;
  for (unsigned i = 0; i < 16; i++) {
    unsigned a, b;
    char x = s[2 * i], y = s[2 * i + 1];
    a = x >= '0' && x <= '9'   ? x - '0'
        : x >= 'a' && x <= 'f' ? x - 'a' + 10
        : x >= 'A' && x <= 'F' ? x - 'A' + 10
                               : 99;
    b = y >= '0' && y <= '9'   ? y - '0'
        : y >= 'a' && y <= 'f' ? y - 'a' + 10
        : y >= 'A' && y <= 'F' ? y - 'A' + 10
                               : 99;
    if (a > 15 || b > 15)
      return false;
    out[i] = (a << 4) | b;
  }
  return true;
}
static bool ota_line(screen_maintenance *m, const char *s, uint32_t now) {
  if (!m->state)
    return false;
  m->deadline = now + 30000;
  if (m->state == 1 && !strncmp(s, "+bin_summary:", 13)) {
    const char *comma = strchr(s + 13, ',');
    uint8_t md5[16];
    if (!comma || !digest(comma + 1, md5) ||
        !screen_update_begin(&m->update, SCREEN_APP_LIMIT, md5)) {
      abort_transfer(m);
      return true;
    }
    m->state = 2;
    rearm(m, now);
    if (!send(m, "start_ota 1 20\r\n"))
      abort_transfer(m);
    return true;
  }
  if (m->state == 2 && !strcmp(s, "+start_ota:OK")) {
    m->state = 3;
    if (!send(m, "download_bin 1024\r\n"))
      abort_transfer(m);
    return true;
  }
  if (m->state == 3 && !strcmp(s, "+download_bin:END")) {
    uint32_t n = m->update.transfer.received;
    if (n < 64 || n % 16) {
      abort_transfer(m);
      return true;
    }
    m->update.transfer.size = n;
    m->staged = screen_update_finish(&m->update);
    if (!m->staged) {
      abort_transfer(m);
      return true;
    }
    m->state = 5;
    rearm(m, now);
    if (!send(m, "end_ota\r\n"))
      abort_transfer(m);
    return true;
  }
  if (m->state == 3 && !strncmp(s, "+download_bin:", 14)) {
    unsigned offset, size;
    char extra;
    if (sscanf(s + 14, "%u,%u%c", &offset, &size, &extra) != 2 ||
        offset != m->update.transfer.received || !size || size > 1024 ||
        size > SCREEN_APP_LIMIT - offset) {
      abort_transfer(m);
      return true;
    }
    m->offset = offset;
    m->block_size = size;
    m->block_at = m->prefix_at = 0;
    m->receiving = true;
    m->state = 4;
    if (!send(m, ">\r\n"))
      abort_transfer(m);
    return true;
  }
  if (m->state == 5 && !strncmp(s, "+end_ota:", 9)) {
    uint8_t md5[16];
    if (!digest(s + 9, md5) || memcmp(md5, m->update.expected, 16)) {
      abort_transfer(m);
      m->staged = false;
      return true;
    }
    /* Staging and activation are independent. The checked guard command
     * commits only after identifying a supported loader and a second complete
     * NOR rehash. Boot exports are independent of activation. */
    m->state = 0;
    if (m->activity) m->activity(m->context, false);
    return true;
  }
  if (strstr(s, ":ERROR")) {
    abort_transfer(m);
    return true;
  }
  return false;
}
void maintenance_feed(screen_maintenance *m, uint8_t c, uint32_t now) {
  if (m->receiving) {
    static const char prefix[] = "+bin:";
    if (m->prefix_at < 5) {
      if (c != (uint8_t)prefix[m->prefix_at++])
        abort_transfer(m);
      return;
    }
    m->block[m->block_at++] = c;
    if (m->block_at == m->block_size) {
      m->receiving = false;
      if (!screen_update_chunk(&m->update, m->offset, m->block,
                               m->block_size)) {
        abort_transfer(m);
        return;
      }
      m->state = 3;
      rearm(m, now);
      if (!send(m, "download_bin 1024\r\n"))
        abort_transfer(m);
    }
    return;
  }
  if (m->discard) {
    if (c == '\n') {
      m->discard = false;
      m->used = 0;
    }
    return;
  }
  if (m->used >= sizeof(m->text) - 1) {
    m->used = 0;
    m->discard = true;
    return;
  }
  m->text[m->used++] = (char)c;
  if (c == '\n' && m->used >= 2 && m->text[m->used - 2] == '\r') {
    size_t n = m->used - 2;
    m->text[n] = 0;
    if (n && !ota_line(m, m->text, now) && m->line)
      m->line(m->context, m->text, n);
    m->used = 0;
  }
}
void maintenance_tick(screen_maintenance *m, uint32_t now) {
  if (m->state && (int32_t)(now - m->deadline) >= 0)
    abort_transfer(m);
}
