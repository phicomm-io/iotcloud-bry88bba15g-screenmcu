#include "stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t jpeg[4096],body[8192],response[16384],image[MEDIA_JPEG_MAX];
static size_t jpeg_size,got;
static unsigned frames,operations;
static bool wanted(void *p){(void)p;return true;}
static void received(void *p,size_t n){(void)p;assert(n==jpeg_size&&!memcmp(image,jpeg,n));frames++;got=n;}
static bool send_wire(void *ctx,const uint8_t *p,size_t n){(void)ctx;assert(n>=5&&n<=517);assert(p[0]>=1&&p[0]<=3);operations++;return true;}
static bool decode(void *ctx,const uint8_t *p,size_t n,const media_jpeg *j,uint16_t *out,unsigned stride){(void)ctx;(void)p;(void)n;memset(out,0x55,stride*((j->height+15)&~15u)*2);return true;}
static screen_stream s;
static void event(unsigned op,uint32_t seq,const void *data,size_t n){uint8_t wire[517]={op};assert(n<=512);media_put32(wire+1,s.connection);if(n)memcpy(wire+5,data,n);stream_socket(&s,seq,wire,n+5);}
int main(void){
 FILE *f=fopen(MEDIA_FIXTURE,"rb");assert(f);jpeg_size=fread(jpeg,1,sizeof(jpeg),f);fclose(f);
 size_t pos=snprintf((char *)body,sizeof(body),"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",jpeg_size);
 memcpy(body+pos,jpeg,jpeg_size);pos+=jpeg_size;memcpy(body+pos,"\r\n--frame--\r\n",13);pos+=13;
 for(unsigned chunked=0;chunked<2;chunked++){
  size_t size=snprintf((char *)response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n%s\r\n",chunked?"Transfer-Encoding: chunked\r\n":"");
  if(chunked){for(size_t at=0;at<pos;){size_t n=pos-at>7?7:pos-at;size+=snprintf((char *)response+size,sizeof(response)-size,"%zx;ignored=ok\r\n",n);memcpy(response+size,body+at,n);size+=n;memcpy(response+size,"\r\n",2);size+=2;at+=n;}memcpy(response+size,"0\r\n\r\n",5);size+=5;}
  else {memcpy(response+size,body,pos);size+=pos;}
  for(size_t step=1;step<=1024;step*=2){mjpeg_parser parser={.image=image,.want=wanted,.frame=received};mjpeg_init(&parser);unsigned before=frames;
   for(size_t at=0;at<size;at+=step)mjpeg_feed(&parser,response+at,size-at>step?step:size-at);
   assert(parser.ended&&!parser.error&&frames==before+1&&got==jpeg_size);
  }
 }
 mjpeg_parser parser={.image=image,.want=wanted,.frame=received};mjpeg_init(&parser);
 const char *bad="HTTP/1.1 200 OK\r\nContent-Length: 131073\r\nContent-Type: image/jpeg\r\n\r\n";
 mjpeg_feed(&parser,(const uint8_t *)bad,strlen(bad));assert(parser.error);
 stream_init(&s);s.send=send_wire;s.decode=decode;uint8_t cfg[519]={MEDIA_STREAM_CONFIG,64,1,240,0,1,3};
 strcpy((char *)cfg+7,"http://camera.test/mjpeg?w={width}&h={height}&fps={fps}");strcpy((char *)cfg+391,"test");strcpy((char *)cfg+455,"secret");
 assert(stream_config(&s,cfg,sizeof(cfg))==MEDIA_OK);assert(!strcmp(s.path,"/mjpeg?w=320&h=240&fps=1"));
 stream_poll(&s,1,false,true);assert(!operations&&s.state==STREAM_PAUSED);
 stream_poll(&s,2,true,true);assert(s.state==STREAM_CONNECTING&&operations==1);
 event(MEDIA_NET_CONNECTED,0,NULL,0);assert(s.state==STREAM_READING&&strstr(s.request,"Authorization: Basic dGVzdDpzZWNyZXQ="));
 strcpy((char *)cfg+7,"http://bad@host/path");assert(stream_config(&s,cfg,sizeof(cfg))==MEDIA_RANGE);assert(s.state==STREAM_READING&&!strcmp(s.host,"camera.test"));
 const char *challenge="HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"room\", nonce=\"nonce\", qop=\"auth,auth-int\"\r\n\r\n";
 event(MEDIA_NET_WRITE,1,challenge,strlen(challenge));stream_poll(&s,3,true,true);assert(s.digest&&s.state==STREAM_CONNECTING);
 event(MEDIA_NET_CONNECTED,0,NULL,0);assert(strstr(s.request,"Authorization: Digest")&&strstr(s.request,"qop=auth")&&strstr(s.request,"response=\""));
 size_t size=snprintf((char *)response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",jpeg_size);memcpy(response+size,jpeg,jpeg_size);size+=jpeg_size;
 unsigned seq=0;for(size_t at=0;at<size;){size_t n=size-at>512?512:size-at;event(MEDIA_NET_WRITE,++seq,response+at,n);event(MEDIA_NET_WRITE,seq,response+at,n);at+=n;}
 assert(s.ready);stream_poll(&s,1003,true,true);assert(s.frames==1&&s.width==32&&s.height==17);
 stream_poll(&s,1004,false,true);assert(s.state==STREAM_PAUSED&&!s.request_size);
 stream_poll(&s,1005,true,true);assert(s.state==STREAM_CONNECTING);stream_poll(&s,17006,true,true);assert(s.state==STREAM_BACKOFF&&s.error==822);
 stream_poll(&s,17007,true,false);assert(s.state==STREAM_PAUSED);uint8_t status[40];assert(stream_status(&s,status,40)==40&&media_u32(status+8)==1);
}
