#include "wallpaper.h"
#include "scene.h"
#include <string.h>
screen_wallpaper wallpaper;
static const uint8_t owner[16]={'I','O','T','W','P','B','A','N','K','0','1',0,0,0,0,0};
static uint32_t crc_more(uint32_t c,const uint8_t *p,size_t n) {
 while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return c;
}
bool wallpaper_header(const uint8_t *p,size_t n) {
 return n==32&&!memcmp(p,"IOTW5651",8)&&p[12]==240&&!p[13]&&p[14]==171&&p[15]==1&&
   scene_u32(p+20)==WALLPAPER_BYTES&&!scene_u32(p+24)&&scene_u32(p+28)==scene_crc32(p,28);
}
static bool read_at(screen_wallpaper *w,uint32_t a,void *p,size_t n) {
 return w->read&&w->read(w->context,a,p,n);
}
static bool timed(screen_wallpaper *w,uint32_t start) {
 return w->now&&(uint32_t)(w->now(w->context)-start)>WALLPAPER_IO_DEADLINE;
}
static void stop(screen_wallpaper *w) {
 if(w->active&&w->activity)w->activity(w->context,false);
 w->active=w->preparing=false;
}
static bool owned(screen_wallpaper *w,uint32_t bank) {
 uint8_t p[20];return read_at(w,bank,p,sizeof(p))&&!memcmp(p,owner,16)&&scene_u32(p+16)==scene_crc32(owner,16);
}
/* Scan at most one 512-byte block per foreground turn. Never pump the UART
 * recursively from a receive callback or block rendering for a full bank. */
