#include "media.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static screen_media m;
static uint8_t p[MEDIA_PACKET_MAX];
static unsigned send(unsigned id,unsigned ttl,unsigned overflow) {
  memset(p,0,sizeof(p));p[0]=MEDIA_PUSH_POLICY;media_put32(p+1,id);
  media_put32(p+9,10000);p[13]=3;p[14]=30;
  media_put32(p+16,ttl);p[20]=overflow;p[22]='X';
  return media_command(&m,p,23);
}
static unsigned state(unsigned id) {
  for(unsigned i=0;i<MEDIA_HISTORY;i++)if(m.history[i].id==id)return m.history[i].state;
  return 0;
}
int main(void) {
  media_init(&m);media_tick(&m,100,false,false);
  assert(send(1,1000,0)==MEDIA_OK);media_tick(&m,101,false,false);media_present(&m);
  assert(m.active==1&&m.queue[0].confirm);
  assert(send(2,500,0)==MEDIA_OK);assert(media_select(&m,0)==MEDIA_OK);
  media_tick(&m,601,true,true);
  assert(media_count(&m)==1&&state(2)==PUSH_EXPIRED&&m.parked);
  media_tick(&m,1100,true,true);
  assert(!media_count(&m)&&state(1)==PUSH_EXPIRED);
  media_tick(&m,1101,false,false);
  /* Expired IDs retain their creation tombstone and cannot replay. */
  assert(send(1,1000,0)==MEDIA_OK&&!media_count(&m));
  media_init(&m);media_tick(&m,UINT32_MAX-300,false,false);
  assert(send(1,1000,0)==MEDIA_OK);media_tick(&m,199,false,false);
  assert(m.queue[0].ttl_remaining==500);media_tick(&m,699,false,false);
  assert(!media_count(&m)&&state(1)==PUSH_EXPIRED);
  /* Hard lifetime can be shortened/reset/disabled explicitly by ID. */
  assert(send(2,1000,0)==MEDIA_OK);memset(p,0,sizeof(p));
  p[0]=MEDIA_PUSH_UPDATE;media_put32(p+1,2);p[17]=MEDIA_UPDATE_TTL>>8;
  media_put32(p+18,200);assert(media_command(&m,p,22)==MEDIA_OK);
  media_tick(&m,799,false,false);assert(m.queue[0].ttl_remaining==100);
  media_put32(p+18,0);assert(media_command(&m,p,22)==MEDIA_OK);
  media_tick(&m,2000,false,false);assert(media_count(&m)==1);
  media_put32(p+18,MEDIA_TTL_MAX_MS+1);assert(media_command(&m,p,22)==MEDIA_RANGE);
  assert(!m.queue[0].ttl_remaining);
  /* Reject is retryable; drop_newest is a successful deliberate discard. */
  media_init(&m);for(unsigned id=1;id<=16;id++)assert(send(id,0,0)==MEDIA_OK);
  assert(media_select(&m,1)==MEDIA_OK);media_present(&m);
  assert(send(99,0,0)==MEDIA_BUSY&&media_count(&m)==16&&!state(99));
  assert(send(99,0,MEDIA_OVERFLOW_DROP_NEWEST)==MEDIA_OK);
  assert(media_count(&m)==16&&m.active==1&&state(99)==PUSH_DROPPED);
  assert(send(99,0,MEDIA_OVERFLOW_DROP_NEWEST)==MEDIA_OK&&media_count(&m)==16);
  assert(send(100,0,MEDIA_OVERFLOW_DROP_OLDEST)==MEDIA_OK);
  assert(media_count(&m)==16&&m.active==1&&state(2)==PUSH_EVICTED);
  assert(media_at(&m,1)->id==1&&media_at(&m,2)->id==3&&media_at(&m,16)->id==100);
  /* A selected later item is also protected; parked queues evict the oldest. */
  assert(media_select(&m,4)==MEDIA_OK);unsigned active=m.active;
  assert(send(101,0,MEDIA_OVERFLOW_DROP_OLDEST)==MEDIA_OK&&m.active==active&&state(1)==PUSH_EVICTED);
  assert(media_select(&m,0)==MEDIA_OK);
  assert(send(102,0,MEDIA_OVERFLOW_DROP_OLDEST)==MEDIA_OK&&!m.active&&state(3)==PUSH_EVICTED);
  uint8_t bytes[MEDIA_STATUS_BYTES];assert(media_status(&m,bytes,sizeof(bytes))==sizeof(bytes)&&bytes[31]==4);
  assert(send(103,MEDIA_TTL_MAX_MS+1,0)==MEDIA_RANGE);
  assert(send(103,0,255)==MEDIA_RANGE&&media_count(&m)==16);
  puts("PASS hard expiry while hidden/suspended/confirm, clock wrap, TTL patch, overflow decisions and active protection");
}
