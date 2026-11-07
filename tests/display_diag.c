#include "display_diag.h"
#include "scene.h"
#include <assert.h>
#include <string.h>
int main(void){
  screen_display_diag d={0};uint8_t out[SCREEN_DIAG_REPLY_MAX],req[16]={SCREEN_MSG_DIAG_REQUEST,1,1,0,1};
  assert(screen_diag_request_valid(req,sizeof(req)));req[12]=1;
  assert(!screen_diag_request_valid(req,sizeof(req)));req[12]=0;req[3]=2;req[11]=9;
  assert(!screen_diag_request_valid(req,sizeof(req)));req[11]=8;
  assert(screen_diag_request_valid(req,sizeof(req)));
  screen_diag_begin(&d,2,UINT32_MAX-100);
  for(unsigned i=0;i<22;i++){
    uint32_t start=UINT32_MAX-90+i*1000;
    screen_diag_flush(&d,0,i*40,479,i==21?853:(int)(i+1)*40-1,start,start+40,i==21);
  }
  screen_diag_monitor(&d,25,480*854,26000);
  assert(d.flush_count==22&&d.flush_us==880&&d.flush_max_us==40);
  assert(d.trace[1].offset_us==1010&&d.trace[21].y2==853&&d.trace[21].last);
  assert(d.monitor_count==1&&d.monitor_ms==25&&d.monitor_pixels==480*854);
  screen_diag_poll(&d,145999);assert(d.active);
  screen_diag_poll(&d,146000);assert(!d.active&&d.elapsed_us==26101);
  assert(screen_diag_encode(&d,37,0,16,8,out,sizeof(out))==128+7*24);
  assert(scene_u32(out+4)==37&&scene_u32(out+8)==1&&scene_u32(out+52)==16&&scene_u32(out+56)==7);
  assert(out[128]==1&&out[128+5*24+1]==1&&out[128+6*24]==2);
  screen_diag_begin(&d,3,0);
  for(unsigned i=0;i<SCREEN_DIAG_TRACE+15;i++)screen_diag_flush(&d,0,0,479,39,i*200,i*200+10,false);
  assert(d.stored==SCREEN_DIAG_TRACE&&d.dropped==15&&d.flush_count==111&&d.flush_us==1110);
  assert(!screen_diag_encode(&d,1,0,97,1,out,sizeof(out)));
  assert(!screen_diag_encode(&d,1,0,0,9,out,sizeof(out)));
  assert(!screen_diag_encode(&d,1,0,0,8,out,128));
  d.active=false;screen_diag_flush(&d,0,0,1,1,0,500,true);assert(d.flush_count==111);
  return 0;
}
