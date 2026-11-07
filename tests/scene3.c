#include "scene.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static void value(uint8_t *p,float f){uint32_t bits;memcpy(&bits,&f,4);scene_put32(p,bits);}
int main(void){
 screen_scene s,out;scene_defaults(&s);uint8_t bytes[SCENE_BYTES];
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes)));
 assert(out.schema==3&&out.page[5].id==6);
 s.item[12].hide_title=1;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[12].hide_title==1);
 bytes[304+288*12+83]=2;assert(!scene_decode(&out,bytes,sizeof(bytes)));
 s.item[12].hide_title=0;s.item[12].points=256;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[12].points==256);
 s.item[12].points=257;assert(!scene_validate(&s));s.item[12].points=0;

 s.item[9].value_mode=SC_VALUE_NUMBER;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[9].value_mode==SC_VALUE_NUMBER);
 s.item[9].value_mode=SC_VALUE_GRADE;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[9].value_mode==SC_VALUE_GRADE);
 s.item[9].value_mode=3;assert(!scene_validate(&s));s.item[9].value_mode=0;
 s.item[0].value_mode=SC_VALUE_NUMBER;assert(!scene_validate(&s));s.item[0].value_mode=0;
 s.item[1].value_mode=SC_VALUE_GRADE;assert(!scene_validate(&s));s.item[1].value_mode=0;
 unsigned external=0;while(external<s.count&&s.item[external].id!=16)external++;
 assert(external<s.count);
 strcpy(s.item[external].text,"通风提醒\n保持空气流动");
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes)));
 assert(!strcmp(out.item[external].text,s.item[external].text));
 screen_scene before=out;
 bytes[304+7]=1; /* Unreadable one-cell widget. */
 assert(!scene_decode(&out,bytes,sizeof(bytes))&&!memcmp(&out,&before,sizeof(out)));
 s.page[1].id=s.page[0].id;assert(!scene_validate(&s));s.page[1].id=2;
 s.item[external].type=SC_LINE;s.item[external].h=6;s.item[external].w=4;
 for(unsigned k=0;k<=SC_CH_RING;k++){
  s.item[external].chart=k;s.item[external].additive=k>=SC_CH_STACK_BAR;
  assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes)));
 }
 /* Full-width one-row readouts and compact charts preserve the SCN3 wire. */
 s.item[external].h=2;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes)));
 s.item[external].h=1;assert(!scene_validate(&s));
 s.item[external].type=SC_VALUE;s.item[external].value_mode=SC_VALUE_NUMBER;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes)));
 s.item[external].w=3;assert(!scene_validate(&s));
 s.item[external].w=2;
 assert(scene_requires_compact(&s.item[external]));
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[external].w==2&&out.item[external].h==1);
 s.item[external].value_mode=0;s.item[external].w=4;s.item[external].type=SC_CLOCK;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[external].h==1);
 s.item[external].type=SC_WEATHER;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[external].h==1);
 s.item[external].h=2;
 assert(scene_encode(&s,bytes)&&scene_decode(&out,bytes,sizeof(bytes))&&out.item[external].h==2);
 s.item[external].h=1;s.item[external].w=2;assert(!scene_validate(&s));
 s.item[external].w=4;s.item[external].h=6;s.item[external].type=SC_LINE;s.item[external].value_mode=0;
 s.item[external].binding=SC_BIND_TEMP;assert(!scene_validate(&s));
 s.item[external].binding=SC_BIND_EXTERNAL;
 assert(scene_encode(&s,bytes));
 memcpy(bytes,"SCN2",4);assert(!scene_decode(&out,bytes,sizeof(bytes)));
 s.schema=2;assert(!scene_validate(&s)&&!scene_encode(&s,bytes));s.schema=3;
 /* Null masks preserve gaps; all malformed updates leave the prior value. */
 uint8_t data[12+2*(48+1+3*4)]={SC_MSG_SERIES,1,16,0,2,3};
 scene_put32(data+6,60);
 for(unsigned k=0;k<2;k++){
  uint8_t *q=data+12+k*61;strcpy((char*)q,k?"Lighting":"Cooling");
  strcpy((char*)q+32,"W");q[48]=5;
  value(q+49,12+k);value(q+53,0);value(q+57,25+k);
 }
 scene_data d={0};assert(scene_series_receive(&d,data,sizeof(data),UINT32_MAX-100));
 assert(d.series_count==2&&d.masks[0]==5&&d.series[1][2]==26&&scene_data_fresh(&d,100));
 scene_data saved=d;data[12+48]=0x80;
 assert(!scene_series_receive(&d,data,sizeof(data),0)&&!memcmp(&d,&saved,sizeof(d)));
 data[12+48]=5;value(data+12+49,NAN);
 assert(!scene_series_receive(&d,data,sizeof(data),0));value(data+12+49,12);
 data[12]=0xc0;data[13]=0x80;assert(!scene_series_receive(&d,data,sizeof(data),0));
 assert(!scene_series_receive(&d,data,sizeof(data)-1,0));
 return 0;
}
