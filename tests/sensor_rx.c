#include "sensor_rx.h"
#include "screen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void checksum(uint8_t *p,unsigned n) {
  uint8_t sum=0;for(unsigned i=0;i<n-1;i++)sum+=p[i];p[n-1]=-sum;
}
static void burst(sensor_rx *q,const uint8_t *p,unsigned n,uint32_t at) {
  for(unsigned i=0;i<n;i++)sensor_rx_push(q,p[i],at+i);
}
static unsigned receive(sensor_rx *q,sensor_rx_frame *frame,unsigned cmd,uint16_t *value) {
  int byte;unsigned count=0;
  while((byte=sensor_rx_pop(q))>=0) {
    unsigned n=sensor_rx_parse(frame,byte);
    if(n&&sensor_uart_value(frame->bytes,n,cmd,value))count++;
  }
  return count;
}
int main(void) {
  sensor_rx q[3]={0};sensor_rx_frame f[3]={0};uint16_t value;
  uint8_t co2[]={0x16,5,1,2,0x51,0,0,0};checksum(co2,sizeof(co2));
  uint8_t air[]={0x16,9,0x16,0,0x7b,0,0,0,0,0x05,0xc2,0};checksum(air,sizeof(air));
  /* Simulate no foreground reads during a long NOR/render transaction. IRQs
   * receive independent ports; the real checksum/parser still sees full data. */
  burst(&q[0],co2,sizeof(co2),1000);burst(&q[1],air,sizeof(air),1002);
  assert(receive(&q[0],&f[0],1,&value)==1&&value==593);
  assert(receive(&q[1],&f[1],0x16,&value)==1&&value==123);
  assert(((unsigned)f[1].bytes[9]<<8|f[1].bytes[10])==1474);
  assert(sensor_rx_pop(&q[2])==-1);
  /* A wire gap must break a frame even if its two halves are consumed in the
   * same foreground iteration. Poll-time timestamps cannot detect this. */
  burst(&q[0],co2,4,1100);burst(&q[0],co2+4,4,1400);
  assert(!receive(&q[0],&f[0],1,&value));
  burst(&q[0],co2,sizeof(co2),1800);assert(receive(&q[0],&f[0],1,&value)==1);
  /* Partial frame survives delayed main-loop polls when the actual wire gap
   * is short, including uint32 clock wrap. */
  sensor_rx_clear(&q[0]);memset(&f[0],0,sizeof(f[0]));
  burst(&q[0],co2,4,0xfffffffc);assert(!receive(&q[0],&f[0],1,&value));
  burst(&q[0],co2+4,4,0);assert(receive(&q[0],&f[0],1,&value)==1);
  /* Overflow latches until the whole burst and parser are discarded. */
  burst(&q[0],co2,4,500);assert(!receive(&q[0],&f[0],1,&value));
  for(unsigned i=0;i<400;i++)sensor_rx_push(&q[0],i,510+i);
  assert(sensor_rx_pop(&q[0])==-2);sensor_rx_parse(&f[0],-2);assert(!f[0].used);
  sensor_rx_clear(&q[0]);burst(&q[0],co2,sizeof(co2),1200);
  assert(receive(&q[0],&f[0],1,&value)==1&&value==593);
  /* Repeated wrap of ring indices; corrupt checksum never yields a value. */
  for(unsigned i=0;i<1000;i++) {
    burst(&q[1],air,sizeof(air),2000+20*i);
    assert(receive(&q[1],&f[1],0x16,&value)==1&&value==123);
  }
  air[11]++;burst(&q[1],air,sizeof(air),30000);assert(!receive(&q[1],&f[1],0x16,&value));
  puts("PASS sensor UART buffering, wire gaps, overflow recovery and real frame checksums");
}
