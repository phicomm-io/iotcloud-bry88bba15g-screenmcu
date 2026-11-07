#include "widgets.h"
#include "runtime.h"
#include "../simulator/widget_fixtures.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static screen_runtime r;
static unsigned sent,old;
static bool send_line(void *ctx,const char *p,size_t n){(void)ctx;assert(n==33&&!memcmp(p,"screen_caps 1 ",14));sent++;return true;}
static void factory(void *ctx,const char *p,size_t n){(void)ctx;assert(n==13&&!memcmp(p,"+version:old!",13));old++;}
int main(void){
 screen_widgets w={0};assert(screen_widget_apply(&w,demo_widget_0,sizeof(demo_widget_0),UINT32_MAX-100));
 assert(screen_widget_valid(&w.items[1],100));assert(!screen_widget_valid(&w.items[1],300000));
 screen_widgets saved=w;
 for(size_t i=0;i<sizeof(demo_widget_0);i++){
  assert(!screen_widget_apply(&w,demo_widget_0,i,0));assert(!memcmp(&w,&saved,sizeof(w)));
 }
 unsigned char bad[768];memcpy(bad,demo_widget_0,sizeof(demo_widget_0));bad[2]=2;assert(!screen_widget_apply(&w,bad,sizeof(demo_widget_0),0));
 memcpy(bad,demo_widget_0,sizeof(demo_widget_0));bad[sizeof(demo_widget_0)]=0;assert(!screen_widget_apply(&w,bad,sizeof(demo_widget_0)+1,0));
 /* Known float32 NaN and infinity must not escape into LVGL geometry. */
 memcpy(bad,demo_widget_0,sizeof(demo_widget_0));memcpy(bad+sizeof(demo_widget_0)-4,"\x7f\xc0\x00\x00",4);assert(!screen_widget_apply(&w,bad,sizeof(demo_widget_0),0));
 memcpy(bad+sizeof(demo_widget_0)-4,"\x7f\x80\x00\x00",4);assert(!screen_widget_apply(&w,bad,sizeof(demo_widget_0),0));
 assert(screen_widget_apply(&w,demo_widget_1,sizeof(demo_widget_1),0));assert(w.items[1].type==1);
 assert(screen_widget_apply(&w,demo_widget_2,sizeof(demo_widget_2),0));assert(w.items[1].count==6);
 assert(screen_widget_apply(&w,demo_widget_3,sizeof(demo_widget_3),0));assert(w.items[1].type==3);
 assert(screen_widget_apply(&w,demo_widget_4,sizeof(demo_widget_4),0));assert(w.items[1].type==4);
 /* Zero-weight pie and malformed UTF8 are rejected transactionally. */
 memcpy(bad,demo_widget_4,sizeof(demo_widget_4));for(size_t i=sizeof(demo_widget_4)-20;i<sizeof(demo_widget_4);i+=5)memset(bad+i+1,0,4);
 assert(!screen_widget_apply(&w,bad,sizeof(demo_widget_4),0));
 memcpy(bad,demo_widget_0,sizeof(demo_widget_0));bad[11]=0xc0;assert(!screen_widget_apply(&w,bad,sizeof(demo_widget_0),0));
 r.send=send_line;r.factory=factory;screen_runtime_init(&r);
 char wire[1536];size_t n=screen_link_encode(wire,sizeof(wire),123,1,demo_widget_0,sizeof(demo_widget_0));
 screen_runtime_feed(&r,wire,n,0);assert(!r.model.external.items[1].valid);
 assert(!screen_runtime_offer(&r,0));assert(screen_runtime_offer(&r,123));assert(sent==1);assert(!screen_runtime_offer(&r,456));
 char ack[80];size_t a=screen_caps_ack(ack,sizeof(ack),124,1);screen_runtime_feed(&r,ack,a,0);assert(!r.caps.ready);
 a=screen_caps_ack(ack,sizeof(ack),123,1);for(size_t i=0;i<a;i++)screen_runtime_feed(&r,ack+i,1,0);assert(r.caps.ready);
 for(size_t i=0;i<n;i++)screen_runtime_feed(&r,wire+i,1,100);assert(r.model.external.items[1].valid&&r.last_sequence==1);
 screen_runtime_feed(&r,wire,n,200);assert(r.model.external.items[1].at==100);
 screen_runtime_feed(&r,"+version:old!\r\n",15,0);assert(old==1);
 screen_sample_set(&r.model,MET_CO2,600,true,100);
 screen_runtime_tick(&r,301000);assert(r.model.external.items[1].expired);
 assert(!screen_widget_valid(&r.model.external.items[1],100));
 assert(!screen_sample_valid(&r.model,MET_CO2,100));
 screen_runtime_init(&r);assert(!r.caps.ready&&!r.model.external.items[1].valid);
 puts("widgets: transactional validation, expiration, UTF8, all chart types and negotiated RX passed");
}
