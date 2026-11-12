#include "media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static screen_media m;
static uint8_t cmd[MEDIA_PACKET_MAX], original[MEDIA_PACKET_MAX];
static unsigned calls, fail_at;
static bool decode(void *ctx,const uint8_t *p,size_t n,const media_jpeg *j,uint16_t *out,unsigned stride) {
  (void)ctx;(void)p;(void)n;calls++;
  memset(out,calls==fail_at?0xee:0x12,stride*((j->height+15u)&~15u)*2);
  return calls!=fail_at;
}
static unsigned push(unsigned id) {
  memset(cmd,0,sizeof(cmd));cmd[0]=MEDIA_PUSH;media_put32(cmd+1,id);
  media_put32(cmd+9,10000);cmd[13]=1;cmd[14]=30;cmd[15]=5;
  memcpy(cmd+16,"TitleBody",9);return media_command(&m,cmd,25);
}
static size_t patch(unsigned id,unsigned mask,const char *title,const char *text) {
  memset(cmd,0,sizeof(cmd));cmd[0]=MEDIA_PUSH_UPDATE;media_put32(cmd+1,id);
  cmd[16]=mask;cmd[17]=mask>>8;cmd[15]=strlen(title);
  size_t at=18+((mask&MEDIA_UPDATE_SOURCE)?MEDIA_STREAM_CONFIG_BYTES:0);
  memcpy(cmd+at,title,strlen(title));memcpy(cmd+at+strlen(title),text,strlen(text));
  return at+strlen(title)+strlen(text);
}
static void source(const char *url) {
  uint8_t *s=cmd+18;s[0]=MEDIA_STREAM_CONFIG;s[1]=64;s[2]=1;s[3]=240;s[5]=1;s[6]=3;
  strcpy((char *)s+7,url);
}
static void unchanged(unsigned result,size_t n) {
  media_notice before=m.queue[0];uint32_t revision=m.revision;
  assert(media_command(&m,cmd,n)==result);
  assert(!memcmp(&m.queue[0],&before,sizeof(before))&&revision==m.revision);
}
int main(void) {
  media_init(&m);m.decode=decode;assert(push(1)==MEDIA_OK);memcpy(original,cmd,25);
  media_tick(&m,100,false,false);media_present(&m);media_tick(&m,1100,false,false);
  assert(m.active==1&&m.presented&&m.queue[0].remaining==9000);
  unsigned signature=m.queue[0].signature;
  size_t n=patch(1,MEDIA_UPDATE_TEXT,"","New body\nSecond line");
  assert(media_command(&m,cmd,n)==MEDIA_OK);
  assert(!strcmp(m.queue[0].title,"Title")&&!strcmp(m.queue[0].text,"New body\nSecond line"));
  assert(m.presented&&m.queue[0].remaining==9000&&m.queue[0].signature==signature&&!calls);
  unchanged(MEDIA_OK,n); /* Exact patch replay has no visual revision. */
  assert(media_command(&m,original,25)==MEDIA_OK&&!strcmp(m.queue[0].text,"New body\nSecond line"));
  n=patch(1,MEDIA_UPDATE_DURATION,"","");media_put32(cmd+9,1000);
  unchanged(MEDIA_OK,n);media_put32(cmd+9,15000);assert(media_command(&m,cmd,n)==MEDIA_OK);
  assert(m.queue[0].remaining==15000&&m.presented);
  n=patch(1,MEDIA_UPDATE_TITLE|MEDIA_UPDATE_MODE,"Updated","");cmd[13]=2;
  assert(media_command(&m,cmd,n)==MEDIA_OK&&m.queue[0].confirm&&!strcmp(m.queue[0].text,"New body\nSecond line"));
  for(unsigned id=2;id<=MEDIA_QUEUE;id++)assert(push(id)==MEDIA_OK);
  assert(push(99)==MEDIA_BUSY);
  n=patch(16,MEDIA_UPDATE_TEXT,"","Hidden updated");assert(media_command(&m,cmd,n)==MEDIA_OK);
  assert(m.active==1&&media_at(&m,16)->id==16&&!strcmp(media_at(&m,16)->text,"Hidden updated"));
  assert(media_select(&m,0)==MEDIA_OK);
  n=patch(1,MEDIA_UPDATE_TEXT|MEDIA_UPDATE_WAKE,"","Parked updated");cmd[13]=1;
  assert(media_command(&m,cmd,n)==MEDIA_OK&&m.parked&&!m.active);
  /* Rejected patches are atomic, including sources and unknown fields. */
  n=patch(1,0,"","");unchanged(MEDIA_RANGE,n);
  n=patch(1,1u<<15,"","");unchanged(MEDIA_RANGE,n);
  n=patch(1,MEDIA_UPDATE_DURATION|MEDIA_UPDATE_TEXT,"","Rejected");media_put32(cmd+9,3600001);unchanged(MEDIA_RANGE,n);
  n=patch(1,MEDIA_UPDATE_TITLE|MEDIA_UPDATE_TEXT,"","");unchanged(MEDIA_RANGE,n);
  n=patch(1,MEDIA_UPDATE_IMAGE,"","");media_put32(cmd+5,999);unchanged(MEDIA_NOT_FOUND,n);
  n=patch(1,MEDIA_UPDATE_FPS,"","");cmd[23]=2;unchanged(MEDIA_RANGE,n);
  n=patch(1,MEDIA_UPDATE_TEXT,"","\xff");unchanged(MEDIA_RANGE,n);
  /* URL merge preserves fields, clears credentials on a changed destination. */
  n=patch(1,MEDIA_UPDATE_URL|MEDIA_UPDATE_USERNAME|MEDIA_UPDATE_PASSWORD,"","");source("http://camera.test/first");
  strcpy((char *)cmd+18+391,"viewer");strcpy((char *)cmd+18+455,"test-only");
  assert(media_command(&m,cmd,n)==MEDIA_OK&&m.queue[0].source[0]);
  assert(media_select(&m,1)==MEDIA_OK);m.source_ready=true;media_present(&m);
  n=patch(1,MEDIA_UPDATE_TEXT,"","Keep connection");assert(media_command(&m,cmd,n)==MEDIA_OK&&m.source_ready&&m.presented);
  n=patch(1,MEDIA_UPDATE_URL,"","");source("http://camera.test/first");unchanged(MEDIA_OK,n);
  n=patch(1,MEDIA_UPDATE_FPS|MEDIA_UPDATE_WIDTH,"","");source("");cmd[23]=3;
  assert(media_command(&m,cmd,n)==MEDIA_OK&&!m.source_ready&&!m.presented);
  assert(m.queue[0].source[5]==3&&!strcmp((char *)m.queue[0].source+455,"test-only"));
  n=patch(1,MEDIA_UPDATE_URL,"","");source("http://other.test/image.jpg");
  assert(media_command(&m,cmd,n)==MEDIA_OK&&!m.queue[0].source[391]&&!m.queue[0].source[455]&&m.queue[0].source[5]==3);
  n=patch(1,MEDIA_UPDATE_URL,"","");source("https://bad.test/#fragment");unchanged(MEDIA_RANGE,n);
  n=patch(1,MEDIA_UPDATE_URL,"","");source("");assert(media_command(&m,cmd,n)==MEDIA_OK&&!m.queue[0].source[0]);
  /* Uploaded replacement decodes once; decoder failure restores the old image. */
  FILE *f=fopen(MEDIA_FIXTURE,"rb");assert(f);media_image *im=&m.images[0];
  im->size=fread(im->bytes,1,sizeof(im->bytes),f);fclose(f);im->id=51;im->state=2;
  m.images[1]=*im;m.images[1].id=52;
  n=patch(1,MEDIA_UPDATE_IMAGE,"","");media_put32(cmd+5,51);
  assert(media_command(&m,cmd,n)==MEDIA_OK&&m.queue[0].image==51&&calls==1&&m.width==32);
  uint32_t crc=m.pixel_crc;media_present(&m);unchanged(MEDIA_OK,n);assert(calls==1&&m.presented);
  n=patch(1,MEDIA_UPDATE_IMAGE|MEDIA_UPDATE_TEXT,"","Rejected new image");media_put32(cmd+5,52);fail_at=calls+1;
  unchanged(MEDIA_DECODE,n);assert(calls==3&&m.pixel_crc==crc&&m.pixels[0]==0x1212);
  fail_at=0;assert(media_command(&m,cmd,n)==MEDIA_OK&&m.queue[0].image==52&&calls==4);
  /* Updating a completed ID never replays it. Dismiss remains idempotent. */
  cmd[0]=MEDIA_DISMISS;media_put32(cmd+1,1);assert(media_command(&m,cmd,5)==MEDIA_OK);
  n=patch(1,MEDIA_UPDATE_TEXT,"","Late update");assert(media_command(&m,cmd,n)==MEDIA_NOT_FOUND);
  assert(media_command(&m,original,25)==MEDIA_OK&&media_count(&m)==15&&!m.active);
  puts("PASS partial updates: stable IDs, full queue, atomic rejects, visible time, hidden views, source ownership, image rollback and late retries");
}
