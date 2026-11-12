/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stream.h"
#include "crypto/md5.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
screen_stream stream;
static bool decode_ready(screen_stream *s);
static bool emit(screen_stream *s, unsigned op, const void *p, size_t n) {
  uint8_t wire[MEDIA_PACKET_MAX];
  if (n > sizeof(wire) - 5 || !s->send)
    return false;
  wire[0] = op;
  media_put32(wire + 1, s->connection);
  if (n)
    memcpy(wire + 5, p, n);
  return s->send(s->context, wire, n + 5);
}
static bool want(void *ctx) {
  screen_stream *s = ctx;
  if (s->state == STREAM_BACKOFF)
    return false;
  // A TCP/UART block can contain the end of one JPEG and the next header.
  // Finish the complete image before the parser reuses its single JPEG arena.
  if (s->ready && !decode_ready(s))
    return false;
  uint32_t period = 1000u / s->fps;
  // TCP buffering and the shared UART add variable delivery latency. Keep
  // the deadline cadence while allowing a quarter-period of arrival jitter.
  unsigned tolerance = period / 4u;
  bool yes = !s->ready && (!s->frame_due ||
                 (int32_t)(s->now - s->frame_due) >= -(int32_t)tolerance);
  if (yes) {
    // Pace frame starts. Transfer/decode time must not halve the source rate.
    // Follow a deadline so small source jitter does not drop alternate frames.
    if (!s->frame_due || (int32_t)(s->now - s->frame_due) > (int32_t)period)
      s->frame_due = s->now + period;
    else
      s->frame_due += period;
  } else
    s->dropped++;
  return yes;
}
static void frame(void *ctx, size_t n) {
  screen_stream *s = ctx;
  s->ready = true;
  s->frame_bytes = n;
}
void stream_init(screen_stream *s) {
  memset(s, 0, sizeof(*s));
  s->target_width = 320;
  s->target_height = 240;
  s->fps = 1;
  s->verify_tls = true;
  s->parser.image = s->jpeg;
  s->parser.context = s;
  s->parser.want = want;
  s->parser.frame = frame;
}
static void close_stream(screen_stream *s) {
  if (s->state == STREAM_CONNECTING || s->state == STREAM_READING)
    emit(s, MEDIA_NET_CLOSE, NULL, 0);
  s->request_size = 0;
  s->ready = false;
  s->frame_due = 0;
}
static void failure(screen_stream *s, unsigned error) {
  close_stream(s);
  s->error = error;
  s->state = STREAM_BACKOFF;
  unsigned delay = 1000u << (s->failures < 5 ? s->failures : 5);
  if (delay > 30000)
    delay = 30000;
  s->failures++;
  s->retry_at = s->now + delay;
  s->revision++;
}
static bool decode_ready(screen_stream *s) {
  media_jpeg j;
  if (!media_jpeg_parse(&j, s->jpeg, s->frame_bytes) ||
      j.width > s->target_width || j.height > s->target_height) {
    failure(s, 820);
    return false;
  }
  unsigned stride = (j.width + 15u) & ~15u;
  if (!s->decode || !s->decode(s->context, s->jpeg, s->frame_bytes, &j,
                             s->pixels, stride)) {
    failure(s, 821);
    return false;
  }
  for (unsigned row = 1; row < j.height; row++)
    memmove(s->pixels + row * j.width, s->pixels + row * stride,
            j.width * 2u);
  s->width = j.width;
  s->height = j.height;
  s->pixel_crc = media_crc32(s->pixels, j.width * j.height * 2u);
  s->frames++;
  s->period_frames++;
  s->frame_at = s->now;
  s->ready = false;
  s->failures = 0;
  s->error = 0;
  s->revision++;
  if (!s->period_start)
    s->period_start = s->now;
  if ((uint32_t)(s->now - s->period_start) >= 5000) {
    s->actual_millifps =
        s->period_frames * 1000000u / (s->now - s->period_start);
    s->period_frames = 0;
    s->period_start = s->now;
  }
  return true;
}
static bool expand(char *out, size_t cap, const char *in, unsigned w,
                   unsigned h, unsigned fps) {
  size_t used = 0;
  while (*in) {
    unsigned value = 0, take = 0;
    if (!strncmp(in, "{width}", 7)) {
      value = w;
      take = 7;
    } else if (!strncmp(in, "{height}", 8)) {
      value = h;
      take = 8;
    } else if (!strncmp(in, "{fps}", 5)) {
      value = fps;
      take = 5;
    }
    if (take) {
      char n[12];
      int count = snprintf(n, sizeof(n), "%u", value);
      if (used + (unsigned)count >= cap)
        return false;
      memcpy(out + used, n, count);
      used += count;
      in += take;
    } else {
      if ((unsigned char)*in <= 32 || (unsigned char)*in == 127 || *in == '#' ||
          used + 1 >= cap)
        return false;
      out[used++] = *in++;
    }
  }
  out[used] = 0;
  return true;
}
typedef struct {
  bool tls;
  unsigned port;
  char host[128], path[512];
} stream_url;
static bool parse_url(stream_url *s, const char *source, unsigned w, unsigned h,
                      unsigned fps) {
  char expanded[512];
  if (!expand(expanded, sizeof(expanded), source, w, h, fps))
    return false;
  const char *p = expanded;
  if (!strncmp(p, "http://", 7)) {
    s->tls = false;
    p += 7;
    s->port = 80;
  } else if (!strncmp(p, "https://", 8)) {
    s->tls = true;
    p += 8;
    s->port = 443;
  } else
    return false;
  unsigned n = 0;
  while (*p && *p != '/' && *p != ':' && *p != '?') {
    if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-')
      return false;
    if (n + 1 >= sizeof(s->host))
      return false;
    s->host[n++] = *p++;
  }
  s->host[n] = 0;
  if (!n)
    return false;
  if (*p == ':') {
    unsigned port = 0;
    p++;
    if (!isdigit((unsigned char)*p))
      return false;
    while (isdigit((unsigned char)*p)) {
      port = port * 10 + *p++ - '0';
      if (port > 65535)
        return false;
    }
    if (!port)
      return false;
    s->port = port;
  }
  if (*p && *p != '/' && *p != '?')
    return false;
  int size =
      snprintf(s->path, sizeof(s->path), "%s%s", *p == '/' ? "" : "/", p);
  return size > 0 && (unsigned)size < sizeof(s->path);
}
unsigned stream_config(screen_stream *s, const uint8_t *p, size_t n) {
  if (n != MEDIA_STREAM_CONFIG_BYTES || p[0] != MEDIA_STREAM_CONFIG ||
      p[6] > 3 || !memchr(p + 7, 0, 384) || !memchr(p + 391, 0, 64) ||
      !memchr(p + 455, 0, 64))
    return MEDIA_RANGE;
  unsigned w = (unsigned)p[1] | (unsigned)p[2] << 8,
           h = (unsigned)p[3] | (unsigned)p[4] << 8, fps = p[5];
  if (!w || w > MEDIA_WIDTH_MAX || !h || h > MEDIA_HEIGHT_MAX || !fps ||
      fps > 10)
    return MEDIA_RANGE;
  stream_url candidate = {0};
  bool clear = !p[7] && !(p[6] & 1);
  if (!clear && !parse_url(&candidate, (const char *)p + 7, w, h, fps))
    return MEDIA_RANGE;
  for (unsigned i = 391; i < 519; i++)
    if (p[i] && (p[i] < 32 || p[i] == 127))
      return MEDIA_RANGE;
  close_stream(s);
  memcpy(s->url, p + 7, 384);
  memcpy(s->username, p + 391, 64);
  memcpy(s->password, p + 455, 64);
  if (clear) {
    memset(s->username, 0, sizeof(s->username));
    memset(s->password, 0, sizeof(s->password));
  }
  s->target_width = w;
  s->target_height = h;
  s->fps = fps;
  s->enabled = p[6] & 1;
  s->verify_tls = p[6] & 2;
  s->tls = candidate.tls;
  s->port = candidate.port;
  memcpy(s->host, candidate.host, sizeof(s->host));
  memcpy(s->path, candidate.path, sizeof(s->path));
  s->state = s->enabled ? STREAM_PAUSED : STREAM_OFF;
  s->failures = s->auth_attempt = 0;
  s->digest = false;
  s->challenge[0] = 0;
  s->error = 0;
  s->revision++;
  return MEDIA_OK;
}
static void md5(const char *in, char out[33]) {
  screen_md5 m;
  uint8_t digest[16];
  screen_md5_init(&m);
  screen_md5_update(&m, in, strlen(in));
  screen_md5_final(&m, digest);
  static const char hex[] = "0123456789abcdef";
  for (unsigned i = 0; i < 16; i++) {
    out[2 * i] = hex[digest[i] >> 4];
    out[2 * i + 1] = hex[digest[i] & 15];
  }
  out[32] = 0;
}
static bool parameter(const char *s, const char *key, char *out, size_t cap) {
  while (*s) {
    while (*s == ' ' || *s == ',')
      s++;
    const char *start = s;
    while (*s && *s != '=' && *s != ',' && *s != ' ')
      s++;
    size_t n = s - start;
    while (*s == ' ')
      s++;
    if (*s != '=')
      return false;
    s++;
    while (*s == ' ')
      s++;
    bool quote = *s == '"';
    if (quote)
      s++;
    size_t used = 0;
    bool match = n == strlen(key) && !strncasecmp(start, key, n);
    while (*s && (quote ? *s != '"' : *s != ',' && *s != ' ')) {
      char c = *s++;
      if (c == '\\' && quote && *s)
        c = *s++;
      if (match) {
        if (used + 1 >= cap)
          return false;
        out[used++] = c;
      }
    }
    if (quote) {
      if (*s != '"')
        return false;
      s++;
    }
    if (match) {
      out[used] = 0;
      return true;
    }
    while (*s && *s != ',')
      s++;
  }
  return false;
}
static bool authorization(screen_stream *s, char *out, size_t cap) {
  out[0] = 0;
  if (!s->username[0])
    return true;
  if (!s->digest) {
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char plain[130], encoded[180];
    int n = snprintf(plain, sizeof(plain), "%s:%s", s->username, s->password);
    unsigned used = 0;
    for (int i = 0; i < n; i += 3) {
      unsigned left = n - i;
      uint32_t v = (uint8_t)plain[i] << 16;
      if (left > 1)
        v |= (uint8_t)plain[i + 1] << 8;
      if (left > 2)
        v |= (uint8_t)plain[i + 2];
      encoded[used++] = b64[v >> 18];
      encoded[used++] = b64[(v >> 12) & 63];
      encoded[used++] = left > 1 ? b64[(v >> 6) & 63] : '=';
      encoded[used++] = left > 2 ? b64[v & 63] : '=';
    }
    encoded[used] = 0;
    return snprintf(out, cap, "Authorization: Basic %s\r\n", encoded) <
           (int)cap;
  }
  char realm[128], nonce[160], qop[96] = "", opaque[128] = "",
                               algorithm[24] = "MD5";
  const char *challenge = s->challenge + 7;
  if (!parameter(challenge, "realm", realm, sizeof(realm)) ||
      !parameter(challenge, "nonce", nonce, sizeof(nonce)))
    return false;
  parameter(challenge, "qop", qop, sizeof(qop));
  parameter(challenge, "opaque", opaque, sizeof(opaque));
  parameter(challenge, "algorithm", algorithm, sizeof(algorithm));
  if (strcasecmp(algorithm, "MD5"))
    return false;
  bool auth = false;
  if (qop[0]) {
    char *p = qop;
    while (*p) {
      while (*p == ' ' || *p == ',')
        p++;
      char *end = p;
      while (*end && *end != ',')
        end++;
      char *trim = end;
      while (trim > p && trim[-1] == ' ')
        trim--;
      if (trim - p == 4 && !strncasecmp(p, "auth", 4))
        auth = true;
      p = *end ? end + 1 : end;
    }
    if (!auth)
      return false;
  }
  /* Header strings originate at the source; reject quotes/control injection. */
  const char *fields[] = {realm, nonce, opaque, s->username, s->path};
  for (unsigned i = 0; i < 5; i++)
    for (const char *p = fields[i]; *p; p++)
      if ((unsigned char)*p < 32 || *p == '"' || *p == '\\')
        return false;
  char tmp[768], ha1[33], ha2[33], response[33], cnonce[17];
  snprintf(cnonce, sizeof(cnonce), "%08lx%08lx", (unsigned long)s->connection,
           (unsigned long)s->now);
  snprintf(tmp, sizeof(tmp), "%s:%s:%s", s->username, realm, s->password);
  md5(tmp, ha1);
  snprintf(tmp, sizeof(tmp), "GET:%s", s->path);
  md5(tmp, ha2);
  if (auth)
    snprintf(tmp, sizeof(tmp), "%s:%s:00000001:%s:auth:%s", ha1, nonce, cnonce,
             ha2);
  else
    snprintf(tmp, sizeof(tmp), "%s:%s:%s", ha1, nonce, ha2);
  md5(tmp, response);
  int n = snprintf(out, cap,
                   "Authorization: Digest username=\"%s\", realm=\"%s\", "
                   "nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5",
                   s->username, realm, nonce, s->path, response);
  if (n < 0 || (unsigned)n >= cap)
    return false;
  if (auth) {
    int add = snprintf(out + n, cap - n,
                       ", qop=auth, nc=00000001, cnonce=\"%s\"", cnonce);
    if (add < 0 || (unsigned)add >= cap - n)
      return false;
    n += add;
  }
  if (opaque[0]) {
    int add = snprintf(out + n, cap - n, ", opaque=\"%s\"", opaque);
    if (add < 0 || (unsigned)add >= cap - n)
      return false;
    n += add;
  }
  if ((unsigned)n + 3 > cap)
    return false;
  memcpy(out + n, "\r\n", 3);
  return true;
}
static bool request(screen_stream *s) {
  char auth[1100];
  if (!authorization(s, auth, sizeof(auth)))
    return false;
  int n = snprintf(
      s->request, sizeof(s->request),
      "GET %s HTTP/1.1\r\nHost: %s:%u\r\nAccept: multipart/x-mixed-replace, "
      "image/jpeg\r\nConnection: close\r\n%s\r\n",
      s->path, s->host, s->port, auth);
  if (n <= 0 || (unsigned)n >= sizeof(s->request))
    return false;
  s->request_size = n;
  s->request_at = 0;
  return true;
}
void stream_socket(screen_stream *s, uint32_t sequence, const uint8_t *p,
                   size_t n) {
  if (n < 5 || media_u32(p + 1) != s->connection)
    return;
  if (p[0] == MEDIA_NET_CONNECTED) {
    if (s->state != STREAM_CONNECTING)
      return;
    mjpeg_init(&s->parser);
    s->rx_sequence = 0;
    if (!request(s)) {
      failure(s, 401);
      return;
    }
    s->state = STREAM_READING;
    s->at = s->now;
    return;
  }
  if (p[0] == MEDIA_NET_CLOSED && s->ready) {
    s->parser.ended = true;
    return;
  }
  if (p[0] == MEDIA_NET_CLOSED || p[0] == MEDIA_NET_ERROR) {
    if (s->state == STREAM_CONNECTING || s->state == STREAM_READING)
      failure(s, 800 + p[0]);
    return;
  }
  if (p[0] != MEDIA_NET_WRITE || s->state != STREAM_READING ||
      sequence <= s->rx_sequence)
    return;
  if (sequence != s->rx_sequence + 1) {
    failure(s, 810);
    return;
  }
  s->rx_sequence = sequence;
  s->at = s->now;
  s->bytes += n - 5;
  mjpeg_feed(&s->parser, p + 5, n - 5);
}
void stream_poll(screen_stream *s, uint32_t now, bool visible,
                 bool link_ready) {
  s->now = now;
  if (!s->enabled || !visible || !link_ready) {
    close_stream(s);
    unsigned state = s->enabled ? STREAM_PAUSED : STREAM_OFF;
    if (s->state != state) {
      s->state = state;
      s->revision++;
    }
    return;
  }
  if (s->ready && !decode_ready(s))
    return;
  if (s->state == STREAM_READING && s->parser.error) {
    if (s->parser.status == 401 && s->username[0] && s->auth_attempt++ < 2 &&
        !strncasecmp(s->parser.challenge, "Digest ", 7)) {
      memcpy(s->challenge, s->parser.challenge, sizeof(s->challenge));
      s->digest = true;
      close_stream(s);
      s->state = STREAM_PAUSED;
    } else {
      failure(s, s->parser.error);
      return;
    }
  }
  if (s->state == STREAM_READING && s->parser.ended) {
    close_stream(s);
    s->state = STREAM_BACKOFF;
    s->retry_at = s->frame_at + 1000u / s->fps;
  }
  if ((s->state == STREAM_CONNECTING || s->state == STREAM_READING) &&
      (uint32_t)(now - s->at) > 15000) {
    failure(s, 822);
    return;
  }
  if (s->state == STREAM_READING && s->request_at < s->request_size) {
    unsigned n = s->request_size - s->request_at;
    if (n > MEDIA_CHUNK)
      n = MEDIA_CHUNK;
    if (emit(s, MEDIA_NET_WRITE, s->request + s->request_at, n))
      s->request_at += n;
  }
  if (s->state == STREAM_PAUSED ||
      (s->state == STREAM_BACKOFF && (int32_t)(now - s->retry_at) >= 0)) {
    s->connection++;
    if (!s->connection)
      s->connection = 1;
    uint8_t open[134] = {s->tls, s->verify_tls, s->port, s->port >> 8};
    size_t len = strlen(s->host);
    memcpy(open + 4, s->host, len + 1);
    if (emit(s, MEDIA_NET_OPEN, open, len + 5)) {
      s->state = STREAM_CONNECTING;
      s->at = now;
      s->revision++;
    }
  }
}
size_t stream_status(const screen_stream *s, uint8_t *p, size_t cap) {
  if (cap < 40)
    return 0;
  memset(p, 0, 40);
  p[0] = s->state;
  p[1] = s->enabled;
  p[2] = s->width;
  p[3] = s->width >> 8;
  p[4] = s->height;
  p[5] = s->height >> 8;
  media_put32(p + 8, s->frames);
  media_put32(p + 12, s->dropped);
  media_put32(p + 16, s->bytes);
  media_put32(p + 20, s->pixel_crc);
  media_put32(p + 24, s->error);
  media_put32(p + 28, s->actual_millifps);
  media_put32(p + 32, s->revision);
  media_put32(p + 36, s->connection);
  return 40;
}
