#include "media_source.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static screen_stream s;
static screen_media m;
static media_source_owner owner;
static uint8_t cfg[MEDIA_STREAM_CONFIG_BYTES], packet[MEDIA_PACKET_MAX], jpeg[4096], response[8192];
static size_t jpeg_size;
static unsigned opens;
static bool send_wire(void *ctx,const uint8_t *p,size_t n){(void)ctx;assert(n>=5&&n<=517);if(p[0]==MEDIA_NET_OPEN)opens++;return true;}
static bool decode(void *ctx,const uint8_t *p,size_t n,const media_jpeg *j,uint16_t *out,unsigned stride){(void)ctx;(void)p;(void)n;memset(out,0x55,stride*((j->height+15)&~15u)*2);return true;}
static void setup(void){media_init(&m);stream_init(&s);s.send=send_wire;s.decode=decode;media_source_init(&owner);memset(cfg,0,sizeof(cfg));cfg[0]=MEDIA_STREAM_CONFIG;cfg[1]=64;cfg[2]=1;cfg[3]=240;cfg[5]=1;cfg[6]=3;strcpy((char *)cfg+7,"http://camera.test/page");assert(media_source_config(&owner,&s,cfg,sizeof(cfg))==MEDIA_OK);opens=0;}
static unsigned push(unsigned id,const char *url,unsigned duration){memset(packet,0,sizeof(packet));packet[0]=MEDIA_PUSH_URL;media_put32(packet+1,id);media_put32(packet+9,duration);packet[13]=1;packet[14]=30;memcpy(packet+16,cfg,sizeof(cfg));strcpy((char *)packet+23,url);return media_command(&m,packet,16+sizeof(cfg));}
static void poll(unsigned now,bool page,bool visible){media_source_poll(&owner,&m,&s,now,page,visible,true);}
static void event(unsigned op,uint32_t seq,const void *data,size_t n){uint8_t wire[517]={op};media_put32(wire+1,s.connection);if(n)memcpy(wire+5,data,n);stream_socket(&s,seq,wire,n+5);}
static void frame(unsigned now,bool multipart){event(MEDIA_NET_CONNECTED,0,NULL,0);size_t n;if(multipart)n=snprintf((char *)response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",jpeg_size);else n=snprintf((char *)response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",jpeg_size);memcpy(response+n,jpeg,jpeg_size);n+=jpeg_size;uint32_t seq=0;for(size_t at=0;at<n;){size_t bytes=n-at>512?512:n-at;event(MEDIA_NET_WRITE,++seq,response+at,bytes);at+=bytes;}poll(now,false,true);assert(s.frames&&m.source_ready);media_present(&m);assert(m.presented);}
int main(void){FILE *f=fopen(MEDIA_FIXTURE,"rb");assert(f);jpeg_size=fread(jpeg,1,sizeof(jpeg),f);fclose(f);
 setup();assert(push(1,"http://bad@host/image",1000)==MEDIA_RANGE);assert(push(1,"http://camera.test/event",1000)==MEDIA_OK);media_put32(packet+5,1);assert(media_command(&m,packet,535)==MEDIA_RANGE);media_put32(packet+5,0);
 media_tick(&m,1,false,false);poll(1,false,true);assert(owner.notice_id==1&&opens==1&&!strcmp(s.path,"/event"));media_present(&m);assert(!m.presented);media_tick(&m,9000,false,false);assert(media_current(&m)->remaining==1000);frame(9001,false);media_tick(&m,9500,false,false);assert(media_current(&m)->remaining==500);
 uint32_t connection=s.connection,revision=m.revision;unsigned before=opens;
 assert(push(2,"http://camera.test/event",3000)==MEDIA_OK&&m.last_push_id==1&&media_count(&m)==1);poll(9501,false,true);assert(s.connection==connection&&opens==before&&m.revision==revision&&media_current(&m)->remaining==3000);
 assert(push(3,"http://camera.test/event",1000)==MEDIA_OK&&media_current(&m)->remaining==3000);
 media_tick(&m,12000,false,false);assert(media_current(&m)->remaining==500);media_tick(&m,12500,false,false);assert(!m.active);poll(12501,true,true);assert(!owner.notice_id&&!strcmp(s.path,"/page")&&!memcmp(owner.page,cfg,sizeof(cfg)));
 // Re-entering the same source does not allocate another image arena. The
 // stack can be full and still accept an event renewing an existing URL.
 setup();for(unsigned i=1;i<=MEDIA_QUEUE;i++){char url[64];snprintf(url,sizeof(url),"http://camera.test/event%u",i);assert(push(i,url,1000)==MEDIA_OK);}assert(push(100,"http://camera.test/extra",1000)==MEDIA_BUSY);assert(push(101,"http://camera.test/event1",5000)==MEDIA_OK&&media_count(&m)==MEDIA_QUEUE&&m.last_push_id==1);
 // Missing frames fail within a bounded visible interval. Hidden time and
 // maintenance do not consume that interval or the display duration.
 setup();assert(push(1,"http://camera.test/missing",1000)==MEDIA_OK);media_tick(&m,1,false,false);poll(1,false,true);poll(10001,false,false);poll(20001,false,false);poll(20002,false,true);assert(m.active);poll(35002,false,true);assert(!m.active&&m.source_error&&m.history[0].state==PUSH_FAILED);poll(35003,true,true);assert(!strcmp(s.path,"/page"));
 // Pausing a notice preserves its countdown, and a changed permanent source
 // is applied only after the notice releases the single network owner.
 setup();assert(push(1,"http://camera.test/mjpeg",1000)==MEDIA_OK);media_tick(&m,1,false,false);poll(1,false,true);frame(100,true);assert(media_select(&m,0)==MEDIA_OK);poll(101,true,true);media_tick(&m,5000,false,false);assert(m.queue[0].remaining==1000);assert(media_select(&m,1)==MEDIA_OK);poll(5001,false,true);frame(5002,true);strcpy((char *)cfg+7,"http://camera.test/newpage");assert(media_source_config(&owner,&s,cfg,sizeof(cfg))==MEDIA_OK&&!strcmp(s.path,"/mjpeg"));assert(media_touch(&m));poll(5003,true,true);assert(!strcmp(s.path,"/newpage"));
 // Explicit ID text/time patches leave the active connection and frame intact.
 setup();assert(push(7,"http://camera.test/updates",1000)==MEDIA_OK);media_tick(&m,1,false,false);poll(1,false,true);frame(10,true);
 connection=s.connection;before=opens;memset(packet,0,sizeof(packet));packet[0]=MEDIA_PUSH_UPDATE;
 media_put32(packet+1,7);packet[16]=MEDIA_UPDATE_TEXT|MEDIA_UPDATE_DURATION;media_put32(packet+9,4000);
 memcpy(packet+18,"Updated live text",17);assert(media_command(&m,packet,35)==MEDIA_OK);poll(20,false,true);
 assert(s.connection==connection&&opens==before&&m.presented&&m.source_ready&&m.queue[0].remaining==4000);
 // Preserve the original text limits even with the longest URL/auth fields.
 setup();assert(push(99,"http://camera.test/image",1000)==MEDIA_OK);packet[15]=64;memset(packet+535,'t',64);memset(packet+599,'x',512);media_put32(packet+1,100);assert(media_command(&m,packet,1111)==MEDIA_OK&&m.last_push_id==99&&strlen(m.queue[0].text)==512);packet[599]=0xff;assert(media_command(&m,packet,1111)==MEDIA_RANGE);uint8_t status[MEDIA_STATUS_BYTES];assert(media_status(&m,status,sizeof(status))==sizeof(status)&&status[31]==3&&media_u32(status+32)==99&&status[MEDIA_STATUS_QUEUE+9]);
}
