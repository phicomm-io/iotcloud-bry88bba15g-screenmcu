#include "sensor_rx.h"
void sensor_rx_clear(sensor_rx *q) {
  q->read=q->write=0;q->overflow=false;q->started=false;q->at=0;
}
void sensor_rx_push(sensor_rx *q,uint8_t byte,uint32_t now) {
  if(q->overflow)return;
  unsigned next=(q->write+1u)&(SENSOR_RX_SIZE-1u);
  if(next==q->read){q->overflow=true;return;}
  unsigned gap=q->started&&(uint32_t)(now-q->at)>100u?SENSOR_RX_GAP:0;
  q->bytes[q->write]=byte|gap;
  q->at=now;q->started=true;q->write=next;
}
int sensor_rx_pop(sensor_rx *q) {
  if(q->overflow)return -2; /* Discard the whole burst, never splice a frame. */
  if(q->read==q->write)return -1;
  int byte=q->bytes[q->read];q->read=(q->read+1u)&(SENSOR_RX_SIZE-1u);
  return byte;
}
unsigned sensor_rx_parse(sensor_rx_frame *f,int entry) {
  if(entry<0){if(entry==-2)f->used=0;return 0;}
  if(entry&SENSOR_RX_GAP)f->used=0;
  uint8_t byte=entry;
  if(!f->used&&byte!=0x16)return 0;
  f->bytes[f->used++]=byte;
  if(f->used==2&&(f->bytes[1]<3||f->bytes[1]>29))f->used=0;
  if(f->used>=3&&f->used==(unsigned)f->bytes[1]+3) {
    unsigned n=f->used;f->used=0;return n;
  }
  return 0;
}
