/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "jpeg_image.h"
#include <string.h>
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] * 256 + p[1]; }
bool media_jpeg_parse(media_jpeg *j, const uint8_t *p, size_t n) {
  if (!j || !p || n < 4 || n > MEDIA_JPEG_MAX || p[0] != 255 || p[1] != 0xd8 ||
      p[n - 2] != 255 || p[n - 1] != 0xd9)
    return false;
  memset(j, 0, sizeof(*j));
  size_t at = 2;
  bool sof = false;
  while (at + 4 <= n) {
    if (p[at++] != 255)
      return false;
    while (at < n && p[at] == 255)
      at++;
    if (at + 3 > n)
      return false;
    unsigned marker = p[at++], len = be16(p + at);
    if (len < 2 || len > n - at)
      return false;
    const uint8_t *v = p + at + 2;
    size_t bytes = len - 2;
    at += len;
    if (marker == 0xdb) {
      while (bytes) {
        if (bytes < 65 || v[0] > 2 || (j->qmask & (1u << v[0])))
          return false;
        unsigned id = *v++;
        for (unsigned k = 0; k < 64; k++)
          if (!v[k])
            return false;
        memcpy(j->qtable[id], v, 64);
        j->qmask |= 1u << id;
        v += 64;
        bytes -= 65;
      }
    } else if (marker == 0xc0) {
      if (sof || bytes != 15 || v[0] != 8 || v[5] != 3)
        return false;
      j->height = be16(v + 1);
      j->width = be16(v + 3);
      if (!j->width || !j->height || j->width > MEDIA_WIDTH_MAX ||
          j->height > MEDIA_HEIGHT_MAX)
        return false;
      j->sampling = v[7];
      if (j->sampling != 0x11 && j->sampling != 0x21 && j->sampling != 0x22)
        return false;
      for (unsigned i = 0; i < 3; i++) {
        if (v[6 + 3 * i] != i + 1 || (i && v[7 + 3 * i] != 0x11) ||
            v[8 + 3 * i] > 2)
          return false;
        j->quant[i] = v[8 + 3 * i];
      }
      sof = true;
    } else if (marker == 0xc4) {
      while (bytes) {
        if (bytes < 17)
          return false;
        unsigned kind = v[0] >> 4, id = v[0] & 15;
        if (kind > 1 || id > 1 || (j->hmask & (1u << (kind * 2 + id))))
          return false;
        unsigned count = 0;
        for (unsigned i = 1; i <= 16; i++)
          count += v[i];
        if (!count || count > (kind ? 162 : 12) || bytes < 17 + count)
          return false;
        media_huffman *h = &j->huffman[id][kind];
        unsigned index = 0, code = 0;
        bool seen[256] = {false};
        for (unsigned length = 1; length <= 16; length++) {
          if (code + v[length] > (1u << length))
            return false;
          for (unsigned k = 0; k < v[length]; k++, index++, code++) {
            unsigned value = v[17 + index];
            if (seen[value] || (!kind && value > 11) ||
                (kind && value != 0 && value != 0xf0 &&
                 (!(value & 15) || (value & 15) > 10)))
              return false;
            seen[value] = true;
            h->code[index] = code;
            h->length[index] = length;
            h->value[index] = value;
          }
          code <<= 1;
        }
        j->hmask |= 1u << (kind * 2 + id);
        v += 17 + count;
        bytes -= 17 + count;
      }
    } else if (marker == 0xda) {
      if (!sof || bytes != 10 || v[0] != 3 || v[7] != 0 || v[8] != 63 ||
          v[9] != 0)
        return false;
      for (unsigned i = 0; i < 3; i++) {
        if (v[1 + 2 * i] != i + 1)
          return false;
        j->dc[i] = v[2 + 2 * i] >> 4;
        j->ac[i] = v[2 + 2 * i] & 15;
        if (j->dc[i] > 1 || j->ac[i] > 1 || !(j->qmask & (1u << j->quant[i])) ||
            !(j->hmask & (1u << j->dc[i])) ||
            !(j->hmask & (1u << (2 + j->ac[i]))))
          return false;
      }
      if (at >= n - 2)
        return false;
      /* A single complete scan. Restart, DNL and subsequent scans would need
       * different hardware configuration and are not accepted implicitly. */
      for (size_t k = at; k < n - 2; k++)
        if (p[k] == 255) {
          if (++k >= n - 2 || p[k] != 0)
            return false;
        }
      j->scan_offset = at;
      j->scan_bytes = n - at;
      return true;
    } else if ((marker >= 0xe0 && marker <= 0xef) || marker == 0xfe) {
      /* Bounded metadata. */
    } else
      return false;
  }
  return false;
}
