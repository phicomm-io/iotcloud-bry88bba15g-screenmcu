#include "display_diag.h"
#include "scene.h"
#include <string.h>
void screen_diag_begin(screen_display_diag *d,unsigned page,uint32_t now) {
  uint32_t generation=d->generation+1;
  memset(d,0,sizeof(*d));d->generation=generation?generation:1;
  d->active=d->enabled=true;d->page=page;d->started_us=d->last_us=now;
}
static screen_diag_record *record(screen_display_diag *d) {
  if(d->stored<SCREEN_DIAG_TRACE)return &d->trace[d->stored++];
  ++d->dropped;return NULL;
}
void screen_diag_flush(screen_display_diag *d,int x1,int y1,int x2,int y2,
                      uint32_t start,uint32_t end,bool last) {
  if(!d->active)return;
  uint32_t duration=end-start;++d->flush_count;d->flush_us+=duration;
  if(duration>d->flush_max_us)d->flush_max_us=duration;
  d->last_us=end;d->elapsed_us=end-d->started_us;
  screen_diag_record *r=record(d);if(!r)return;
  *r=(screen_diag_record){.offset_us=start-d->started_us,.duration_us=duration,
       .pixels=(uint32_t)(x2-x1+1)*(uint32_t)(y2-y1+1),.x1=x1,.y1=y1,.x2=x2,.y2=y2,
       .kind=1,.last=last};
}
void screen_diag_monitor(screen_display_diag *d,uint32_t ms,uint32_t pixels,uint32_t now) {
  if(!d->active)return;
  ++d->monitor_count;d->monitor_ms+=ms;d->monitor_pixels+=pixels;
  d->last_us=now;d->elapsed_us=now-d->started_us;
  screen_diag_record *r=record(d);if(!r)return;
  *r=(screen_diag_record){.offset_us=now-d->started_us,.duration_us=ms*1000,
                         .pixels=pixels,.kind=2};
}
void screen_diag_poll(screen_display_diag *d,uint32_t now) {
  /* Called after the synchronous handler returns, not between render chunks.
   * The quiet interval collects follow-up rounds without adding it to latency. */
  if(d->active&&(uint32_t)(now-d->last_us)>=120000)d->active=false;
}
size_t screen_diag_encode(screen_display_diag *d,uint32_t request,unsigned status,
                         unsigned offset,unsigned limit,uint8_t *out,size_t cap) {
  if(limit>SCREEN_DIAG_BATCH||offset>SCREEN_DIAG_TRACE||cap<SCREEN_DIAG_SUMMARY_BYTES)return 0;
  unsigned count=offset<d->stored?d->stored-offset:0;if(count>limit)count=limit;
  size_t n=SCREEN_DIAG_SUMMARY_BYTES+count*SCREEN_DIAG_RECORD_BYTES;if(n>cap)return 0;
  memset(out,0,n);out[0]=SCREEN_MSG_DIAG_REPLY;out[1]=1;out[2]=status;
  out[3]=(d->active?1:0)|(d->waiting?2:0)|(d->enabled?4:0);
  uint32_t values[]={request,d->generation,d->page,d->elapsed_us,d->flush_count,
      d->flush_us,d->flush_max_us,d->monitor_count,d->monitor_ms,d->monitor_pixels,
      d->stored,d->dropped,offset,count,40};
  for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++)scene_put32(out+4+4*i,values[i]);
  for(unsigned i=0;i<count;i++){
    screen_diag_record *r=&d->trace[offset+i];uint8_t *p=out+SCREEN_DIAG_SUMMARY_BYTES+i*SCREEN_DIAG_RECORD_BYTES;
    p[0]=r->kind;p[1]=r->last;p[2]=(offset+i)&255;p[3]=(offset+i)>>8;
    int16_t xy[]={r->x1,r->y1,r->x2,r->y2};
    for(unsigned k=0;k<4;k++){p[4+2*k]=xy[k]&255;p[5+2*k]=(uint16_t)xy[k]>>8;}
    scene_put32(p+12,r->offset_us);scene_put32(p+16,r->duration_us);scene_put32(p+20,r->pixels);
  }
  return n;
}
