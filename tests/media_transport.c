#include "media_wire.h"
#include <assert.h>
#include <string.h>
static unsigned calls;
static uint8_t expected[MEDIA_PACKET_MAX];
static void packet(void *ctx,unsigned ch,uint32_t session,uint32_t seq,const uint8_t *p,size_t n) {
  (void)ctx;assert(ch==MEDIA_COMMAND&&session==0x12340001&&seq==27);
  assert(n==sizeof(expected)&&!memcmp(p,expected,n));calls++;
}
int main(void) {
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
