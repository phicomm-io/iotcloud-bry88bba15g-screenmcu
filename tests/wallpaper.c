#include "wallpaper.h"
#include "scene.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
static uint8_t nor[WALLPAPER_BANK*2],data[WALLPAPER_BYTES];
static uint16_t pixels[480*854];
static uint32_t clock_ms;
static bool fail_read,fail_write,off;
static unsigned writes,reads;
static uint32_t read_delay;
static bool reenter;
static unsigned reentries;
static uint8_t header[32];
static bool read_nor(void *c,uint32_t a,void *p,size_t n) {
 (void)c;reads++;if(fail_read||a<WALLPAPER_BASE||n>sizeof(nor)-(a-WALLPAPER_BASE))return false;
 memcpy(p,nor+a-WALLPAPER_BASE,n);clock_ms+=read_delay;return true;
}
static bool write_nor(void *c,uint32_t a,const void *p,size_t n) {
 (void)c;writes++;if(fail_write||a<WALLPAPER_BASE||n>sizeof(nor)-(a-WALLPAPER_BASE))return false;
 for(size_t i=0;i<n;i++)nor[a-WALLPAPER_BASE+i]&=((const uint8_t*)p)[i];
 return true;
}
static bool erase_nor(void *c,uint32_t a,size_t n) {
 (void)c;assert(!(a&4095)&&n==4096&&a>=WALLPAPER_BASE&&a+n<=WALLPAPER_BASE+sizeof(nor));memset(nor+a-WALLPAPER_BASE,255,n);return true;
}
static uint32_t now(void *c) {(void)c;return clock_ms;}
static void activity(void *c,bool active){(void)c;off=active;}
static void service(void *c) {
 if(!reenter)return;
 screen_wallpaper *w=c;
 uint8_t p[44]={SCREEN_MSG_WALLPAPER,1,WP_BEGIN},reply[20];memcpy(p+12,header,32);
 unsigned before=reads;wallpaper_poll(w);assert(reads==before);
 assert(wallpaper_receive(w,p,sizeof(p),reply)&&reply[2]&&!w->active&&!off);
 p[2]=WP_QUERY;assert(wallpaper_receive(w,p,12,reply)&&!reply[2]);reentries++;
}
static screen_wallpaper setup(void) {
 return (screen_wallpaper){.read=read_nor,.write=write_nor,.erase=erase_nor,.now=now,.pixels=pixels,.activity=activity};
}
static void settled(screen_wallpaper *w) {
 for(unsigned i=0;i<1100;i++){wallpaper_poll(w);if(!w->preparing&&!w->reading&&!w->pending&&(!w->kind||w->ready||w->kind==4))return;}
 assert(!"Wallpaper did not settle");
}
static void begin(screen_wallpaper *w) {
 uint8_t p[44]={SCREEN_MSG_WALLPAPER,1,WP_BEGIN},reply[20];memcpy(p+12,header,32);
 assert(wallpaper_receive(w,p,sizeof(p),reply)&&!reply[2]&&off&&reply[3]==WALLPAPER_PREPARING);
 for(unsigned i=0;w->preparing&&i<600;i++)wallpaper_poll(w);
 assert(w->active&&!w->preparing);
}
static void upload(screen_wallpaper *w) {
 begin(w);uint8_t p[524]={SCREEN_MSG_WALLPAPER,1,WP_CHUNK},reply[20];
 for(unsigned at=0;at<WALLPAPER_BYTES;at+=512) {
  size_t n=WALLPAPER_BYTES-at;if(n>512)n=512;
  scene_put32(p+8,at);memcpy(p+12,data+at,n);
  assert(wallpaper_receive(w,p,12+n,reply)&&!reply[2]);
 }
 p[2]=WP_COMMIT;assert(wallpaper_receive(w,p,12,reply)&&!reply[2]&&!off);
 w->wanted=4;settled(w);assert(w->ready&&w->crc==scene_crc32(data,sizeof(data)));
}
int main(void) {
 memset(nor,255,sizeof(nor));for(unsigned i=0;i<sizeof(data);i++)data[i]=i*13;
 memcpy(header,"IOTW5651",8);header[12]=240;header[14]=171;header[15]=1;
 scene_put32(header+16,scene_crc32(data,sizeof(data)));scene_put32(header+20,sizeof(data));scene_put32(header+28,scene_crc32(header,28));
 assert(wallpaper_header(header,32));screen_wallpaper w=setup();wallpaper_init(&w);assert(!w.ready&&!w.pending);
 upload(&w);assert(w.generation==1);assert(pixels[0]==(uint16_t)(data[0]|data[1]<<8)&&pixels[0]==pixels[481]);
 screen_wallpaper restored=setup();wallpaper_init(&restored);restored.wanted=4;
 restored.service=service;restored.context=&restored;reenter=true;settled(&restored);reenter=false;
 assert(restored.ready&&restored.generation==1&&reentries==WALLPAPER_HEIGHT);
 // Slow I/O stops at the load deadline, then remains in fallback without retries.
 restored=setup();wallpaper_init(&restored);restored.wanted=4;read_delay=200;
 settled(&restored);read_delay=0;assert(!restored.ready&&restored.state==RESOURCE_TIMEOUT);
 unsigned stopped=reads;wallpaper_poll(&restored);assert(reads==stopped);
 // A partial second-bank upload and reboot retain the first complete image.
 begin(&w);screen_wallpaper cut=setup();wallpaper_init(&cut);cut.wanted=4;settled(&cut);assert(cut.ready&&cut.generation==1);
 clock_ms+=16000;wallpaper_poll(&w);assert(!w.active&&!off);
 // Retrying a partially written, owned bank succeeds.
 upload(&w);assert(w.generation==2);
 // Corruption produces one bounded read, then a stable fallback without I/O.
 nor[w.bank-WALLPAPER_BASE+8192]^=1;restored=setup();wallpaper_init(&restored);restored.wanted=4;settled(&restored);assert(!restored.ready&&restored.state==RESOURCE_INVALID);
 unsigned before=reads;for(unsigned i=0;i<2000;i++)wallpaper_poll(&restored);assert(reads==before);
 // Presets work even with the NOR disconnected, and text rendering needs no NOR.
 fail_read=true;restored.wanted=1;wallpaper_poll(&restored);
 assert(!restored.ready&&restored.kind==1&&restored.load_at==4);
 settled(&restored);assert(restored.ready);fail_read=false;
 // A BEGIN immediately after changing a preset is accepted between batches.
 restored.wanted=2;wallpaper_poll(&restored);assert(!restored.ready&&restored.load_at==4);
 begin(&restored);
 uint8_t cancel[12]={SCREEN_MSG_WALLPAPER,1,WP_CANCEL},cancel_reply[20];
 assert(wallpaper_receive(&restored,cancel,sizeof(cancel),cancel_reply)&&!cancel_reply[2]);
 settled(&restored);assert(restored.ready&&restored.kind==2);
 uint32_t last=wallpaper_preset(2,479,853);
 assert(pixels[480*854-1]==(uint16_t)(((last>>8)&0xf800)|((last>>5)&0x7e0)|((last>>3)&31)));
 // Slow healthy SPI works without monopolizing a turn or tripping 12-second limits.
 memset(nor,255,sizeof(nor));w=setup();wallpaper_init(&w);read_delay=50;
 uint8_t prep[44]={SCREEN_MSG_WALLPAPER,1,WP_BEGIN},resp[20];memcpy(prep+12,header,32);
 assert(wallpaper_receive(&w,prep,sizeof(prep),resp)&&!resp[2]&&resp[3]==WALLPAPER_PREPARING);
 unsigned turns=0;while(w.preparing){unsigned r=reads;wallpaper_poll(&w);assert(reads-r<=4);assert(++turns<600);}
 assert(w.active&&clock_ms-w.prepare_started>12000);prep[2]=WP_CANCEL;assert(wallpaper_receive(&w,prep,12,resp));read_delay=0;
 // Foreign resource data is never erased or claimed.
 memset(nor,255,sizeof(nor));nor[8192]=0x5a;w=setup();wallpaper_init(&w);
 uint8_t p[44]={SCREEN_MSG_WALLPAPER,1,WP_BEGIN},reply[20];memcpy(p+12,header,32);before=writes;
 assert(wallpaper_receive(&w,p,sizeof(p),reply)&&!reply[2]);settled(&w);assert(!w.active&&!off&&w.state==RESOURCE_IO);assert(writes==before&&nor[8192]==0x5a);
 fail_write=true;memset(nor,255,sizeof(nor));assert(wallpaper_receive(&w,p,sizeof(p),reply)&&!reply[2]);settled(&w);assert(!w.active&&!off);
 return 0;
}
