#include "calibration.h"
#include <string.h>
void calibration_init(sensor_calibration *s){
 if(!s)return;
 memset(s,0,sizeof(*s));
 for(unsigned i=0;i<2;i++){s->jobs[i].stage=1;s->jobs[i].status=0x52;}
}
bool calibration_request(sensor_calibration *s,unsigned channels){
 if(!s||!channels||(channels&~3u)||s->pending)return false;
 for(unsigned i=0;i<2;i++)if(channels&(1u<<i))s->jobs[i].stage=1;
 s->pending=(uint8_t)channels;return true;
}
static bool emit(calibration_emit f,void *ctx,unsigned channel,unsigned event,
 const uint8_t *data,size_t n){return f&&f(ctx,channel,event,data,n);}
static void restore_other(sensor_calibration *s,unsigned i){
 if(s->jobs[1-i].saved_status==0x53)s->jobs[1-i].status=0x53;
}
static void poll_job(sensor_calibration *s,unsigned i,uint32_t age,uint16_t value,
 calibration_emit f,void *ctx){
 calibration_job *j=&s->jobs[i];unsigned ch=1u<<i;
 if(!(s->pending&ch))return;
 switch(j->stage){
 case 1:
  s->network_awake=18000;if(i==0)j->issued=0;j->index=0;
  /* Stock clears only the first five entries; all ten are overwritten before use. */
  for(unsigned n=0;n<5;n++)j->samples[n]=0;
  j->wait=(j->status==0x52||(i==1&&j->status==0x56)||age<=(i?3195u:1597u))?3:8;
  j->stage=2;break;
 case 2:
  if(j->wait==3){
   j->status=0x55;j->saved_status=0;
   emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);
   j->issued=0;j->wait=0;j->stage=8;
  }else if(j->wait==8){
   j->status=0x53;j->saved_status=0x53;restore_other(s,i);
   j->wait=0;emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);j->stage=3;
  }
  break;
 case 3:
  if(j->wait++>=119){
   j->wait=0;
   /* Guard a corrupt state instead of reproducing stock's unchecked write. */
   if(j->index>=10){j->issued=0;j->stage=8;break;}
   j->samples[j->index++]=value;
   if(j->index>=10)j->stage=5;
  }
  break;
 case 5:
  for(unsigned a=0;a<9;a++)for(unsigned b=a+1;b<10;b++)
   if(j->samples[a]>j->samples[b]){uint16_t v=j->samples[a];j->samples[a]=j->samples[b];j->samples[b]=v;}
  j->stage=7;break;
 case 7:
  if((unsigned)(j->samples[9]-j->samples[0])>=201){
   j->status=0x55;restore_other(s,i);emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);j->issued=0;
  }else{
   if(i==0){
    static const uint8_t cmd[]={0x11,3,3,1,0x90,0x58};
    s->co2_command_pending=true;
    if(!emit(f,ctx,ch,CAL_COMMAND,cmd,sizeof(cmd))){
     s->co2_command_pending=false;j->status=0x55;j->issued=0;
     emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);j->wait=0;j->stage=8;break;
    }
   }
   j->status=0x54;restore_other(s,i);emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);
   if(i==1){
    static const uint8_t cmd[]={0x11,2,3,0,0xea};
    if(!emit(f,ctx,ch,CAL_COMMAND,cmd,sizeof(cmd))){
     j->status=0x55;j->issued=0;emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);j->stage=8;break;
    }
   }
   j->issued=1;
  }
  if(i==0)j->wait=0;
  j->stage=8;break;
 case 8:{
  uint8_t code=j->issued?3:2;emit(f,ctx,ch,CAL_REPORT,&code,1);
  j->saved_status=0;j->stage=9;break;
 }
 case 9:
  if(j->wait++>=21){j->wait=0;j->stage=16;}
  break;
 case 16:
  j->issued=0;restore_other(s,i);emit(f,ctx,ch,CAL_SNAPSHOT,NULL,0);
  if(s->network_awake<10)s->network_awake=10;
  j->stage=1;s->pending&=(uint8_t)(i?1:2);break;
 default:break;
 }
}
void calibration_poll(sensor_calibration *s,uint32_t age,uint16_t co2,uint16_t tvoc,
 calibration_emit f,void *ctx){
 if(!s||!f)return;
 poll_job(s,0,age,co2,f,ctx);poll_job(s,1,age,tvoc,f,ctx);
}
bool calibration_factory_service(sensor_calibration *s,bool hc,int co2,bool ht,int tvoc){
 if(!s||(s->pending&0xf0)||!hc)return false;
 if(co2==1&&(s->pending&1))return false;
 if(!ht)return false;
 if(tvoc==1&&(s->pending&2))return false;
 s->pending=(uint8_t)((s->pending&3)|(co2==1?1:0)|(tvoc==1?2:0));return true;
}
bool calibration_hcho_install(bool has_client_token){return has_client_token;}
