/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mjpeg.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
void mjpeg_init(mjpeg_parser *m) {
  uint8_t *image = m->image;
  void *ctx = m->context;
  bool (*want)(void *) = m->want;
  void (*frame)(void *, size_t) = m->frame;
  memset(m, 0, sizeof(*m));
  m->image = image;
  m->context = ctx;
  m->want = want;
  m->frame = frame;
}
static bool decimal(const char *p, uint32_t *out) {
  uint32_t n = 0;
  if (!isdigit((unsigned char)*p))
    return false;
  while (isdigit((unsigned char)*p)) {
    unsigned d = *p++ - '0';
    if (n > (UINT32_MAX - d) / 10)
      return false;
    n = n * 10 + d;
  }
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p)
    return false;
  *out = n;
  return true;
}
static char *value(char *line, const char *name) {
  size_t n = strlen(name);
  if (strncasecmp(line, name, n) || line[n] != ':')
    return NULL;
  char *p = line + n + 1;
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}
static bool token(const char *s, const char *word) {
  size_t n = strlen(word);
  return !strncasecmp(s, word, n) && (!s[n] || s[n] == ';' || s[n] == ' ');
}
static void begin_frame(mjpeg_parser *m) {
  m->received = m->last = 0;
  m->capture = m->image && (!m->want || m->want(m->context));
  m->part_state = 2;
}
static void complete(mjpeg_parser *m) {
  if (m->capture && m->frame)
    m->frame(m->context, m->received);
  m->part_state = 0;
  m->received = 0;
  m->line_used = 0;
  m->part_header_bytes = 0;
  if (!m->multipart)
    m->ended = true;
}
static void header(mjpeg_parser *m) {
  char *v;
  if (!m->status) {
    unsigned status;
    char extra;
    if (sscanf(m->line, "HTTP/1.%c %u", &extra, &status) != 2 ||
        (extra != '0' && extra != '1') || status < 100 || status > 599) {
      m->error = 1;
      return;
    }
    m->status = status;
    return;
  }
  if (!m->line[0]) {
    if (m->status != 200) {
      m->error = m->status;
      return;
    }
    if (!m->multipart && !m->jpeg) {
      m->error = 2;
      return;
    }
    if (m->jpeg && m->length_known && m->content_length > MEDIA_JPEG_MAX) {
      m->error = 4;
      return;
    }
    m->state = 1;
    m->line_used = 0;
    if (m->jpeg)
      begin_frame(m);
    return;
  }
  if ((v = value(m->line, "Content-Type"))) {
    if (token(v, "multipart/x-mixed-replace")) {
      m->multipart = true;
      char *b = NULL;
      for (char *p = v; *p; p++)
        if (!strncasecmp(p, "boundary=", 9)) {
          b = p + 9;
          break;
        }
      if (!b) {
        m->error = 2;
        return;
      }
      char quote = *b == '"' ? *b++ : 0;
      unsigned n = 0;
      while (*b && *b != quote && *b != ';' && *b != ' ') {
        if (n >= sizeof(m->boundary) - 3) {
          m->error = 2;
          return;
        }
        m->boundary[n + 2] = *b++;
        n++;
      }
      if (!n) {
        m->error = 2;
        return;
      }
      m->boundary[0] = m->boundary[1] = '-';
      m->boundary[n + 2] = 0;
      /* Some cameras include the leading -- in the parameter itself. */
      if (n > 2 && m->boundary[2] == '-' && m->boundary[3] == '-')
        memmove(m->boundary, m->boundary + 2, n + 1);
    } else if (token(v, "image/jpeg"))
      m->jpeg = true;
  } else if ((v = value(m->line, "Transfer-Encoding"))) {
    if (strcasecmp(v, "chunked")) {
      m->error = 3;
      return;
    }
    m->chunked = true;
  } else if ((v = value(m->line, "WWW-Authenticate"))) {
    /* Prefer Digest if both schemes are offered. */
    if (!m->challenge[0] || !strncasecmp(v, "Digest ", 7))
      memcpy(m->challenge, v, strlen(v) + 1);
  } else if ((v = value(m->line, "Content-Length")) && !m->multipart) {
    if (!decimal(v, &m->content_length)) {
      m->error = 4;
      return;
    }
    m->length_known = true;
  }
}
static void part_line(mjpeg_parser *m) {
  char *v;
  if (m->part_state == 0) {
    if (!strcmp(m->line, m->boundary)) {
      m->part_state = 1;
      m->content_length = 0;
      m->length_known = false;
      m->jpeg = false;
      m->part_header_bytes = 0;
    } else if (!strncmp(m->line, m->boundary, strlen(m->boundary)) &&
               strstr(m->line + strlen(m->boundary), "--"))
      m->ended = true;
  } else if (m->part_state == 1) {
    if (!m->line[0]) {
      if (!m->jpeg) {
        m->error = 5;
        return;
      }
      begin_frame(m);
    } else if ((v = value(m->line, "Content-Length"))) {
      if (!decimal(v, &m->content_length) || m->content_length < 4 ||
          m->content_length > MEDIA_JPEG_MAX) {
        m->error = 4;
        return;
      }
      m->length_known = true;
    } else if ((v = value(m->line, "Content-Type")))
      m->jpeg = token(v, "image/jpeg");
  }
}
static void body(mjpeg_parser *m, unsigned c) {
  if (m->part_state == 2) {
    if (m->received >= MEDIA_JPEG_MAX) {
      m->error = 4;
      return;
    }
    if (m->received == 0 && c != 255) {
      m->error = 6;
      return;
    }
    if (m->received == 1 && c != 0xd8) {
      m->error = 6;
      return;
    }
    if (m->capture)
      m->image[m->received] = c;
    m->received++;
    bool eoi = m->last == 255 && c == 0xd9;
    m->last = c;
    if (m->length_known) {
      if (m->received == m->content_length) {
        if (!eoi) {
          m->error = 6;
          return;
        }
        complete(m);
      }
    } else if (eoi)
      complete(m);
    return;
  }
  if (++m->part_header_bytes > 2048) {
    m->error = 7;
    return;
  }
  if (c == '\r')
    return;
  if (c == '\n') {
    m->line[m->line_used] = 0;
    part_line(m);
    m->line_used = 0;
  } else if (m->line_used + 1 < sizeof(m->line))
    m->line[m->line_used++] = c;
  else
    m->error = 7;
}
void mjpeg_feed(mjpeg_parser *m, const uint8_t *p, size_t n) {
  while (n-- && !m->error && !m->ended) {
    unsigned c = *p++;
    if (!m->state) {
      if (++m->header_bytes > 8192) {
        m->error = 7;
        break;
      }
      if (c == '\r')
        continue;
      if (c == '\n') {
        m->line[m->line_used] = 0;
        header(m);
        m->line_used = 0;
      } else if (m->line_used + 1 < sizeof(m->line))
        m->line[m->line_used++] = c;
      else
        m->error = 7;
    } else if (!m->chunked)
      body(m, c);
    else
      switch (m->chunk_state) {
      case 0:
        if (c == '\r')
          break;
        if (c == '\n') {
          m->chunk_line[m->chunk_used] = 0;
          char *end;
          unsigned long bytes = strtoul(m->chunk_line, &end, 16);
          if (!m->chunk_used || end == m->chunk_line || (*end && *end != ';') ||
              bytes > 0x1000000ul) {
            m->error = 3;
            break;
          }
          m->chunk_left = bytes;
          m->chunk_used = 0;
          m->chunk_state = bytes ? 1 : 4;
        } else if (m->chunk_used < 63)
          m->chunk_line[m->chunk_used++] = c;
        else
          m->error = 3;
        break;
      case 1:
        body(m, c);
        if (!--m->chunk_left)
          m->chunk_state = 2;
        break;
      case 2:
        if (c != '\r')
          m->error = 3;
        else
          m->chunk_state = 3;
        break;
      case 3:
        if (c != '\n')
          m->error = 3;
        else
          m->chunk_state = 0;
        break;
      default:
        m->ended = true;
        break;
      }
  }
}
