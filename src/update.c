#include "update.h"
#include <string.h>
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void screen_update_abort(screen_update *s){s->failed=true;s->transfer.active=false;s->transfer.verified=false;memset(&s->md5,0,sizeof(s->md5));}
bool screen_update_begin(screen_update *s,uint32_t size,const uint8_t md5[16]){
 if(!s||!md5||!s->erase||!s->write||!s->read||!screen_ota_begin(&s->transfer,size))return false;
 s->failed=false;memcpy(s->expected,md5,16);memset(s->vectors,0,8);screen_md5_init(&s->md5);
 size_t erase_size=(size+0x7fffu)&~0x7fffu;
 if(!s->erase(s->context,SCREEN_OTA_STAGE,erase_size)){screen_update_abort(s);return false;}return true;
}
bool screen_update_chunk(screen_update *s,uint32_t offset,const void *data,size_t size){
 if(!s||s->failed||!s->transfer.active||!data||!size||size>1024||offset!=s->transfer.received||size>s->transfer.size-offset)return false;
 if(!s->write(s->context,SCREEN_OTA_STAGE+offset,data,size)){screen_update_abort(s);return false;}
 if(offset<8){size_t take=8-offset;if(take>size)take=size;memcpy(s->vectors+offset,data,take);}
 screen_md5_update(&s->md5,data,size);return screen_ota_accept(&s->transfer,offset,data,size);
}
bool screen_update_finish(screen_update *s){
 if(!s||s->failed||!s->transfer.active||s->transfer.received!=s->transfer.size)return false;
 uint8_t digest[16];screen_md5_final(&s->md5,digest);
 uint32_t sp=le32(s->vectors),reset=le32(s->vectors+4);
 if(memcmp(digest,s->expected,16)||(sp&7)||sp<=0x20000000u||sp>0x20010000u||!(reset&1)||reset<SCREEN_APP_BASE||reset>=SCREEN_APP_BASE+s->transfer.size){screen_update_abort(s);return false;}
 if(!screen_update_recheck(s)){screen_update_abort(s);return false;}
 return screen_ota_verify(&s->transfer,true);
}
bool screen_update_recheck(screen_update *s){
 if(!s||s->failed||!s->read||s->transfer.received!=s->transfer.size||s->transfer.size<64||s->transfer.size>SCREEN_APP_LIMIT)return false;
 /* Confirm what actually reached NOR, including all data after the vectors. */
 uint8_t digest[16];
 screen_md5_init(&s->md5);uint8_t block[256];
 for(uint32_t offset=0;offset<s->transfer.size;){size_t n=s->transfer.size-offset;if(n>sizeof(block))n=sizeof(block);
  if(!s->read(s->context,SCREEN_OTA_STAGE+offset,block,n)){screen_update_abort(s);return false;}
  screen_md5_update(&s->md5,block,n);offset+=(uint32_t)n;
 }
 screen_md5_final(&s->md5,digest);if(memcmp(digest,s->expected,16)){screen_update_abort(s);return false;}
 return true;
}