static void prepare(screen_wallpaper *w) {
 uint8_t p[512];
 if(!w->prepare_at&&owned(w,w->target))w->prepare_at=WALLPAPER_BANK;
 if(w->prepare_at<WALLPAPER_BANK) {
  if(!read_at(w,w->target+w->prepare_at,p,sizeof(p)))goto failed;
  for(unsigned i=0;i<sizeof(p);i++)if(p[i]!=255)goto failed;
  w->prepare_at+=sizeof(p);
  if(timed(w,w->prepare_started)){stop(w);w->state=RESOURCE_TIMEOUT;return;}
  return;
 }
 if(!owned(w,w->target)) {
  memcpy(p,owner,16);scene_put32(p+16,scene_crc32(owner,16));
  if(!w->write||!w->write(w->context,w->target,p,20)||!owned(w,w->target))goto failed;
 }
 if(!w->erase||!w->erase(w->context,w->target+4096,4096))goto failed;
 w->preparing=false;w->deadline=(w->now?w->now(w->context):0)+15000;return;
failed:
 stop(w);w->state=RESOURCE_IO;
}
void wallpaper_init(screen_wallpaper *w) {
 w->ready=w->active=w->pending=w->preparing=w->reading=false;w->kind=0;w->initialized=true;w->generation=0;w->crc=0;
 w->state=RESOURCE_ABSENT;w->bank=WALLPAPER_BASE;
 for(unsigned i=0;i<2;i++) {
  uint32_t bank=WALLPAPER_BASE+i*WALLPAPER_BANK;uint8_t h[32];
  if(!owned(w,bank)||!read_at(w,bank+4096,h,sizeof(h))||!wallpaper_header(h,sizeof(h)))continue;
  uint32_t generation=scene_u32(h+8);
  if(generation&&generation>w->generation){w->generation=generation;w->bank=bank;w->crc=scene_u32(h+16);w->pending=true;}
 }
}
static void poll_inner(screen_wallpaper *w) {
 uint32_t now=w->now?w->now(w->context):0;
 if(w->active&&(int32_t)(now-w->deadline)>=0){stop(w);w->state=RESOURCE_TIMEOUT;}
 if(w->active){if(w->preparing)prepare(w);return;}
 if(w->wanted!=w->kind) {
  w->kind=w->wanted;w->ready=w->reading=false;w->load_at=0;
  if(w->kind==4)w->pending=true;
 }
 /* A bounded batch leaves UART reception to the foreground loop. Pumping it
  * recursively while loading rejected an upload sent just after a preset. */
 if(w->kind&&w->kind<4&&!w->ready&&w->pixels) {
   unsigned end=w->load_at+4;if(end>854)end=854;
   for(unsigned y=w->load_at;y<end;y++) {
    for(unsigned x=0;x<480;x++) {
     uint32_t c=wallpaper_preset(w->kind,x,y);
     w->pixels[y*480+x]=((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31);
    }
   }
   w->load_at=end;w->ready=end==854;return;
 }
 if(w->pending&&w->kind==4&&!w->reading) {
  w->pending=false;w->ready=false;w->state=RESOURCE_UNCHECKED;
  if(!w->pixels){w->state=RESOURCE_INVALID;return;}
  w->reading=true;w->load_at=0;w->load_crc=~0u;w->load_started=now;
 }
 if(!w->reading)return;
 uint8_t row[WALLPAPER_WIDTH*2];unsigned y=w->load_at;
 if(!read_at(w,w->bank+8192+y*sizeof(row),row,sizeof(row))) {
  w->reading=false;w->state=RESOURCE_IO;return;
 }
 if(timed(w,w->load_started)){w->reading=false;w->state=RESOURCE_TIMEOUT;return;}
 w->load_crc=crc_more(w->load_crc,row,sizeof(row));
 for(unsigned x=0;x<WALLPAPER_WIDTH;x++) {
  uint16_t v=row[x*2]|row[x*2+1]<<8;unsigned at=y*2*480+x*2;
  w->pixels[at]=w->pixels[at+1]=w->pixels[at+480]=w->pixels[at+481]=v;
 }
 w->load_at++;
 if(w->service)w->service(w->context);
 if(w->load_at<WALLPAPER_HEIGHT)return;
 w->reading=false;
 if(~w->load_crc!=w->crc){w->state=RESOURCE_INVALID;return;}
 w->ready=true;w->state=RESOURCE_READY;
}
void wallpaper_poll(screen_wallpaper *w) {
 if(w->loading)return;
 w->loading=true;poll_inner(w);w->loading=false;
}
bool wallpaper_receive(screen_wallpaper *w,const uint8_t *p,size_t n,uint8_t reply[20]) {
 if(n<12||p[0]!=SCREEN_MSG_WALLPAPER||p[1]!=1||p[2]>WP_CANCEL||p[3])return false;
 unsigned op=p[2];bool ok=false;uint32_t now=w->now?w->now(w->context):0;
 if(op==WP_QUERY)ok=n==12;
 else if(op==WP_CANCEL&&n==12){stop(w);ok=true;}
 else if(op==WP_BEGIN&&n==44&&!w->active&&!w->loading&&!w->reading&&wallpaper_header(p+12,32)&&w->generation<UINT32_MAX) {
  w->target=w->generation?(w->bank==WALLPAPER_BASE?WALLPAPER_BASE+WALLPAPER_BANK:WALLPAPER_BASE):WALLPAPER_BASE;
  if(w->activity)w->activity(w->context,true);
  w->active=true;
  memcpy(w->header,p+12,32);w->expected=scene_u32(p+28);
  w->received=w->erased=w->prepare_at=0;w->running=~0u;
  w->preparing=true;w->prepare_started=now;w->deadline=now+WALLPAPER_IO_DEADLINE;ok=true;
 } else if(op==WP_CHUNK&&w->active&&!w->preparing&&n>12&&n<=524&&scene_u32(p+8)==w->received&&n-12<=WALLPAPER_BYTES-w->received) {
  uint32_t end=w->received+n-12;ok=true;
  while(ok&&w->erased<end) {
   ok=w->erase&&w->erase(w->context,w->target+8192+w->erased,4096);w->erased+=4096;
  }
  uint8_t check[512];uint32_t at=w->target+8192+w->received;
  ok=ok&&w->write&&w->write(w->context,at,p+12,n-12)&&read_at(w,at,check,n-12)&&!memcmp(check,p+12,n-12);
  if(ok){w->running=crc_more(w->running,p+12,n-12);w->received=end;w->deadline=now+15000;}
 } else if(op==WP_COMMIT&&n==12&&w->active&&!w->preparing&&w->received==WALLPAPER_BYTES&&~w->running==w->expected) {
  /* Commit the image header last. A power cut leaves the other bank valid. */
  scene_put32(w->header+8,w->generation+1);scene_put32(w->header+28,scene_crc32(w->header,28));
  uint8_t check[32];
  ok=w->write&&w->write(w->context,w->target+4096,w->header,32)&&read_at(w,w->target+4096,check,32)&&!memcmp(check,w->header,32);
  if(ok){w->generation++;w->bank=w->target;w->crc=w->expected;w->pending=true;stop(w);}
 }
 if(!ok&&op!=WP_QUERY){stop(w);w->state=RESOURCE_IO;}
 memset(reply,0,20);reply[0]=SCREEN_MSG_WALLPAPER_STATUS;reply[1]=1;reply[2]=ok?0:1;
 reply[3]=w->preparing?WALLPAPER_PREPARING:w->active?6:w->state;scene_put32(reply+4,scene_u32(p+4));
 scene_put32(reply+8,w->received);scene_put32(reply+12,w->crc);scene_put32(reply+16,w->generation);
 return true;
}
static uint32_t mix(uint32_t a,uint32_t b,unsigned t,unsigned scale) {
 uint32_t c=0;for(unsigned k=0;k<24;k+=8)c|=((((a>>k)&255)*(scale-t)+((b>>k)&255)*t)/scale)<<k;return c;
}
static int ridge(unsigned x,const int *p,unsigned n) {
 for(unsigned i=1;i<n;i++)if((int)x<=p[i*2])return p[i*2-1]+(p[i*2+1]-p[i*2-1])*((int)x-p[(i-1)*2])/(p[i*2]-p[(i-1)*2]);
 return p[n*2-1];
}
uint32_t wallpaper_preset(unsigned kind,unsigned x,unsigned y) {
 if(kind==1) {
  uint32_t c=mix(0x6d9391,0x102e36,y,854);
  if(((int)x-355)*((int)x-355)+((int)y-153)*((int)y-153)<42*42)c=mix(c,0xe4dfbc,6,10);
  const int a[]={0,374,95,265,151,313,261,212,380,351,480,293},b[]={0,517,110,385,213,480,327,339,480,504},d[]={0,589,139,510,266,639,356,565,480,596};
  if((int)y>ridge(x,a,6))c=0x557777;
  if((int)y>ridge(x,b,5))c=0x30545b;
  if((int)y>ridge(x,d,5))c=0x193b43;
  int r=(int)x-240;if((int)y>690+r*r/750)c=0x102b34;return c;
 }
 if(kind==2) {
  uint32_t c=mix(0x10233b,0x171c34,x+y,1334);int center=125+(int)y/4+((int)y-400)*((int)y-400)/3000;
  int d=(int)x-center;if(d<0)d=-d;unsigned t=d<125?(125-d)*65/125:0;
  return mix(c,y<430?0x75c6bb:0x8e85bd,t,100);
 }
 uint32_t c=mix(0x55485d,0xd7ae86,y,854);
 if(((int)x-349)*((int)x-349)+((int)y-217)*((int)y-217)<49*49)c=mix(c,0xeed5ad,75,100);
 int r=(int)x-240;if((int)y>400+r*r/600)c=0xb48a74;
 if(y>480+x/3)c=mix(0xc69d77,0x66504e,y-480,374);
 if((int)y>650+r*r/750)c=0x775754;
 if(y>749+x/5)c=0x4c3b42;
 return c;
}
