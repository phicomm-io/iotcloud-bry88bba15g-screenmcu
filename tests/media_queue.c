#include "media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static screen_media m;
static uint8_t jpeg[4096],cmd[MEDIA_PACKET_MAX];
static unsigned calls;
static bool decode(void *ctx,const uint8_t *p,size_t n,const media_jpeg *j,uint16_t *out,unsigned stride) {
  (void)ctx;(void)p;(void)n;calls++;
  for(unsigned y=0;y<j->height;y++)for(unsigned x=0;x<stride;x++)out[y*stride+x]=x+y*32;
  return true;
}
static unsigned push(unsigned id,bool wake,bool confirm,unsigned image) {
  memset(cmd,0,sizeof(cmd));cmd[0]=MEDIA_PUSH;media_put32(cmd+1,id);media_put32(cmd+5,image);
  media_put32(cmd+9,1000);cmd[13]=(wake?1:0)|(confirm?2:0);cmd[14]=30;memcpy(cmd+16,"hello\nworld",11);
  return media_command(&m,cmd,27);
}
int main(void) {
  FILE *f=fopen(MEDIA_FIXTURE,"rb");assert(f);size_t n=fread(jpeg,1,sizeof(jpeg),f);fclose(f);
  media_jpeg j;assert(media_jpeg_parse(&j,jpeg,n)&&j.width==32&&j.height==17);
  for(size_t i=0;i<n;i++)assert(!media_jpeg_parse(&j,jpeg,i));
  uint8_t bad[4096];memcpy(bad,jpeg,n);for(size_t i=0;i<n-1;i++)if(bad[i]==255&&bad[i+1]==0xc0){bad[i+1]=0xc2;break;}assert(!media_jpeg_parse(&j,bad,n));
  media_init(&m);m.decode=decode;
  cmd[0]=MEDIA_BEGIN;media_put32(cmd+1,55);media_put32(cmd+5,n);media_put32(cmd+9,media_crc32(jpeg,n));cmd[13]=32;cmd[15]=17;
  assert(media_command(&m,cmd,17)==MEDIA_OK);assert(media_command(&m,cmd,17)==MEDIA_OK);
  cmd[0]=MEDIA_COMMIT;assert(media_command(&m,cmd,5)==MEDIA_RANGE);
  for(size_t at=0;at<n;){size_t bytes=n-at>512?512:n-at;cmd[0]=MEDIA_WRITE;media_put32(cmd+5,at);memcpy(cmd+9,jpeg+at,bytes);
    assert(media_command(&m,cmd,bytes+9)==MEDIA_OK);assert(media_command(&m,cmd,bytes+9)==MEDIA_OK);
    cmd[9]^=1;assert(media_command(&m,cmd,bytes+9)==MEDIA_RANGE);cmd[9]^=1;at+=bytes;}
  m.images[0].bytes[5]^=1;cmd[0]=MEDIA_COMMIT;assert(media_command(&m,cmd,5)==MEDIA_CRC);m.images[0].bytes[5]^=1;
  assert(media_command(&m,cmd,5)==MEDIA_OK);assert(push(1,false,false,55)==MEDIA_OK);
  assert(push(1,false,false,55)==MEDIA_OK);assert(push(1,true,false,55)==MEDIA_RANGE);
  media_tick(&m,1,true,false);assert(!m.active);media_tick(&m,100,false,false);assert(m.active==1&&calls==1&&!m.presented);
  media_tick(&m,5000,false,false);assert(media_current(&m)->remaining==1000);media_present(&m);
  media_tick(&m,5500,false,false);assert(media_current(&m)->remaining==500);
  media_tick(&m,6000,false,true);assert(media_current(&m)->remaining==500);
  media_tick(&m,20000,false,false);assert(media_current(&m)->remaining==500);
  media_tick(&m,20500,false,false);assert(!m.active&&m.history[0].state==PUSH_TIMEDOUT);
  assert(push(1,false,false,55)==MEDIA_OK&&!m.active); /* Completed id is not replayed. */
  assert(push(2,true,true,0)==MEDIA_OK);media_tick(&m,21000,true,false);assert(m.active==2);
  assert(!media_touch(&m));media_present(&m);media_tick(&m,99999,true,false);assert(m.active==2);
  assert(media_touch(&m)&&!m.active&&m.history[1].state==PUSH_CONFIRMED);
  for(unsigned i=3;i<7;i++)assert(push(i,true,true,0)==MEDIA_OK);
  assert(push(7,true,true,0)==MEDIA_BUSY);
  cmd[0]=MEDIA_DISMISS;media_put32(cmd+1,4);assert(media_command(&m,cmd,5)==MEDIA_OK);
  assert(push(7,true,true,0)==MEDIA_OK);media_tick(&m,100000,false,false);assert(m.active==3);
  media_present(&m);assert(media_touch(&m));media_tick(&m,100001,false,false);assert(m.active==5);
  uint8_t status[132];assert(media_status(&m,status,sizeof(status))==132);assert(!media_status(&m,status,131));
  media_tick(&m,220001,false,true);assert(!m.images[0].id);assert(push(8,true,false,0)==MEDIA_BUSY);
}
