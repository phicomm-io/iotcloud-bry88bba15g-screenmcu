#include "screen.h"
#include "sensors.h"
#include "calibration.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct sht_vector {
 uint8_t packet[6];sensor_sht_config config;sensor_sht_state before;
 uint8_t status;bool ramp,force;sensor_sht_result result;sensor_sht_state after;
};
struct event {unsigned type,channel,n;uint8_t data[6];};
struct cal_vector {
 sensor_calibration before;uint32_t age;uint16_t co2,tvoc;sensor_calibration after;
 unsigned count;struct event events[4];
};
#include "sht_vectors.inc"
#include "calibration_vectors.inc"
struct log {unsigned n;bool reject;struct event events[8];};
static bool capture(void *ctx,unsigned channel,unsigned event,const uint8_t *p,size_t n){
 struct log *log=ctx;assert(log->n<8&&n<=6);
 struct event *e=&log->events[log->n++];*e=(struct event){event,event==CAL_SNAPSHOT?0:channel,(unsigned)n,{0}};
 if(n)memcpy(e->data,p,n);
 return !(log->reject&&event==CAL_COMMAND);
}
static void same_job(const calibration_job *a,const calibration_job *b){
 assert(a->stage==b->stage&&a->issued==b->issued&&a->index==b->index);
 assert(a->status==b->status&&a->saved_status==b->saved_status&&a->wait==b->wait);
 assert(!memcmp(a->samples,b->samples,sizeof(a->samples)));
}
static void same_cal(const sensor_calibration *a,const sensor_calibration *b){
 assert(a->pending==b->pending&&a->network_awake==b->network_awake&&a->co2_command_pending==b->co2_command_pending);
 same_job(&a->jobs[0],&b->jobs[0]);same_job(&a->jobs[1],&b->jobs[1]);
}
int main(void){
 for(unsigned i=0;i<sizeof(sht_vectors)/sizeof(*sht_vectors);i++){
  const struct sht_vector *v=&sht_vectors[i];sensor_sht_state s=v->before;sensor_sht_result r;
  assert(sensor_sht_compensate(&s,&v->config,v->packet,v->status,v->ramp,v->force,&r));
  assert(r.temperature10==v->result.temperature10&&r.humidity10==v->result.humidity10);
  assert(r.raw_temperature10==v->result.raw_temperature10&&r.raw_humidity10==v->result.raw_humidity10);
  assert(r.unclamped_temperature10==v->result.unclamped_temperature10);
  assert(s.samples==v->after.samples&&s.base==v->after.base&&s.hcho==v->after.hcho&&s.settled==v->after.settled);
 }
 /* CRC failures are transactional; no count/compensation/sample mutation. */
 const struct sht_vector *v=&sht_vectors[1];sensor_sht_state state=v->before,before=state;
 sensor_sht_result result={1,2,3,4,5},old=result;uint8_t bad[6];memcpy(bad,v->packet,6);bad[5]^=1;
 assert(!sensor_sht_compensate(&state,&v->config,bad,0x51,true,false,&result));
 assert(state.samples==before.samples&&state.base==before.base&&state.hcho==before.hcho&&state.settled==before.settled);
 assert(!memcmp(&result,&old,sizeof(result)));
 assert(!sensor_sht_compensate(NULL,&v->config,bad,0x51,true,false,&result));
 /* HCHO final conversion and raw status; truncated frames never read byte 13. */
 const unsigned raw[]={0,7,8,391,392,65535},expected[]={1,1,2,49,50,50};
 for(unsigned i=0;i<6;i++){
  uint8_t p[15]={0x16,12,1};p[3]=raw[i]>>8;p[4]=raw[i];p[13]=0x59;
  uint8_t sum=0;for(unsigned k=0;k<14;k++)sum+=p[k];p[14]=(uint8_t)-sum;
  uint16_t value=99;uint8_t status=0;assert(sensor_hcho(p,15,&value,&status));
  assert(value==expected[i]&&status==0x59);
  for(unsigned n=0;n<14;n++)assert(!sensor_hcho(p,n,&value,&status));
  p[14]++;assert(!sensor_hcho(p,15,&value,&status));assert(value==expected[i]&&status==0x59);
 }
 for(unsigned i=0;i<sizeof(cal_vectors)/sizeof(*cal_vectors);i++){
  const struct cal_vector *c=&cal_vectors[i];sensor_calibration s=c->before;struct log log={0};
  calibration_poll(&s,c->age,c->co2,c->tvoc,capture,&log);same_cal(&s,&c->after);
  assert(log.n==c->count);
  for(unsigned j=0;j<log.n;j++){
   const struct event *a=&log.events[j],*b=&c->events[j];
   assert(a->type==b->type&&a->channel==b->channel&&a->n==b->n&&!memcmp(a->data,b->data,a->n));
  }
 }
 sensor_calibration cal;calibration_init(&cal);
 assert(!calibration_request(&cal,0)&&!calibration_request(&cal,4));
 assert(calibration_request(&cal,CAL_CO2|CAL_TVOC)&&!calibration_request(&cal,CAL_CO2));
 sensor_calibration saved=cal;calibration_poll(&cal,4000,400,100,NULL,NULL);same_cal(&cal,&saved);
 /* A rejected UART queue cannot yield command-issued status. */
 for(unsigned i=0;i<2;i++){
  calibration_init(&cal);assert(calibration_request(&cal,1u<<i));cal.jobs[i].stage=7;
  for(unsigned n=0;n<10;n++)cal.jobs[i].samples[n]=400;
  struct log log={.reject=true};calibration_poll(&cal,4000,400,100,capture,&log);
  assert(!cal.jobs[i].issued&&cal.jobs[i].status==0x55&&!cal.co2_command_pending);
  log.n=0;calibration_poll(&cal,4000,400,100,capture,&log);
  assert(log.n==1&&log.events[0].type==CAL_REPORT&&log.events[0].data[0]==2);
 }
 /* A stock request starts jobs but never implies completion. Duplicate active
  * requests are rejected rather than reproducing stock's NULL token dereference. */
 calibration_init(&cal);assert(calibration_factory_service(&cal,true,1,true,1)&&cal.pending==3);
 calibration_init(&cal);
 assert(!calibration_factory_service(&cal,false,1,true,1));
 assert(!calibration_factory_service(&cal,true,1,false,1));
 assert(calibration_request(&cal,CAL_CO2));assert(!calibration_factory_service(&cal,true,1,true,0)&&cal.pending==CAL_CO2);
 assert(calibration_hcho_install(true)&&!calibration_hcho_install(false));
 /* Complete asynchronous stock-service flows, including readiness/noise refusal. */
 for(unsigned scenario=0;scenario<3;scenario++){
  calibration_init(&cal);cal.jobs[0].status=cal.jobs[1].status=0x51;
  assert(calibration_factory_service(&cal,true,1,true,1));
  unsigned commands=0,reports=0,ticks=0;
  while(cal.pending&&ticks<1400){
   struct log log={0};unsigned value=400+(scenario==2&&ticks%240>=120?300:0);
   calibration_poll(&cal,scenario?4000:0,(uint16_t)value,(uint16_t)value,capture,&log);
   for(unsigned e=0;e<log.n;e++){
    if(log.events[e].type==CAL_COMMAND)commands++;
    if(log.events[e].type==CAL_REPORT){reports++;assert(log.events[e].data[0]==(scenario==1?3:2));}
   }
   ticks++;
  }
  assert(!cal.pending&&reports==2&&commands==(scenario==1?2u:0u));
 }
 static screen_runtime runtime;
 screen_runtime_init(&runtime);const struct sht_vector *first=&sht_vectors[0];
 assert(!screen_runtime_sht(&runtime,first->packet,0x51,false,false,10));
 assert(!screen_runtime_calibrate(&runtime,CAL_CO2));
 screen_runtime_sensor_config(&runtime,&first->config);
 assert(screen_runtime_sht(&runtime,first->packet,0x51,false,false,10));
 assert(runtime.model.samples[MET_TEMP].value==first->result.temperature10/10.0f);
 assert(runtime.model.samples[MET_HUM].value==first->result.humidity10/10.0f);
 assert(screen_sample_valid(&runtime.model,MET_TEMP,10));
 struct log runtime_log={0};runtime.context=&runtime_log;runtime.calibration_event=capture;
 assert(!screen_runtime_calibration_service(&runtime,true,1,true,0));
 for(unsigned now=100;now<=330000;now+=100){
  runtime.now=now;
  screen_sample_set(&runtime.model,MET_CO2,400,true,now);
  screen_sample_set(&runtime.model,MET_TVOC,100,true,now);
  screen_runtime_calibration_poll(&runtime,now/100,400,100);
 }
 runtime.co2_not_ready=true;
 assert(screen_runtime_calibration_warmup(&runtime,0)==180000);
 assert(!screen_runtime_calibration_service(&runtime,true,1,true,0));
 runtime.co2_not_ready=false;
 assert(!screen_runtime_calibration_warmup(&runtime,0));
 assert(!screen_runtime_calibration_warmup(&runtime,1));
 assert(screen_runtime_calibration_service(&runtime,true,1,true,0));
 screen_runtime_calibration_poll(&runtime,4000,400,100);
 assert(runtime.calibration.jobs[0].stage==2);
 assert(screen_runtime_calibration_cancel(&runtime));
 assert(runtime.calibration_result[0]==SCREEN_CAL_CANCELLED);
 assert(screen_runtime_calibration_service(&runtime,true,0,true,1));
 runtime.module.inhibited=true;
 screen_runtime_calibration_poll(&runtime,4000,400,100);
 assert(runtime.calibration_result[1]==SCREEN_CAL_SENSOR_LOST);
 assert(screen_runtime_calibration_warmup(&runtime,1)==320000);
 assert(screen_runtime_calibration_cancel(&runtime));
 runtime.module.inhibited=false;
 assert(screen_runtime_calibration_service(&runtime,true,1,true,0));
 runtime_log.n=0;
 runtime.calibration.jobs[0].stage=7;
 for(unsigned n=0;n<10;n++)runtime.calibration.jobs[0].samples[n]=400;
 screen_runtime_calibration_poll(&runtime,4000,400,100);
 assert(runtime.calibration_result[0]==SCREEN_CAL_SENT);
 assert(!screen_runtime_calibration_cancel(&runtime));
 screen_runtime_calibration_ack(&runtime);
 assert(runtime.calibration_result[0]==SCREEN_CAL_ACKNOWLEDGED);
 runtime_log.n=0;
 screen_runtime_calibration_poll(&runtime,4000,400,100);
 assert(runtime.calibration_result[0]==SCREEN_CAL_ACKNOWLEDGED);
 screen_runtime_sensor_config(&runtime,NULL);
 assert(!screen_runtime_sht(&runtime,first->packet,0x51,false,false,20));
 puts("223 SHT CPU fixtures, 180 calibration CPU transitions, CRC/HCHO/send-failure/compatibility scenarios passed");
}
