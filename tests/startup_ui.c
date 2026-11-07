#include "startup_ui.h"
#include "resources.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
LV_FONT_DECLARE(zh16);
static lv_color_t buffer[480*16];
static uint16_t pixels[480*854],before[480*854];
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p) {
  for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)
    pixels[y*480+x]=p[(y-a->y1)*lv_area_get_width(a)+x-a->x1].full;
  lv_disp_flush_ready(d);
}
static lv_obj_t *find(lv_obj_t *p,const char *s) {
  for(unsigned i=0;i<lv_obj_get_child_cnt(p);i++) {
    lv_obj_t *o=lv_obj_get_child(p,i);
    if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),s))return o;
    lv_obj_t *r=find(o,s);if(r)return r;
  }
  return NULL;
}
static void draw(void){lv_timer_handler();lv_refr_now(NULL);}
static void artifact(const char *name) {
  const char *dir=getenv("SCREEN_UI_EVIDENCE");if(!dir)return;
  char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);
  fprintf(f,"P6\n480 854\n255\n");for(unsigned i=0;i<480*854;i++){
    lv_color_t c={.full=pixels[i]};lv_color32_t v={.full=lv_color_to32(c)};
    fputc(v.ch.red,f);fputc(v.ch.green,f);fputc(v.ch.blue,f);
  }fclose(f);
}
int main(void) {
  lv_init();static lv_disp_draw_buf_t b;static lv_disp_drv_t d;
  lv_disp_draw_buf_init(&b,buffer,NULL,480*16);lv_disp_drv_init(&d);
  d.hor_res=480;d.ver_res=854;d.draw_buf=&b;d.flush_cb=flush;lv_disp_drv_register(&d);
  screen_model m;screen_init(&m);screen_ui(&m,1000,false);draw();
  screen_font_resources=(screen_resources){.state=RESOURCE_LOADING,.bytes=100000,.received=25000};
  screen_startup_begin(1000);screen_startup_ui(&m,1000);draw();
  assert(find(lv_layer_top(),"加载中")&&find(lv_layer_top(),"字库 25%")&&find(lv_layer_top(),"传感器 0/5"));
  artifact("startup-loading-zh");memcpy(before,pixels,sizeof(pixels));
  lv_tick_inc(400);screen_startup_ui(&m,1400);draw();
  bool animation=false;for(unsigned y=760;y<810;y++)for(unsigned x=32;x<80;x++)
    if(before[y*480+x]!=pixels[y*480+x])animation=true;
  assert(animation); // Pixels move under the actual LVGL spinner timer.
  screen_event(&m,1);screen_ui(&m,1400,false);draw();
  assert(lv_obj_get_child_cnt(lv_layer_top())==1&&find(lv_layer_top(),"加载中"));
  m.appearance.language=1;screen_startup_ui(&m,1500);draw();
  assert(find(lv_layer_top(),"Loading")&&find(lv_layer_top(),"Sensors 0/5"));artifact("startup-loading-en");
  m.asleep=true;screen_startup_ui(&m,1600);assert(lv_obj_has_flag(lv_obj_get_child(lv_layer_top(),0),LV_OBJ_FLAG_HIDDEN));
  m.asleep=false;m.ota=true;screen_startup_ui(&m,1700);assert(lv_obj_has_flag(lv_obj_get_child(lv_layer_top(),0),LV_OBJ_FLAG_HIDDEN));
  m.ota=false;screen_startup_ui(&m,1800);assert(!lv_obj_has_flag(lv_obj_get_child(lv_layer_top(),0),LV_OBJ_FLAG_HIDDEN));
  for(unsigned i=0;i<5;i++)screen_sample_set(&m,i,i,true,2000);
  screen_startup_ui(&m,2000);assert(find(lv_layer_top(),"Sensors ready"));
  screen_font_resources.state=RESOURCE_READY;screen_startup_ui(&m,2100);assert(!lv_obj_get_child_cnt(lv_layer_top()));
  /* Missing optional HCHO does not delay completion. Missing fitted sensors
   * and failed NOR settle to a warning, then normal pages with no retry loop. */
  screen_init(&m);screen_startup_begin(0xfffffff0);screen_font_resources.state=RESOURCE_IO;
  screen_startup_ui(&m,0xfffffff0+1000u);draw();
  assert(find(lv_layer_top(),"使用内置字库"));
  screen_startup_ui(&m,0xfffffff0+SCREEN_SENSOR_STARTUP_MS);draw();
  assert(find(lv_layer_top(),"部分传感器未连接"));artifact("startup-fallback");
  screen_startup_ui(&m,0xfffffff0+SCREEN_SENSOR_STARTUP_MS+5000u);assert(!lv_obj_get_child_cnt(lv_layer_top()));
  /* Repeat long font loading and page changes without accumulating objects. */
  for(unsigned k=0;k<100;k++) {
    screen_startup_begin(1000);screen_font_resources.state=RESOURCE_LOADING;
    screen_startup_ui(&m,1000);assert(lv_obj_get_child_cnt(lv_layer_top())==1);
    for(unsigned i=0;i<3;i++){screen_event(&m,1);screen_ui(&m,1000,false);screen_startup_ui(&m,1001);}
    screen_startup_ui(&m,SCREEN_FONT_DEADLINE+6000u);
    screen_startup_ui(&m,SCREEN_FONT_DEADLINE+11000u);assert(!lv_obj_get_child_cnt(lv_layer_top()));
  }
  /* Loading status must be legible before the external bank is ready. */
  const char *s="加载中字库传感器已就绪使用内置部分功能未连接";
  while(*s){uint32_t cp=_lv_txt_encoded_next(s,NULL);s+=_lv_txt_encoded_size(s);lv_font_glyph_dsc_t g;assert(lv_font_get_glyph_dsc(&zh16,&g,cp,0));}
  puts("PASS LVGL animated startup, paging, bilingual fallback and bounded sensor wait");
}
