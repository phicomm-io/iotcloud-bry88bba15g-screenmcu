#include "font_bank.h"
#include "scene.h"
#include <string.h>
screen_font_bank font_bank;
static const uint8_t owner[16]="IOTFONTBANK02";
static bool read_at(screen_font_bank *b,uint32_t at,void *p,size_t n) {return b->read&&b->read(b->context,at,p,n);}
static bool owned(screen_font_bank *b,uint32_t at) {
  uint8_t p[20];return read_at(b,at,p,20)&&!memcmp(p,owner,16)&&scene_u32(p+16)==scene_crc32(owner,16);
}
static void stop(screen_font_bank *b,unsigned state) {
  bool active=b->active;b->active=b->preparing=false;b->state=state;
  if(active&&state!=RESOURCE_READY&&b->font){b->font->state=RESOURCE_UNCHECKED;b->font->count=b->font->bytes=b->font->crc=0;}
  if(active&&b->activity)b->activity(b->context,false);
}
void font_bank_init(screen_font_bank *b) {
  b->active=b->preparing=false;b->state=RESOURCE_ABSENT;b->generation=0;b->bank=SCREEN_FONT_BASE;
  for(unsigned i=0;i<2;i++) {
    uint32_t at=SCREEN_FONT_BASE+i*SCREEN_FONT_BANK,g,n,c;uint8_t h[32];
    if(owned(b,at)&&read_at(b,at+4096,h,32)&&resources_bank(h,&g,&n,&c)&&g>b->generation) {
      b->bank=at;b->generation=g;b->crc=c;
    }
  }
}
void font_bank_poll(screen_font_bank *b) {
  if(!b->active)return;
  uint32_t now=b->now?b->now(b->context):0;
  if((int32_t)(now-b->deadline)>=0){stop(b,RESOURCE_TIMEOUT);return;}
  if(!b->preparing)return;
  uint8_t p[512];
  if(!b->prepare_at&&owned(b,b->target))b->prepare_at=SCREEN_FONT_BANK;
  if(b->prepare_at<SCREEN_FONT_BANK) {
    if(!read_at(b,b->target+b->prepare_at,p,512))goto failed;
    for(unsigned i=0;i<512;i++)if(p[i]!=255)goto failed;
    b->prepare_at+=512;return;
  }
  if(!owned(b,b->target)) {
    memcpy(p,owner,16);scene_put32(p+16,scene_crc32(owner,16));
    if(!b->write||!b->write(b->context,b->target,p,20)||!owned(b,b->target))goto failed;
  }
  /* Invalidate target header before the first payload sector is erased. */
  if(!b->erase||!b->erase(b->context,b->target+4096,4096))goto failed;
  b->preparing=false;b->deadline=now+15000;return;
failed:stop(b,RESOURCE_IO);
}
bool font_bank_receive(screen_font_bank *b,const uint8_t *p,size_t n,uint8_t reply[20]) {
  if(n<12||p[0]!=SCREEN_MSG_FONT||p[1]!=1||p[2]>4||p[3])return false;
  unsigned op=p[2];bool ok=false;uint32_t now=b->now?b->now(b->context):0;
  if(op==0)ok=n==12;
  else if(op==4&&n==12){stop(b,RESOURCE_ABSENT);ok=true;}
  else if(op==1&&n==44&&!b->active&&b->font&&b->font->cache&&b->font->state!=RESOURCE_LOADING&&
          !memcmp(p+12,"IOTFNT02",8)&&resources_header(p+12,b->font->capacity)&&b->generation<UINT32_MAX) {
    /* Preserve the bank whose payload actually validated, including fallback. */
    if(b->font->state==RESOURCE_READY&&b->font->base>=SCREEN_FONT_BASE+8192u&&
       b->font->base<SCREEN_FONT_BASE+2*SCREEN_FONT_BANK)b->bank=b->font->base-8192u;
    b->target=b->generation?(b->bank==SCREEN_FONT_BASE?SCREEN_FONT_BASE+SCREEN_FONT_BANK:SCREEN_FONT_BASE):SCREEN_FONT_BASE;
    memcpy(b->header,p+12,32);b->total=scene_u32(p+20);
    b->active=b->preparing=true;b->received=b->prepare_at=b->erased=0;
    b->started=now;b->deadline=now+SCREEN_FONT_DEADLINE;
    /* Keep known builtin text while the same SDRAM cache is reused. */
    b->font->state=RESOURCE_ABSENT;b->font->count=b->font->bytes=b->font->crc=0;
    if(b->activity)b->activity(b->context,true);
    ok=true;
  } else if(op==2&&b->active&&!b->preparing&&n>12&&n<=524&&
            scene_u32(p+8)==b->received&&n-12<=b->total-b->received) {
    uint32_t end=b->received+n-12;ok=true;
    while(ok&&b->erased<end) {
      ok=b->erase&&b->erase(b->context,b->target+8192+b->erased,4096);b->erased+=4096;
    }
    uint8_t check[512];uint32_t at=b->target+8192+b->received;
    ok=ok&&b->write&&b->write(b->context,at,p+12,n-12)&&read_at(b,at,check,n-12)&&!memcmp(check,p+12,n-12);
    if(ok){memcpy(b->font->cache+b->received,check,n-12);b->received=end;b->deadline=now+15000;}
  } else if(op==3&&n==12&&b->active&&!b->preparing&&b->received==b->total&&
            !memcmp(b->header,b->font->cache,32)&&resources_validate(b->font,b->total)) {
    uint8_t h[32]={0},check[32];memcpy(h,"IOTFBK02",8);scene_put32(h+8,b->generation+1);
    scene_put32(h+12,b->total);uint32_t crc=scene_crc32(b->font->cache,b->total);
    scene_put32(h+16,crc);scene_put32(h+28,scene_crc32(h,28));
    ok=b->write&&b->write(b->context,b->target+4096,h,32)&&read_at(b,b->target+4096,check,32)&&!memcmp(check,h,32);
    if(ok){b->generation++;b->bank=b->target;b->crc=crc;b->font->generation=b->generation;b->font->base=b->bank+8192;
      b->font->expected=crc;b->font->expected_bytes=b->total;stop(b,RESOURCE_READY);}
  }
  if(!ok&&op){if(b->active&&b->font)b->font->state=RESOURCE_ABSENT;stop(b,RESOURCE_IO);}
  memset(reply,0,20);reply[0]=SCREEN_MSG_FONT_STATUS;reply[1]=1;reply[2]=ok?0:1;
  reply[3]=b->preparing?RESOURCE_PREPARING:b->active?RESOURCE_UPLOADING:b->state;
  scene_put32(reply+4,scene_u32(p+4));scene_put32(reply+8,b->received);
  scene_put32(reply+12,b->crc);scene_put32(reply+16,b->generation);return true;
}
