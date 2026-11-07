#include "startup_ui.h"
#include "resources.h"
#include "responsive_font.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
static struct {
  uint32_t at,finished_at;
  uint8_t received;
  bool active,finished;
  lv_obj_t *panel,*spinner,*title,*font,*sensors;
} startup;
void screen_startup_begin(uint32_t now) {
  if(startup.panel)lv_obj_del(startup.panel);
  memset(&startup,0,sizeof(startup));startup.active=true;startup.at=now;
}
static lv_obj_t *text(lv_obj_t *parent,int x,int y,int w,int h,unsigned size) {
  lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
  lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(o,screen_ui_font(size,false),0);return o;
}
static void set(lv_obj_t *o,const char *s,unsigned color) {
  if(strcmp(lv_label_get_text(o),s))lv_label_set_text(o,s);
  lv_obj_set_style_text_color(o,lv_color_hex(color),0);
}
void screen_startup_ui(const screen_model *m,uint32_t now) {
  if(!startup.active)return;
  /* HCHO is an optional extension. Never wait for a module that is not fitted. */
  const uint8_t expected=(1u<<MET_TEMP)|(1u<<MET_HUM)|(1u<<MET_CO2)|(1u<<MET_PM25)|(1u<<MET_TVOC);
  for(unsigned i=0;i<SCREEN_METRICS;i++)
    if((expected&(1u<<i))&&screen_sample_valid(m,i,now))startup.received|=1u<<i;
  bool sensors_ready=(startup.received&expected)==expected;
  bool sensors_wait=!sensors_ready&&(uint32_t)(now-startup.at)<SCREEN_SENSOR_STARTUP_MS;
  unsigned state=screen_font_resources.state;
  bool font_ready=state==RESOURCE_READY;
  bool font_wait=(state==RESOURCE_UNCHECKED||state==RESOURCE_LOADING)&&
                 (uint32_t)(now-startup.at)<SCREEN_FONT_DEADLINE+5000u;
  if(!sensors_wait&&!font_wait) {
    if(!startup.finished){startup.finished=true;startup.finished_at=now;}
    if((font_ready&&sensors_ready)||(uint32_t)(now-startup.finished_at)>=5000) {
      if(startup.panel)lv_obj_del(startup.panel);
      startup.panel=NULL;startup.active=false;return;
    }
  }
  if(!startup.panel) {
    lv_obj_t *p=startup.panel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(p);
    lv_obj_set_pos(p,16,734);lv_obj_set_size(p,448,104);
    lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);lv_obj_set_style_radius(p,14,0);
    lv_obj_set_style_border_width(p,1,0);lv_obj_clear_flag(p,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    startup.spinner=lv_spinner_create(p,1200,90);
    lv_obj_set_pos(startup.spinner,18,30);lv_obj_set_size(startup.spinner,44,44);
    lv_obj_remove_style(startup.spinner,NULL,LV_PART_KNOB);
    lv_obj_set_style_arc_width(startup.spinner,4,LV_PART_MAIN);
    lv_obj_set_style_arc_width(startup.spinner,4,LV_PART_INDICATOR);
    startup.title=text(p,80,8,350,32,28);
    startup.font=text(p,80,42,350,24,20);
    startup.sensors=text(p,80,71,350,24,20);
  }
  if(m->asleep||m->ota)lv_obj_add_flag(startup.panel,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(startup.panel,LV_OBJ_FLAG_HIDDEN);
  const screen_appearance *a=&m->appearance;bool en=a->language==1;
  unsigned accent=appearance_color(a,AP_ACCENT),muted=appearance_color(a,AP_UNIT);
  lv_obj_set_style_bg_color(startup.panel,lv_color_hex(appearance_color(a,AP_BACKGROUND)),0);
  lv_obj_set_style_border_color(startup.panel,lv_color_hex(appearance_color(a,AP_LINE)),0);
  lv_obj_set_style_arc_color(startup.spinner,lv_color_hex(appearance_color(a,AP_LINE)),LV_PART_MAIN);
  lv_obj_set_style_arc_color(startup.spinner,lv_color_hex(accent),LV_PART_INDICATOR);
  if(font_wait||sensors_wait)lv_obj_clear_flag(startup.spinner,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(startup.spinner,LV_OBJ_FLAG_HIDDEN);
  set(startup.title,font_wait||sensors_wait?(en?"Loading":"加载中"):(en?"Ready with limited data":"部分功能未就绪"),accent);
  char b[96];
  if(font_wait) {
    unsigned percent=screen_font_resources.bytes?(uint64_t)screen_font_resources.received*100/screen_font_resources.bytes:0;
    if(percent>99)percent=99; /* Complete only after the CRC and glyph checks. */
    snprintf(b,sizeof(b),en?"Font %u%%":"字库 %u%%",percent);
  } else snprintf(b,sizeof(b),"%s",font_ready?(en?"Font ready":"字库已就绪"):(en?"Using built-in font":"使用内置字库"));
  set(startup.font,b,muted);
  unsigned count=0;for(unsigned i=0;i<SCREEN_METRICS;i++)count+=(startup.received>>i)&1u;
  if(sensors_wait)snprintf(b,sizeof(b),en?"Sensors %u/5":"传感器 %u/5",count);
  else snprintf(b,sizeof(b),"%s",sensors_ready?(en?"Sensors ready":"传感器已就绪"):(en?"Some sensors unavailable":"部分传感器未连接"));
  set(startup.sensors,b,muted);
}
