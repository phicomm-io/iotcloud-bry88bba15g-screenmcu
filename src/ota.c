#include "ota.h"
bool screen_ota_begin(screen_ota *s,uint32_t n){
 if(!s||s->active||n<64||n>SCREEN_APP_LIMIT||n%16)return false;
 *s=(screen_ota){.size=n,.active=true};return true;
}
bool screen_ota_accept(screen_ota *s,uint32_t offset,const void *p,size_t n){
 if(!s||!s->active||s->verified||!p||!n||n>1024||offset!=s->received||n>s->size-s->received)return false;
 s->received+=(uint32_t)n;return true;
}
bool screen_ota_verify(screen_ota *s,bool match){
 if(!s||!s->active||s->received!=s->size||!match)return false;
 s->verified=true;s->active=false;return true;
}
