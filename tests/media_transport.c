#include "media_wire.h"
#include <assert.h>
#include <string.h>
static unsigned calls;
static uint8_t expected[MEDIA_PACKET_MAX];
static void packet(void *ctx,unsigned ch,uint32_t session,uint32_t seq,const uint8_t *p,size_t n) {
  (void)ctx;assert(ch==MEDIA_COMMAND&&session==0x12340001&&seq==27);
  assert(n==sizeof(expected)&&!memcmp(p,expected,n));calls++;
}
/* Independent bitwise oracle checks the optimized CRC across empty input,
 * all byte values, unaligned starts and both UART and full RGB frame lengths. */
static uint32_t reference_crc(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (unsigned bit = 0; bit < 8; bit++)
      c = (c >> 1) ^ ((c & 1u) ? 0xedb88320u : 0u);
  }
  return ~c;
}
static void crc_vectors(void) {
  static uint8_t pixels[480u * 854u * 2u + 4u];
  assert(media_crc32(NULL, 0) == 0);
  assert(media_crc32("123456789", 9) == 0xcbf43926u);
  for (unsigned i = 0; i < sizeof(pixels); i++)
    pixels[i] = (uint8_t)(i * 73u + (i >> 8));
  const size_t lengths[] = {0, 1, 2, 3, 4, 17, 255, 256, 511, 512,
                            MEDIA_PACKET_MAX, 320u * 240u * 2u,
                            480u * 854u * 2u};
  for (unsigned offset = 0; offset < 4; offset++)
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
      assert(media_crc32(pixels + offset, lengths[i]) ==
             reference_crc(pixels + offset, lengths[i]));
}
int main(void) {
  crc_vectors();
  media_parser parser={.packet=packet};uint8_t wire[1400];
  for(unsigned i=0;i<sizeof(expected);i++)expected[i]=i;
  size_t n=media_encode(wire,sizeof(wire),MEDIA_COMMAND,0x12340001,27,expected,sizeof(expected));assert(n);
  assert(!media_feed(&parser,'a',0));
  for(unsigned round=0;round<4;round++)for(unsigned i=0;i<n;i++)assert(media_feed(&parser,wire[i],round));
  assert(calls==4);
  /* Invalid escape, CRC damage, truncation, and oversize never dispatch. */
  wire[n-3]^=1;for(unsigned i=0;i<n;i++)media_feed(&parser,wire[i],5);assert(calls==4&&parser.crc_errors==1);wire[n-3]^=1;
  uint8_t bad[]={0,0xdb,0xff,1,0};for(unsigned i=0;i<sizeof(bad);i++)media_feed(&parser,bad[i],6);assert(calls==4);
  media_feed(&parser,0,7);for(unsigned i=0;i<2000;i++)media_feed(&parser,1,7);media_feed(&parser,0,7);assert(calls==4);
  media_feed(&parser,0,8);media_feed(&parser,1,8);assert(!media_feed(&parser,'x',1010));
  for(unsigned i=0;i<n-1;i++)media_feed(&parser,wire[i],1011); /* Lost ending. */
  for(unsigned j=0;j<3;j++)for(unsigned i=0;i<n;i++)media_feed(&parser,wire[i],1012+j);
  assert(calls>=6&&parser.framing_errors>=3);
  assert(!media_encode(wire,4,0,1,1,expected,640));
  assert(!media_encode(wire,sizeof(wire),0,0,1,expected,1));
}
