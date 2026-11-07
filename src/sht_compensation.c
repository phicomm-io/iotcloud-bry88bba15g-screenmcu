#include "screen.h"
#include "sensors.h"
#include <math.h>
/* Recovered from SENSOR3_MCU SV.04.00.04.00, 0x18268..0x18e04.
 * Keep evaluation order/float round points: algebraic simplification changes
 * the stock fixed-point result near truncation boundaries. */
static float single(double x){volatile float f=(float)x;return f;}
static double pressure(float t){
 double x=(double)t;
 double p=pow(x,4.0)*0.0007;
 p=p+pow(x,3.0)*0.0086;
 p=p+pow(x,2.0)*1.8046;
 p=p+pow(x,1.0)*41.449;
 return p+616.58;
}
static int32_t rh_limit(int32_t x){return x<0?0:x>=1000?999:x;}
bool sensor_sht_compensate(sensor_sht_state *state,const sensor_sht_config *c,
 const uint8_t frame[6],uint8_t status,bool ramp,bool force,sensor_sht_result *out){
 float t,rh;
 if(!state||!c||!out||!sensor_sht3x(frame,&t,&rh))return false;
 sensor_sht_state s=*state;
 sensor_sht_result r;
 int32_t sum=(int32_t)c->temperature+c->secondary;
 double base_step=(double)sum/3720.0,heat_step=(double)c->hcho_heat/480.0;
 s.samples++;
 double elapsed=(double)s.samples*2.06;
 if(elapsed>4200.0)s.settled=true;
 r.raw_temperature10=(int32_t)((double)t*10.0);
 if(status==0x59){
  if(fabs(s.hcho)<=fabs(heat_step*2.06))s.hcho=0.0;
  else s.hcho=s.hcho+heat_step*(-2.06);
 }else{
  if(fabs(s.hcho)>=fabs((double)c->hcho_heat))s.hcho=(double)c->hcho_heat;
  else s.hcho=s.hcho+heat_step*2.06;
 }
 if(!ramp)s.base=(double)sum;
 else if(elapsed<=480.0){s.base=0.0;s.hcho=0.0;}
 else{
  if(fabs(s.base)<fabs((double)sum))s.base=s.base+base_step*2.06;
  if(!(fabs(s.base)<fabs((double)sum)))s.base=(double)sum;
 }
 if(force)s.base=(double)sum;
 r.unclamped_temperature10=(int32_t)(((double)r.raw_temperature10+s.base)+s.hcho);
 r.temperature10=r.unclamped_temperature10;
 if(r.temperature10<=-201)r.temperature10=-200;
 else if(r.temperature10>600)r.temperature10=600;
 r.raw_humidity10=(int32_t)single((double)rh*10.0);
 if(c->temperature==0)r.humidity10=rh_limit(r.raw_humidity10+c->humidity);
 else{
  double p=pressure(t);
  float scaled=single((double)rh*622.0);
  scaled=single((double)scaled/100.0);
  double numerator=(double)scaled*p;
  double vapor=(double)single((double)rh/100.0)*p;
  double mixing=numerator/(10132500.0-vapor);
  float corrected=single((double)r.unclamped_temperature10/10.0);
  double corrected_p=pressure(corrected);
  if(mixing==0.0)r.humidity10=0;
  else{
   double humidity=((mixing*10132500.0)/(corrected_p*(mixing+622.0)))*1000.0;
   r.humidity10=humidity>999.0?999:humidity<0.0?0:(int32_t)humidity;
  }
 }
 *state=s;*out=r;return true;
}
