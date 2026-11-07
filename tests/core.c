#include "screen.h"
#include "ota.h"
#include "screen_link.h"
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
static screen_model m;
int main(void){
 screen_init(&m);assert(!screen_sample_valid(&m,0,0));m.asleep=true;screen_event(&m,1);assert(!m.asleep&&m.page==0);
 for(unsigned i=0;i<m.scene.pages;i++)screen_event(&m,1);assert(m.page==0);screen_event(&m,1);screen_event(&m,2);assert(m.page==0);
 screen_sample_set(&m,0,23,true,UINT32_MAX-100);assert(screen_sample_valid(&m,0,5));assert(!screen_sample_valid(&m,0,20000));
 screen_sample_set(&m,0,NAN,true,0);assert(!screen_sample_valid(&m,0,0));
 for(unsigned i=0;i<1500;i++){screen_sample_set(&m,2,600,true,i*60000);screen_record(&m,i*60000);screen_record(&m,i*60000);}
 assert(m.count==1440);assert(m.history[m.head].minute==60);
 uint8_t f[]={0x16,5,1,2,0x58,0,0,0x8a};uint16_t value=0;assert(sensor_uart_value(f,8,1,&value)&&value==600);f[7]++;assert(!sensor_uart_value(f,8,1,&value));assert(!sensor_uart_value(f,4,1,&value));
 const uint8_t sht[]={0xbe,0xef,0x92,0xbe,0xef,0x92};float t,h;assert(sensor_sht3x(sht,&t,&h));assert(t>85&&t<86&&h>74&&h<75);
 assert(battery_permille(3000)==0&&battery_permille(4185)==1000&&battery_permille(3662)==500);
 screen_ota ota={0};uint8_t block[1024]={0};assert(!screen_ota_begin(&ota,SCREEN_APP_LIMIT+16));assert(screen_ota_begin(&ota,2048));assert(!screen_ota_accept(&ota,1024,block,1024));assert(screen_ota_accept(&ota,0,block,1024));assert(!screen_ota_verify(&ota,true));assert(!screen_ota_accept(&ota,0,block,1024));assert(screen_ota_accept(&ota,1024,block,1024));assert(!screen_ota_verify(&ota,false));assert(screen_ota_verify(&ota,true));
 assert(!screen_link_encode(NULL,100,1,1,"a",1));
 assert(screen_link_crc32("123456789",9)==0xcbf43926u);
 char wire[1536];unsigned char input[768],decoded[768];for(unsigned i=0;i<768;i++)input[i]=i;
 for(unsigned i=1;i<=768;i++){size_t n=screen_link_encode(wire,sizeof(wire),123,42,input,i);assert(n&&n<1536);uint32_t sid,seq;size_t used=sizeof(decoded);assert(screen_link_decode(wire,n-2,&sid,&seq,decoded,&used));assert(sid==123&&seq==42&&used==i&&!memcmp(input,decoded,i));wire[31]=wire[31]=='0'?'1':'0';used=sizeof(decoded);assert(!screen_link_decode(wire,n-2,&sid,&seq,decoded,&used));}
 size_t one=screen_link_encode(wire,sizeof(wire),1,1,"a",1);size_t capacity=sizeof(decoded);uint32_t sid,seq;
 assert(one==46&&wire[41]=='Q');wire[41]='R';assert(!screen_link_decode(wire,one-2,&sid,&seq,decoded,&capacity));
 screen_link_encode(wire,sizeof(wire),1,1,"abc",3);capacity=2;assert(!screen_link_decode(wire,44,&sid,&seq,decoded,&capacity));
 screen_init(&m);screen_sample_set(&m,MET_CO2,600,true,0);screen_record(&m,0);
 screen_sample_set(&m,MET_CO2,620,true,180000);screen_record(&m,180000);
 assert(m.count==4&&m.history[0].valid&&m.history[1].valid==0&&m.history[2].valid==0&&m.history[3].valid);
 screen_record(&m,180001);assert(m.count==4);
 screen_init(&m);screen_record(&m,UINT32_MAX-5000);screen_record(&m,120000);assert(m.count==3);
 puts("core scenarios passed");
}
