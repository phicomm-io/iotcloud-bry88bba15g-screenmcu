/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "board.h"
#include "jpeg_image.h"
#include <string.h>
static void reset_decoder(void) {
  SYS->PRSTEN = 0x55;
  SYS->PRSTR1 |= SYS_PRSTR1_JPEG_Msk;
  __DSB();
  SYS->PRSTR1 &= ~SYS_PRSTR1_JPEG_Msk;
  SYS->PRSTEN = 0;
  __DSB();
}
bool board_jpeg_decode(void *context, const uint8_t *p, size_t n,
                       const media_jpeg *j, uint16_t *out, unsigned stride) {
  (void)context;
  if (!p || !out || stride < j->width || stride > MEDIA_WIDTH_MAX ||
      stride * ((j->height + 15u) & ~15u) > MEDIA_WIDTH_MAX * 864u ||
      j->scan_offset >= n)
    return false;
  jfif_info_t info;
  memset(&info, 0, sizeof(info));
  info.Width = j->width;
  info.Height = j->height;
  info.CompCnt = 3;
  memcpy(info.QTable, j->qtable, sizeof(info.QTable));
  info.QTableCnt = 3;
  unsigned combinations[2] = {~0u, ~0u}, count = 0;
  for (unsigned i = 0; i < 3; i++) {
    unsigned key = j->dc[i] * 2 + j->ac[i], slot;
    for (slot = 0; slot < count; slot++)
      if (combinations[slot] == key)
        break;
    if (slot == count) {
      if (count == 2)
        return false;
      combinations[count++] = key;
    }
    info.CompInfo[i].id = i + 1;
    info.CompInfo[i].hfactor = i ? 1 : j->sampling >> 4;
    info.CompInfo[i].vfactor = i ? 1 : j->sampling & 15;
    info.CompInfo[i].qtab_id = j->quant[i];
    info.CompInfo[i].htab_id_dc = info.CompInfo[i].htab_id_ac = slot;
  }
  for (unsigned i = 0; i < 2; i++) {
    unsigned key = combinations[i < count ? i : 0];
    const media_huffman *dc = &j->huffman[key / 2][0],
                        *ac = &j->huffman[key % 2][1];
    memcpy(info.HTable[i].DC.codeWord, dc->code,
           sizeof(info.HTable[i].DC.codeWord));
    memcpy(info.HTable[i].DC.codeLen, dc->length,
           sizeof(info.HTable[i].DC.codeLen));
    memcpy(info.HTable[i].DC.codeVal, dc->value,
           sizeof(info.HTable[i].DC.codeVal));
    memcpy(info.HTable[i].AC.codeWord, ac->code,
           sizeof(info.HTable[i].AC.codeWord));
    memcpy(info.HTable[i].AC.codeLen, ac->length,
           sizeof(info.HTable[i].AC.codeLen));
    memcpy(info.HTable[i].AC.codeVal, ac->value,
           sizeof(info.HTable[i].AC.codeVal));
  }
  info.HTableCnt = 2;
  info.CodeAddr = (uint32_t)(p + j->scan_offset);
  info.CodeLen = j->scan_bytes;
  reset_decoder();
  JPEG_InitStructure init = {0};
  JPEG_Init(JPEG, &init);
  jpeg_outset_t setting = {
      .format = JPEG_OUT_RGB565, .RGBAddr = (uint32_t)out, .RGBWidth = stride};
  __DSB();
  JPEG_Decode(JPEG, &info, &setting);
  uint32_t start = board_ms;
  while (JPEG_DecodeBusy(JPEG)) {
    /* Do not recursively pump media input while its packet buffer is borrowed.
     * RX interrupt and 4 KiB ring cover this bounded hardware operation. */
    if ((uint32_t)(board_ms - start) > 100) {
      reset_decoder();
      return false;
    }
    board_watchdog_feed();
  }
  __DSB();
  bool ok =
      (JPEG->IR & JPEG_IR_IFDONE_Msk) && !(JPEG->IR & JPEG_IR_IFERROR_Msk);
  if (!ok)
    reset_decoder();
  return ok;
}
