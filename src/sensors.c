#include "screen.h"
#include "sensors.h"
bool sensor_uart_value(const uint8_t *p,unsigned n,uint8_t cmd,uint16_t *v){
 if(!p||!v||n<6||p[0]!=0x16||n!=(unsigned)p[1]+3||p[2]!=cmd)return false;
 uint8_t sum=0;for(unsigned i=0;i<n;i++)sum+=p[i];if(sum)return false;
 *v=((uint16_t)p[3]<<8)|p[4];return true;
}
static uint8_t crc(const uint8_t *p){uint8_t c=255;for(unsigned i=0;i<2;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c&128)?(c<<1)^0x31:c<<1;}return c;}
bool sensor_sht3x(const uint8_t p[6],float *t,float *h){
 if(!p||!t||!h||crc(p)!=p[2]||crc(p+3)!=p[5])return false;
 /* Raw conversion only. Stateful stock compensation is in sht_compensation.c. */
 *t=(float)(175.0*(((unsigned)p[0]<<8)|p[1])/65534.0-45.0);
 *h=100.0f*(((unsigned)p[3]<<8)|p[4])/65534.0f;return true;
}
unsigned battery_permille(unsigned mv){
 static const unsigned curve[]={3233,3435,3532,3585,3616,3662,3749,3842,3943,4053,4185};
 if(mv<=curve[0])return 0;
 if(mv>=curve[10])return 1000;
 for(unsigned i=1;i<11;i++)if(mv<curve[i])return (i-1)*100+(mv-curve[i-1])*100/(curve[i]-curve[i-1]);
 return 1000;
}

/* 0x4a888: CMD=1 requires frame[13], beyond the common value envelope. */
bool sensor_hcho(const uint8_t *p,unsigned n,uint16_t *value,uint8_t *status){
 uint16_t raw;
 if(!value||!status||n<14||!sensor_uart_value(p,n,1,&raw))return false;
 float x=(float)((double)raw*1.25/10.0);
 x=(float)((double)x+1.0);
 if(x>50.0f)x=50.0f;
 *value=(uint16_t)x;*status=p[13];return true;
}
