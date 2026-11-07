#include "resources.h"
#include "scene.h"
#include <string.h>
screen_resources screen_font_resources;
static uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static bool fail(screen_resources *r, unsigned reason) {
  r->state = reason; r->bytes = r->crc = r->count = r->version = 0; return false;
}
/* CRC-32/IEEE, two table lookups per byte. Incremental work stays inside the
 * bounded read slice instead of blocking UART/UI on two whole-font scans. */
static uint32_t crc_update(uint32_t crc,const uint8_t *p,size_t n) {
  static const uint32_t table[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,
    0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,
    0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
  while(n--){crc^=*p++;crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];}
  return crc;
}
bool resources_header(const uint8_t *p,size_t capacity) {
  bool old=!memcmp(p,"IOTFNT01",8),modern=!memcmp(p,"IOTFNT02",8);
  unsigned count=u16(p+16);uint32_t bytes=scene_u32(p+8);
  return (old||modern)&&count&&count<=(old?1024:8192)&&u16(p+18)==24&&
    p[20]==36&&p[21]==2&&(old?(!u16(p+22)&&!scene_u32(p+28)):
      (p[22]==2&&p[23]==32&&scene_u32(p+28)==scene_crc32(p,28)))&&
    !scene_u32(p+24)&&bytes>=32+count*24&&bytes<=capacity&&
    bytes<=(old?0x20000u:SCREEN_RESOURCE_LIMIT);
}
static bool validate(screen_resources *r,size_t n,bool crc_checked) {
  if(!r||!r->cache||n<32||!resources_header(r->cache,r->capacity))return r?fail(r,RESOURCE_INVALID):false;
  const uint8_t *p=r->cache;uint32_t bytes=scene_u32(p+8),crc=scene_u32(p+12);
  unsigned count=u16(p+16),bpp=p[7]=='2'?2:1;
  if(n!=bytes||(!crc_checked&&~crc_update(~0u,p+32,bytes-32)!=crc))return fail(r,RESOURCE_INVALID);
  uint32_t previous=0,end=32+count*24;
  for(unsigned i=0;i<count;i++) {
    const uint8_t *e=p+32+i*24;uint32_t cp=scene_u32(e),at=scene_u32(e+4);
    unsigned size=u16(e+8),w=u16(e+12),h=u16(e+14);
    int16_t x=(int16_t)u16(e+16),y=(int16_t)u16(e+18);
    if(cp<=previous||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||w>64||h>64||
       u16(e+10)>96||x< -64||x>64||y< -64||y>64||scene_u32(e+20)||
       size!=(w*h*bpp+7)/8||at!=end||at>bytes||size>bytes-at)return fail(r,RESOURCE_INVALID);
    end=at+size;previous=cp;
  }
  if(end!=bytes)return fail(r,RESOURCE_INVALID);
  r->bytes=bytes;r->crc=crc;r->count=count;r->version=p[7]-'0';r->bpp=bpp;r->pixels=32;r->state=RESOURCE_READY;
  return true;
}
bool resources_validate(screen_resources *r,size_t n) {return validate(r,n,false);}
bool resources_bank(const uint8_t *h,uint32_t *generation,uint32_t *bytes,uint32_t *crc) {
  if(memcmp(h,"IOTFBK02",8)||!scene_u32(h+8)||scene_u32(h+12)<32||
     scene_u32(h+12)>SCREEN_RESOURCE_LIMIT||scene_u32(h+20)||scene_u32(h+24)||
     scene_u32(h+28)!=scene_crc32(h,28))return false;
  *generation=scene_u32(h+8);*bytes=scene_u32(h+12);*crc=scene_u32(h+16);return true;
}
static bool begin_at(screen_resources *r,uint32_t at) {
  r->started=r->now?r->now(r->context):0;
  if(!r->read(r->context,at,r->cache,32))return fail(r,RESOURCE_IO);
  if(memcmp(r->cache,"IOTFNT01",8)&&memcmp(r->cache,"IOTFNT02",8))return fail(r,RESOURCE_ABSENT);
  if(!resources_header(r->cache,r->capacity)||
     (r->expected_bytes&&r->expected_bytes!=scene_u32(r->cache+8)))return fail(r,RESOURCE_INVALID);
  r->base=at;r->bytes=scene_u32(r->cache+8);r->received=32;r->elapsed_ms=0;
  r->whole_crc=crc_update(~0u,r->cache,32);r->payload_crc=~0u;
  r->state=RESOURCE_LOADING;return true;
}
void resources_start(screen_resources *r) {
  if(!r||r->state)return; /* failures latch until explicit upload/reboot */
  if(!r->read||!r->cache||r->capacity<32){fail(r,RESOURCE_IO);return;}
  uint32_t base=SCREEN_RESOURCE_BASE,other=0,generation=0,expected=0,bytes=0;
  uint32_t other_generation=0,other_expected=0,other_bytes=0;
  const uint8_t owner[16]="IOTFONTBANK02";
  for(unsigned i=0;i<2;i++) {
    uint32_t bank=SCREEN_FONT_BASE+i*SCREEN_FONT_BANK,g,n,c;uint8_t p[32];
    if(!r->read(r->context,bank,p,20)||memcmp(p,owner,16)||scene_u32(p+16)!=scene_crc32(owner,16)||
       !r->read(r->context,bank+4096,p,32)||!resources_bank(p,&g,&n,&c))continue;
    if(g>generation){
      other=generation?base:0;other_generation=generation;other_expected=expected;other_bytes=bytes;
      base=bank+8192;generation=g;expected=c;bytes=n;
    } else {other=bank+8192;other_generation=g;other_expected=c;other_bytes=n;}
  }
  r->generation=generation;r->expected=expected;r->expected_bytes=bytes;r->alternate=other;
  r->alternate_generation=other_generation;r->alternate_expected=other_expected;r->alternate_bytes=other_bytes;
  if(!begin_at(r,base)&&other){
    r->alternate=0;r->generation=other_generation;r->expected=other_expected;r->expected_bytes=other_bytes;
    begin_at(r,other);
  }
}
static void failed_load(screen_resources *r,unsigned reason) {
  uint32_t alternate=r->alternate;r->alternate=0;fail(r,reason);
  if(alternate){
    r->generation=r->alternate_generation;r->expected=r->alternate_expected;r->expected_bytes=r->alternate_bytes;
    begin_at(r,alternate);
  }
}
void resources_poll(screen_resources *r) {
  if(!r||r->state!=RESOURCE_LOADING)return;
  if(r->now)r->elapsed_ms=(uint32_t)(r->now(r->context)-r->started);
  unsigned deadline=r->cache[7]=='1'?5000:SCREEN_FONT_DEADLINE;
  if(r->now&&(uint32_t)(r->now(r->context)-r->started)>=deadline){failed_load(r,RESOURCE_TIMEOUT);return;}
  uint32_t slice=r->now?r->now(r->context):0;
  /* At most 8 KiB or 8 ms per foreground slice. The old one-block-per-frame
   * policy limited a 1.6 MiB font to thousands of LVGL frame intervals. */
  for(unsigned chunks=0;chunks<16&&r->received<r->bytes;chunks++) {
    size_t n=r->bytes-r->received;if(n>512)n=512;
    uint8_t *p=r->cache+r->received;
    if(!r->read(r->context,r->base+r->received,p,n)){failed_load(r,RESOURCE_IO);return;}
    r->whole_crc=crc_update(r->whole_crc,p,n);r->payload_crc=crc_update(r->payload_crc,p,n);
    r->received+=n;
    if(r->now){uint32_t now=r->now(r->context);
      if((uint32_t)(now-r->started)>=deadline){failed_load(r,RESOURCE_TIMEOUT);return;}
      if((uint32_t)(now-slice)>=8)break;
    }
  }
  if(r->now)r->elapsed_ms=(uint32_t)(r->now(r->context)-r->started);
  if(r->received<r->bytes)return;
  if((r->expected&&~r->whole_crc!=r->expected)||~r->payload_crc!=scene_u32(r->cache+12)||
     !validate(r,r->bytes,true))failed_load(r,RESOURCE_INVALID);
}
bool resources_load(screen_resources *r) {
  if(!r)return false;
  resources_start(r);
  while(r->state==RESOURCE_LOADING){if(r->service)r->service(r->context);resources_poll(r);}
  return r->state==RESOURCE_READY;
}
bool resources_glyph(const screen_resources *r,uint32_t c,resource_glyph *out) {
  if(!r||!out||r->state!=RESOURCE_READY)return false;
  unsigned lo=0,hi=r->count;
  while(lo<hi) {
    unsigned mid=(lo+hi)/2;const uint8_t *e=r->cache+32+mid*24;uint32_t cp=scene_u32(e);
    if(cp<c)lo=mid+1;else if(cp>c)hi=mid;
    else{*out=(resource_glyph){u16(e+10),u16(e+12),u16(e+14),(int16_t)u16(e+16),
                             (int16_t)u16(e+18),r->cache+scene_u32(e+4),r->bpp};return true;}
  }
  return false;
}
