#include "font_bank.h"
#include "scene.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t nor[2*SCREEN_FONT_BANK],cache[SCREEN_RESOURCE_LIMIT],request[524],reply[20],file[57];
static unsigned now_ms,reads,writes,erases,read_latency_ms;static bool broken,fail_commit;
static bool read_at(void *c,uint32_t at,void *p,size_t n) {
 (void)c;reads++;if(broken||at<SCREEN_FONT_BASE||at>SCREEN_FONT_BASE+sizeof(nor)||n>SCREEN_FONT_BASE+sizeof(nor)-at)return false;
 if(n==512)now_ms+=read_latency_ms;
 memcpy(p,nor+at-SCREEN_FONT_BASE,n);return true;
}
static bool write_at(void *c,uint32_t at,const void *p,size_t n) {
 (void)c;writes++;if(broken||(fail_commit&&(at-SCREEN_FONT_BASE)%SCREEN_FONT_BANK==4096)||at<SCREEN_FONT_BASE||at>SCREEN_FONT_BASE+sizeof(nor)||n>SCREEN_FONT_BASE+sizeof(nor)-at)return false;
 for(size_t i=0;i<n;i++)nor[at-SCREEN_FONT_BASE+i]&=((const uint8_t*)p)[i];
 return true;
}
static bool erase_at(void *c,uint32_t at,size_t n) {
 (void)c;erases++;if(broken||n!=4096||(at&4095)||at<SCREEN_FONT_BASE||at>=SCREEN_FONT_BASE+sizeof(nor))return false;
 memset(nor+at-SCREEN_FONT_BASE,255,n);return true;
}
static uint32_t clock_at(void *c){(void)c;return now_ms;}
static screen_resources resource(void) {return (screen_resources){.read=read_at,.now=clock_at,.cache=cache,.capacity=sizeof(cache)};}
static screen_font_bank setup(screen_resources *r) {
 return (screen_font_bank){.read=read_at,.write=write_at,.erase=erase_at,.now=clock_at,.font=r};
}
static void make_file(unsigned gray) {
 memset(file,0,sizeof(file));memcpy(file,"IOTFNT02",8);scene_put32(file+8,sizeof(file));
 file[16]=1;file[18]=24;file[20]=36;file[21]=2;file[22]=2;file[23]=32;
 scene_put32(file+32,0x9f99);scene_put32(file+36,56);file[40]=1;file[42]=32;file[44]=file[46]=2;file[56]=gray;
 scene_put32(file+12,scene_crc32(file+32,25));scene_put32(file+28,scene_crc32(file,28));
}
static bool op(screen_font_bank *b,unsigned kind,unsigned at,const uint8_t *bytes,size_t n) {
 memset(request,0,12);request[0]=SCREEN_MSG_FONT;request[1]=1;request[2]=kind;scene_put32(request+4,18);scene_put32(request+8,at);
 if(n)memcpy(request+12,bytes,n);
 assert(font_bank_receive(b,request,12+n,reply));return reply[2]==0;
}
static void prepare(screen_font_bank *b) {
 assert(op(b,1,0,file,32));unsigned polls=0;
 while(b->preparing){font_bank_poll(b);assert(++polls<5000);}
 assert(b->active);assert(op(b,2,0,file,sizeof(file)));
}
int main(void) {
 memset(nor,255,sizeof(nor));make_file(0x6c);screen_resources r=resource();screen_font_bank b=setup(&r);font_bank_init(&b);
 prepare(&b);assert(!r.count);assert(op(&b,3,0,NULL,0));assert(b.generation==1&&r.state==RESOURCE_READY);
 resource_glyph g;assert(resources_glyph(&r,0x9f99,&g)&&g.bpp==2&&g.pixels[0]==0x6c);
 r=resource();assert(resources_load(&r));unsigned before=reads;broken=true;
 for(unsigned i=0;i<1000;i++)assert(resources_glyph(&r,0x9f99,&g));
 assert(reads==before);broken=false;
 /* Power loss after payload, before commit: prior complete bank still boots. */
 b=setup(&r);font_bank_init(&b);make_file(0x93);prepare(&b);
 r=resource();assert(resources_load(&r)&&resources_glyph(&r,0x9f99,&g)&&g.pixels[0]==0x6c);
 /* A failed header write does not claim the new generation. */
 b=setup(&r);font_bank_init(&b);prepare(&b);fail_commit=true;assert(!op(&b,3,0,NULL,0));fail_commit=false;
 r=resource();assert(resources_load(&r)&&resources_glyph(&r,0x9f99,&g)&&g.pixels[0]==0x6c);
 /* Fully committed new bank; corrupted latest payload falls back once. */
 b=setup(&r);font_bank_init(&b);prepare(&b);assert(op(&b,3,0,NULL,0)&&b.generation==2);
 nor[SCREEN_FONT_BANK+8192+56]^=1;r=resource();assert(resources_load(&r)&&resources_glyph(&r,0x9f99,&g)&&g.pixels[0]==0x6c);
 assert(r.generation==1&&r.base==SCREEN_FONT_BASE+8192);
 /* Updating after fallback preserves the known-good bank until commit. */
 b=setup(&r);font_bank_init(&b);prepare(&b);assert(b.target==SCREEN_FONT_BASE+SCREEN_FONT_BANK);
 r=resource();assert(resources_load(&r)&&r.generation==1);
 b=setup(&r);font_bank_init(&b);prepare(&b);assert(op(&b,3,0,NULL,0)&&b.generation==2);
 r=resource();assert(resources_load(&r)&&r.generation==2);
 /* Unknown nonblank memory is never erased or programmed. */
 memset(nor,255,sizeof(nor));nor[1500]=0;r=resource();b=setup(&r);font_bank_init(&b);before=erases;unsigned wb=writes;
 assert(op(&b,1,0,file,32));for(unsigned i=0;i<6;i++)font_bank_poll(&b);assert(!b.active&&b.state==RESOURCE_IO&&erases==before&&writes==wb);
 /* Bounded foreground work and timeout; queries leave the watchdog/UART owner available. */
 memset(nor,255,sizeof(nor));r=resource();b=setup(&r);font_bank_init(&b);assert(op(&b,1,0,file,32));before=reads;font_bank_poll(&b);assert(reads-before<=2);
 now_ms+=SCREEN_FONT_DEADLINE+1;font_bank_poll(&b);assert(!b.active&&b.state==RESOURCE_TIMEOUT);
 /* Conservative GPIO SPI can take over a minute to scan a full blank bank.
  * The foreground still performs one 512-byte block per poll. */
 now_ms=0;read_latency_ms=25;r=resource();b=setup(&r);font_bank_init(&b);
 assert(op(&b,1,0,file,32));unsigned polls=0;
 while(b.preparing){before=reads;font_bank_poll(&b);assert(reads-before<=3);assert(++polls<5000);}
 assert(b.active&&now_ms>60000&&now_ms<SCREEN_FONT_DEADLINE);
 assert(op(&b,2,0,file,sizeof(file))&&op(&b,3,0,NULL,0));
 puts("font bank: checked chunks, atomic header, power cuts, older-bank fallback, foreign data and timeout passed");return 0;
}
