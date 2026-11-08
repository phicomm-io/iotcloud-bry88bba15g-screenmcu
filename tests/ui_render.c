#include "screen.h"
#include "ui_layout.h"
#include "responsive_font.h"
#include "wallpaper.h"
#include "resources.h"
#include "ui_language.h"
#include <stdlib.h>
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static lv_color_t pixels[SCREEN_WIDTH*16];
static screen_model model;
static lv_area_t probe;
static uint16_t frame[SCREEN_WIDTH*SCREEN_HEIGHT],image[SCREEN_WIDTH*SCREEN_HEIGHT];
static unsigned plotted;
static unsigned probe_color=0xb7dca5;
static bool probing;
static void flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p) {
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)frame[y*480+x]=p[(y-a->y1)*lv_area_get_width(a)+x-a->x1].full;
 if(probing) {
  lv_color_t color=lv_color_hex(probe_color);
  for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)
   if(x>=probe.x1&&x<=probe.x2&&y>=probe.y1&&y<=probe.y2 &&
      p[(y-a->y1)*lv_area_get_width(a)+x-a->x1].full==color.full)plotted++;
 }
 lv_disp_flush_ready(d);
}
static void draw(void) {lv_refr_now(NULL);}
static void scene(unsigned type,unsigned w,unsigned h) {
 memset(&model.scene,0,sizeof(model.scene));model.page=0;
 model.scene.pages=1;model.scene.count=1;model.scene.schema=3;
 model.scene.page[0]=(scene_page){.enabled=true,.id=1};
 strcpy(model.scene.page[0].title,"空气");
 model.scene.item[0]=(scene_item){.id=1,.type=type,.binding=MET_CO2,.w=w,.h=h,.hours=24};
 strcpy(model.scene.item[0].title,"CO2");
}
static lv_obj_t *find(lv_obj_t *root,const char *text) {
 for(unsigned i=0;i<lv_obj_get_child_cnt(root);i++) {
  lv_obj_t *o=lv_obj_get_child(root,i);
  if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text))return o;
  lv_obj_t *child=find(o,text);if(child)return child;
 }
 return NULL;
}
static void inside(lv_obj_t *parent) {
 lv_area_t a;lv_obj_get_content_coords(parent,&a);
 for(unsigned i=0;i<lv_obj_get_child_cnt(parent);i++) {
  lv_obj_t *o=lv_obj_get_child(parent,i);lv_area_t b;lv_obj_get_coords(o,&b);
  assert(b.x1>=a.x1&&b.y1>=a.y1&&b.x2<=a.x2&&b.y2<=a.y2);
 }
}
static lv_obj_t *card(void) {
 lv_obj_t *o=find(lv_scr_act(),"CO2");
 assert(o);return lv_obj_get_parent(o);
}
static void rain_icon(unsigned size) {
 lv_obj_t *parent=lv_obj_get_parent(find(lv_scr_act(),"26°")),*icon=NULL;
 for(unsigned i=0;i<lv_obj_get_child_cnt(parent);i++) {
  lv_obj_t *o=lv_obj_get_child(parent,i);
  if(lv_obj_get_width(o)==(int)size&&lv_obj_get_height(o)==(int)size) {icon=o;break;}
 }
 assert(icon);lv_obj_get_coords(icon,&probe);
 probe_color=0x8ebfcd;probing=true;plotted=0;lv_obj_invalidate(icon);draw();probing=false;
 assert(plotted>0); /* Real rain pixels, including the provider's drizzle descriptions. */
 probe_color=0xb7dca5;
}
static void artifact(const char *name) {
 const char *dir=getenv("SCREEN_UI_EVIDENCE");if(!dir)return;
 char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n480 854\n255\n");for(unsigned i=0;i<480*854;i++) {
  lv_color_t c={.full=frame[i]};lv_color32_t v={.full=lv_color_to32(c)};fputc(v.ch.red,f);fputc(v.ch.green,f);fputc(v.ch.blue,f);
 }fclose(f);
}
static void rail(unsigned count,unsigned active) {
 unsigned found=0;
 for(unsigned i=0;i<lv_obj_get_child_cnt(lv_scr_act());i++) {
  lv_obj_t *o=lv_obj_get_child(lv_scr_act(),i);
  if(lv_obj_get_y(o)||lv_obj_get_height(o)!=(int)appearance_rail_height(&model.appearance))continue;
  assert(found<count);
  int left=SCREEN_WIDTH*found/count,right=SCREEN_WIDTH*(found+1)/count;
  assert(lv_obj_get_x(o)==left&&lv_obj_get_width(o)==right-left);
  unsigned role=found==active?AP_ACCENT:AP_UNIT;
  assert(lv_obj_get_style_bg_color(o,0).full==lv_color_hex(appearance_color(&model.appearance,role)).full);
  assert(lv_obj_get_style_bg_opa(o,0)==(found==active?UI_PAGE_RAIL_ACTIVE_OPACITY:UI_PAGE_RAIL_IDLE_OPACITY)*255/100);
  assert(lv_obj_get_style_radius(o,0)==0);
  assert(frame[2*SCREEN_WIDTH+left]!=lv_color_hex(appearance_color(&model.appearance,AP_CANVAS)).full);
  found++;
 }
 assert(found==count);
}
static void compact_layout(void) {
 for(unsigned pages=1;pages<=SCENE_PAGES;pages++) {
  scene(SC_VALUE,4,6);model.scene.pages=pages;model.page=pages-1;model.scene.item[0].page=model.page;
  for(unsigned i=0;i<pages;i++) {
   model.scene.page[i]=(scene_page){.enabled=true,.id=i+1};strcpy(model.scene.page[i].title,"空气");
  }
  screen_scene saved=model.scene;
  assert(screen_ui(&model,120000,false));draw();rail(pages,pages-1);inside(card());
  assert(!memcmp(&saved,&model.scene,sizeof(saved))); /* No scene migration. */
  lv_area_t a;lv_obj_get_coords(card(),&a);
  assert(a.x1==16&&a.y1==66&&a.x2==463&&a.y2==837);
 }
 artifact("layout-eight-pages");
 model.scene.page[1].enabled=false;model.scene.page[3].enabled=false;
 assert(screen_ui(&model,120000,false));draw();rail(6,5);artifact("layout-disabled-pages");
 /* Selectable rails cover the edge without moving or shrinking any widget.
  * The wide rail must leave the page title below its lower edge. */
 for(unsigned k=0;k<3;k++) {
  static const unsigned sizes[]={6,12,20};model.appearance.page_rail=sizes[k];
  assert(screen_ui(&model,120000,false));draw();rail(6,5);
  lv_obj_t *title=find(lv_scr_act(),"空气");assert(title);
  assert(lv_obj_get_y(title)>=(int)sizes[k]+2);
  unsigned edge=sizes[k]-1;
  assert(frame[edge*SCREEN_WIDTH+479]!=lv_color_hex(appearance_color(&model.appearance,AP_CANVAS)).full);
  assert(frame[(edge+1)*SCREEN_WIDTH+479]==lv_color_hex(appearance_color(&model.appearance,AP_CANVAS)).full);
  char name[32];snprintf(name,sizeof(name),"page-rail-%u",sizes[k]);artifact(name);
 }
 model.appearance.page_rail=0;
 /* The bottom-right cell uses the recovered area, including short full rows. */
 scene(SC_VALUE,2,2);model.scene.item[0].x=2;model.scene.item[0].y=4;
 assert(screen_ui(&model,120000,false));draw();inside(card());lv_area_t a;lv_obj_get_coords(card(),&a);
 assert(a.x1==244&&a.y1==586&&a.x2==463&&a.y2==837);
 scene(SC_VALUE,4,1);model.scene.item[0].y=5;
 assert(screen_ui(&model,120000,false));draw();inside(card());lv_obj_get_coords(card(),&a);
 assert(a.x1==16&&a.y1==716&&a.x2==463&&a.y2==837);artifact("layout-bottom-row");
}
static void fits_text(lv_obj_t *o) {
 assert(o);lv_point_t size;
 lv_txt_get_size(&size,lv_label_get_text(o),lv_obj_get_style_text_font(o,0),
                 lv_obj_get_style_text_letter_space(o,0),0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 assert(size.x<=lv_obj_get_width(o)&&size.y<=lv_obj_get_height(o));
}
static void fits_label(lv_obj_t *root,const char *text) {
 lv_obj_t *o=find(root,text);if(!o)fprintf(stderr,"Missing compact label: %s\n",text);fits_text(o);
}
static void ota_text(void) {
 model.appearance.language=0;model.ota=true;
 assert(screen_ui(&model,120000,false));draw();
 lv_obj_t *o=find(lv_scr_act(),"正在更新\n请保持供电");assert(o);
 artifact("ota-dialog");
 const lv_font_t *fonts[]={lv_obj_get_style_text_font(o,0),screen_ui_font(24,false)};
 for(unsigned i=0;i<sizeof(fonts)/sizeof(fonts[0]);i++) {
  assert(lv_font_get_glyph_width(fonts[i],'\n',0)==0);
  assert(lv_font_get_glyph_width(fonts[i],'\r',0)==0);
 }
 model.ota=false;
}
static void compact_widgets(void) {
 screen_init(&model);model.battery_valid=true;model.battery_percent=78;
 wallpaper.ready=false;wallpaper.wanted=0;
 model.clock_valid=true;model.hour=18;model.minute=42;model.local_day=20763;
 model.weather=(screen_weather){.valid=true,.fields=15,.temperature=26,.feels=27,.humidity=62,.wind=2.4};
 strcpy(model.weather.location,"广州");strcpy(model.weather.text,"晴间多云");
 for(unsigned lang=0;lang<2;lang++) {
  model.appearance.language=lang;
  scene(SC_CLOCK,4,1);assert(scene_validate(&model.scene));
  assert(screen_ui(&model,120000,false));draw();
  lv_obj_t *time=find(lv_scr_act(),"18:42");fits_text(time);inside(lv_obj_get_parent(time));
  lv_obj_t *date=lv_obj_get_child(lv_obj_get_parent(time),1);fits_text(date);
  assert(lv_obj_get_x(time)+lv_obj_get_width(time)<lv_obj_get_x(date));
  model.clock_valid=false;assert(screen_ui(&model,120000,false));draw();fits_label(lv_scr_act(),"--:--");
  model.clock_valid=true;
  for(unsigned rows=1;rows<=2;rows++) {
   scene(SC_WEATHER,4,rows);strcpy(model.scene.item[0].title,"广州");
   assert(scene_validate(&model.scene));
   for(unsigned hidden=0;hidden<2;hidden++) {
    model.scene.item[0].hide_title=hidden;
    for(unsigned state=0;state<3;state++) {
     model.weather.valid=state!=1;model.weather.expired=state==2;
     assert(screen_ui(&model,120000,false));draw();
     lv_obj_t *temp=find(lv_scr_act(),state==1?"--":"26°");assert(temp);
     inside(lv_obj_get_parent(temp));fits_text(temp);
     if(rows==2&&state!=1)assert(find(lv_scr_act(),"62%")&&find(lv_scr_act(),lang?"Feels like":"体感"));
    }
    model.weather.temperature=-40;assert(screen_ui(&model,120000,false));draw();fits_label(lv_scr_act(),"-40°");model.weather.temperature=26;
   }
  }
  scene(SC_VALUE,2,1);assert(scene_validate(&model.scene));
  const float samples[]={628,9999,-12};const char *texts[]={"628","9999","-12"};
  for(unsigned i=0;i<3;i++) {
   screen_sample_set(&model,MET_CO2,samples[i],true,120000);
   assert(screen_ui(&model,120000,false));draw();inside(card());fits_label(card(),texts[i]);
  }
  model.scene.item[0].binding=MET_TVOC;model.scene.item[0].value_mode=SC_VALUE_GRADE;
  model.tvoc_level=1;screen_sample_set(&model,MET_TVOC,100,true,120000);
  assert(screen_ui(&model,120000,false));draw();inside(card());fits_label(card(),lang?"Good":"优");
 }
 /* One six-row scene exercises every new shape together through the real renderer. */
 scene(SC_CLOCK,4,1);model.scene.count=6;strcpy(model.scene.page[0].title,"组合");
 model.scene.item[1]=(scene_item){.id=2,.type=SC_WEATHER,.y=1,.w=4,.h=1};
 model.scene.item[2]=(scene_item){.id=3,.type=SC_WEATHER,.y=2,.w=4,.h=2};
 strcpy(model.scene.item[1].title,"广州");strcpy(model.scene.item[2].title,"广州");
 model.scene.item[3]=(scene_item){.id=4,.type=SC_VALUE,.binding=MET_CO2,.y=4,.w=2,.h=1};strcpy(model.scene.item[3].title,"CO2");
 model.scene.item[4]=(scene_item){.id=5,.type=SC_VALUE,.binding=MET_TEMP,.x=2,.y=4,.w=2,.h=1};strcpy(model.scene.item[4].title,"温度");
 model.scene.item[5]=(scene_item){.id=6,.type=SC_VALUE,.binding=MET_TVOC,.y=5,.w=4,.h=1};strcpy(model.scene.item[5].title,"TVOC");
 model.weather.valid=true;model.weather.expired=false;
 screen_sample_set(&model,MET_CO2,628,true,120000);screen_sample_set(&model,MET_TEMP,23.5,true,120000);
 assert(scene_validate(&model.scene));
 for(unsigned lang=0;lang<2;lang++) {
  model.appearance.language=lang;assert(screen_ui(&model,120000,false));draw();
  artifact(lang?"compact-widgets-en":"compact-widgets-zh");
 }
 const char *dir=getenv("SCREEN_UI_EVIDENCE");
 if(dir){char path[1024];uint8_t bytes[SCENE_BYTES];assert(scene_encode(&model.scene,bytes));snprintf(path,sizeof(path),"%s/compact-widgets.bin",dir);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));fclose(f);}
}
static void status_icons(void) {
 scene(SC_VALUE,4,6);model.network=true;model.battery_valid=true;model.battery_percent=78;
 assert(screen_ui(&model,120000,false));draw();artifact("status-battery");
 lv_obj_t *retained=card();unsigned children=lv_obj_get_child_cnt(lv_scr_act());
 memcpy(image,frame,sizeof(frame));
 model.charging=true;assert(screen_ui(&model,120000,false));draw();artifact("status-charging");
 assert(memcmp(image,frame,SCREEN_WIDTH*UI_GRID_TOP*sizeof(*frame)));
 assert(!memcmp(image+SCREEN_WIDTH*UI_GRID_TOP,frame+SCREEN_WIDTH*UI_GRID_TOP,
                sizeof(frame)-SCREEN_WIDTH*UI_GRID_TOP*sizeof(*frame)));
 assert(card()==retained&&lv_obj_get_child_cnt(lv_scr_act())==children);
 assert(!screen_ui(&model,120001,false));
 model.charging=false;assert(screen_ui(&model,120001,false));draw();
 assert(!memcmp(image,frame,sizeof(frame))); /* Unplugging removes every bolt pixel. */
 model.network=false;assert(screen_ui(&model,120001,false));draw();artifact("status-offline");
 assert(memcmp(image,frame,SCREEN_WIDTH*UI_GRID_TOP*sizeof(*frame)));
 assert(!memcmp(image+SCREEN_WIDTH*UI_GRID_TOP,frame+SCREEN_WIDTH*UI_GRID_TOP,
                sizeof(frame)-SCREEN_WIDTH*UI_GRID_TOP*sizeof(*frame)));
 model.network=true;assert(screen_ui(&model,120001,false));draw();assert(!memcmp(image,frame,sizeof(frame)));
 model.charging=true;
 for(unsigned percent=0;percent<=100;percent+=100) {
  model.battery_percent=percent;assert(screen_ui(&model,120001,false));draw();
  artifact(percent?"status-charging-full":"status-charging-empty");
 }
 model.battery_valid=false;assert(screen_ui(&model,120001,false));draw();artifact("status-charging-unknown");
 assert(card()==retained&&lv_obj_get_child_cnt(lv_scr_act())==children);
}
int main(void) {
 const char *bank=getenv("SCREEN_TEST_FONT");
 if(bank){
  static uint8_t font_cache[SCREEN_RESOURCE_LIMIT];FILE *f=fopen(bank,"rb");assert(f);
  size_t n=fread(font_cache,1,sizeof(font_cache),f);assert(fgetc(f)==EOF);fclose(f);
  screen_font_resources=(screen_resources){.cache=font_cache,.capacity=sizeof(font_cache)};
  assert(resources_validate(&screen_font_resources,n)&&screen_font_resources.bpp==2);
 }
 lv_init();static lv_disp_draw_buf_t buffer;static lv_disp_drv_t display;
 lv_disp_draw_buf_init(&buffer,pixels,NULL,SCREEN_WIDTH*16);
 lv_disp_drv_init(&display);display.hor_res=SCREEN_WIDTH;display.ver_res=SCREEN_HEIGHT;
 display.draw_buf=&buffer;display.flush_cb=flush;lv_disp_drv_register(&display);
 screen_init(&model);status_icons();screen_init(&model);model.battery_valid=true;model.battery_percent=78;
 compact_layout();screen_init(&model);model.battery_valid=true;model.battery_percent=78;
 scene(SC_VALUE,2,2);screen_sample_set(&model,MET_CO2,628,true,120000);
 assert(screen_ui(&model,120000,false));draw();inside(card());
 lv_obj_t *value=find(card(),"628"),*title=find(card(),"CO2"),*unit=find(card(),"ppm");
 assert(value&&title&&unit);
 int small_font=lv_obj_get_style_text_font(value,0)->line_height;
 assert(lv_obj_get_y(title)<lv_obj_get_y(value)&&lv_obj_get_y(unit)<lv_obj_get_y(value));
 assert(lv_obj_get_style_text_font(title,0)->line_height>lv_obj_get_style_text_font(unit,0)->line_height);
 model.scene.item[0].w=4;model.scene.item[0].h=4;
 assert(screen_ui(&model,120000,false));draw();inside(card());
 assert(lv_obj_get_style_text_font(find(card(),"628"),0)->line_height>small_font);
 model.scene.item[0].binding=MET_TVOC;model.scene.item[0].value_mode=SC_VALUE_GRADE;
 model.tvoc_level=1;screen_sample_set(&model,MET_TVOC,100,true,120000);
 assert(screen_ui(&model,120000,false));draw();
 lv_obj_t *grade=find(lv_scr_act(),"优");assert(grade);inside(lv_obj_get_parent(grade));
 model.scene.item[0].binding=MET_CO2;model.scene.item[0].value_mode=SC_VALUE_NUMBER;
 for(unsigned h=1;h<=3;h++) {
  model.scene.item[0].h=h;assert(screen_ui(&model,120000,false));draw();inside(card());
  title=find(card(),"CO2");unit=find(card(),"ppm");value=find(card(),"628");
  assert(lv_obj_get_y(unit)>lv_obj_get_y(title));
  assert(lv_obj_get_x(value)>lv_obj_get_x(title)+lv_obj_get_width(title));
 }
 lv_mem_monitor_t before,after;lv_mem_monitor(&before);clock_t start=clock();
 for(unsigned i=0;i<4000;i++)assert(!screen_ui(&model,120000+i,false));
 double skipped_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
 lv_mem_monitor(&after);assert(before.free_size==after.free_size);
 screen_sample_set(&model,MET_CO2,628.01f,true,124001);
 assert(!screen_ui(&model,124001,false)); /* same rounded readout */
 screen_sample_set(&model,MET_TVOC,888,true,124001);
 assert(!screen_ui(&model,124001,false)); /* invisible metric */
 screen_sample_set(&model,MET_CO2,629,true,124001);
 lv_obj_t *retained_card=card(),*retained_title=find(card(),"CO2"),*retained_value=find(card(),"628");
 assert(screen_ui(&model,124001,false));draw();
 assert(card()==retained_card&&find(card(),"CO2")==retained_title&&find(card(),"629")==retained_value);
 assert(screen_ui(&model,144001,false));draw();assert(find(card(),"--"));
 /* Hiding a row title preserves its unit and reclaims real numeric width. */
 scene(SC_VALUE,4,1);screen_sample_set(&model,MET_CO2,628,true,150000);
 assert(screen_ui(&model,150000,false));draw();int normal_width=lv_obj_get_width(find(lv_scr_act(),"628"));
 model.scene.item[0].hide_title=1;assert(screen_ui(&model,150000,false));draw();
 assert(!find(lv_scr_act(),"CO2"));value=find(lv_scr_act(),"628");assert(value&&find(lv_scr_act(),"ppm"));
 assert(lv_obj_get_width(value)>normal_width);inside(lv_obj_get_parent(value));artifact("titleless-value");
 scene(SC_TEXT,4,6);strcpy(model.scene.item[0].text,"Readings");
 assert(screen_ui(&model,150000,false));draw();int body_y=lv_obj_get_y(find(lv_scr_act(),"Readings")),body_h=lv_obj_get_height(find(lv_scr_act(),"Readings"));
 model.scene.item[0].hide_title=1;assert(screen_ui(&model,150000,false));draw();value=find(lv_scr_act(),"Readings");
 assert(lv_obj_get_y(value)==0&&lv_obj_get_height(value)==body_h+body_y);artifact("titleless-text");
 scene(SC_CLOCK,4,2);model.clock_valid=true;model.hour=18;model.minute=42;model.local_day=20763;
 assert(screen_ui(&model,150000,false));draw();
 assert(!screen_ui(&model,151000,false));model.minute++;
 assert(screen_ui(&model,151000,false));draw();
 model.asleep=true;model.minute++;assert(!screen_ui(&model,152000,false));
 model.asleep=false;assert(screen_ui(&model,152000,false));draw();
 scene(SC_VALUE,2,2);model.scene.item[0].binding=SC_BIND_EXTERNAL;
 model.data[0]=(scene_data){.id=1,.count=1,.valid=true,.at=150000,.ttl=10000,.values={12}};
 assert(screen_ui(&model,150000,false));draw();
 model.data[0].at=151000;assert(!screen_ui(&model,151000,false));
 assert(screen_ui(&model,161000,false));draw();
 scene(SC_WEATHER,4,6);strcpy(model.scene.item[0].title,"广州");
 model.weather=(screen_weather){.valid=true,.fields=15,.temperature=26,.feels=27,.humidity=62,.wind=2.4};
 strcpy(model.weather.location,"广州");strcpy(model.weather.text,"多云");
 for(unsigned i=0;i<3;i++) {
  model.weather.days[i].valid=true;model.weather.days[i].low=22;model.weather.days[i].high=28;
  strcpy(model.weather.days[i].date,"2026-11-07");strcpy(model.weather.days[i].text,"晴");
 }
 assert(screen_ui(&model,162000,false));draw();
 lv_obj_t *weather=lv_obj_get_parent(find(lv_scr_act(),"26°"));inside(weather);
 const char *drizzle[]={"Light drizzle","Moderate drizzle","Dense drizzle","Freezing drizzle"};
 for(unsigned i=0;i<sizeof(drizzle)/sizeof(drizzle[0]);i++) {
  strcpy(model.weather.text,drizzle[i]);assert(screen_ui(&model,162000,false));draw();rain_icon(100);
 }
 model.appearance.language=1;strcpy(model.weather.text,"小雨");assert(screen_ui(&model,162000,false));draw();
 assert(find(lv_scr_act(),"Light rain")&&find(lv_scr_act(),"Feels like")&&find(lv_scr_act(),"Humidity"));
 artifact("weather-english");model.appearance.language=0;
 assert(!strcmp(screen_weather_text(true,"晴间多云"),"Mainly clear"));
 assert(!strcmp(screen_weather_text(false,"Dense drizzle"),"大毛毛雨"));
 assert(!strcmp(screen_weather_text(true,"heavy intensity rain"),"Heavy intensity rain"));
 assert(!strcmp(screen_weather_text(true,"custom weather"),"custom weather"));
 model.scene.item[0].w=2;model.scene.item[0].h=2;
 assert(screen_ui(&model,162000,false));draw();rain_icon(40);
 model.scene.item[0].w=4;model.scene.item[0].h=6;
 assert(screen_ui(&model,162000,false));draw();
 model.weather.at=163000;model.weather.observed++;
 assert(!screen_ui(&model,163000,false));
 model.weather.expired=true;assert(screen_ui(&model,164000,false));draw();
 model.scene.item[0].w=2;model.scene.item[0].h=2;
 assert(screen_ui(&model,164000,false));draw();inside(lv_obj_get_parent(find(lv_scr_act(),"26°")));
 for(unsigned kind=SC_CH_LINE;kind<=SC_CH_RING;kind++) {
  scene(SC_LINE,4,2);scene_item *it=&model.scene.item[0];it->binding=SC_BIND_EXTERNAL;it->chart=kind;
  it->series_count=kind==SC_CH_LINE||kind==SC_CH_BAR?1:3;it->additive=kind>=SC_CH_STACK_BAR;
  model.data[0]=(scene_data){.id=1,.valid=true,.at=170000,.ttl=100000,.count=12,.series_count=it->series_count};
  for(unsigned k=0;k<it->series_count;k++) {
   strcpy(model.data[0].names[k],k==0?"照明":k==1?"空调":"其他");strcpy(model.data[0].units[k],"W");
   model.data[0].masks[k]=0xfff;for(unsigned j=0;j<12;j++)model.data[0].series[k][j]=12+j+k;
  }
  assert(screen_ui(&model,170000,false));draw();inside(card());
 }
 scene(SC_LINE,4,2);model.scene.item[0].binding=MET_CO2;model.count=0;
 screen_sample_set(&model,MET_CO2,628,true,170001);
 assert(screen_ui(&model,170001,false));draw();assert(find(card(),"628"));
 lv_obj_t *plot=lv_obj_get_child(card(),1);lv_obj_get_coords(plot,&probe);
 probing=true;plotted=0;lv_obj_invalidate(plot);draw();probing=false;
 assert(plotted>0); /* One real sample must draw a visible point immediately. */
 screen_sample_set(&model,MET_CO2,629,true,170002);
 assert(screen_ui(&model,170002,false));draw();assert(find(card(),"629"));
 model.head=0;model.have_minute=false;screen_record(&model,170002);
 screen_sample_set(&model,MET_CO2,700,true,230002);screen_record(&model,230002);
 assert(screen_ui(&model,230002,false));draw();assert(find(card(),"2m"));
 plot=lv_obj_get_child(card(),1);lv_obj_get_coords(plot,&probe);
 probing=true;plotted=0;lv_obj_invalidate(plot);draw();probing=false;
 assert(plotted>100); /* Two real minute samples form a line during startup. */
 model.history[1].valid=0;
 screen_sample_set(&model,MET_CO2,710,true,290002);screen_record(&model,290002);
 assert(screen_ui(&model,290002,false));draw();assert(find(card(),"3m"));
 plot=lv_obj_get_child(card(),1);lv_obj_get_coords(plot,&probe);
 probe.x1=(probe.x1+probe.x2)/2-4;probe.x2=probe.x1+8;
 probing=true;plotted=0;lv_obj_invalidate(plot);draw();probing=false;
 assert(plotted==0); /* Stretching never joins measurements across a missing minute. */
 model.count=SCREEN_HISTORY;model.head=0;
 for(unsigned i=0;i<SCREEN_HISTORY;i++)model.history[i]=(screen_point){.minute=i+2,.valid=1u<<MET_CO2,.values={[MET_CO2]=600}};
 screen_sample_set(&model,MET_CO2,600,true,86460000);
 assert(screen_ui(&model,86460000,false));draw();assert(find(card(),"24h"));
 model.scene.item[0].points=256;assert(screen_ui(&model,86460000,false));draw();inside(card());
 artifact("chart-256-points");
 scene(SC_VALUE,4,1);screen_sample_set(&model,MET_CO2,600,true,180000);
 assert(screen_ui(&model,180000,false));draw();lv_mem_monitor(&before);start=clock();
 retained_card=card();retained_title=find(card(),"CO2");retained_value=find(card(),"600");
 for(unsigned i=0;i<200;i++) {
  screen_sample_set(&model,MET_CO2,601+i,true,180001+i);
  assert(screen_ui(&model,180001+i,false));draw();
  char value_text[32];snprintf(value_text,sizeof(value_text),"%u",601+i);
  assert(card()==retained_card&&find(card(),"CO2")==retained_title&&find(card(),value_text)==retained_value);
 }
 lv_mem_monitor(&after);assert(before.free_size==after.free_size);
 double redraw_ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
 /* Retain an unrelated chart and all its children when a neighbouring
  * numeric card changes; minute history is still reflected on the next tick. */
 scene(SC_LINE,4,2);model.scene.count=2;model.count=0;
 model.scene.item[1]=(scene_item){.id=2,.type=SC_VALUE,.binding=MET_TEMP,.w=2,.h=2,.y=2};
 strcpy(model.scene.item[1].title,"温度");
 screen_sample_set(&model,MET_CO2,650,true,300000);screen_record(&model,300000);
 screen_sample_set(&model,MET_TEMP,24,true,300000);
 assert(screen_ui(&model,300000,false));draw();retained_card=card();
 plot=lv_obj_get_child(retained_card,1);
 screen_sample_set(&model,MET_TEMP,25,true,301000);
 assert(screen_ui(&model,301000,false));draw();assert(find(lv_scr_act(),"25.0"));
 assert(card()==retained_card&&lv_obj_get_child(card(),1)==plot);
 screen_sample_set(&model,MET_CO2,660,true,301001);
 assert(!screen_ui(&model,301001,false)); /* minute curve does not chase UART packets */
 screen_sample_set(&model,MET_CO2,670,true,360000);screen_record(&model,360000);
 assert(screen_ui(&model,360000,false));draw();assert(find(card(),"2m"));
 /* Actual scaled bitmaps are identical after LRU hits and evictions, at
  * different sizes and fonts. A complete ten-digit number remains cached. */
 static uint8_t expected_glyph[192*192/4];
 const lv_font_t *numeric=screen_ui_font(192,true);
 for(unsigned c='0';c<='9';c++)assert(lv_font_get_glyph_bitmap(numeric,c));
 screen_font_stats hot=screen_ui_font_stats();
 for(unsigned repeat=0;repeat<20;repeat++)for(unsigned c='0';c<='9';c++)assert(lv_font_get_glyph_bitmap(numeric,c));
 screen_font_stats warm=screen_ui_font_stats();assert(warm.hits-hot.hits==200&&warm.misses==hot.misses);
 for(unsigned k=0;k<2;k++)for(unsigned size=16;size<=192;size+=16) {
  const lv_font_t *font=screen_ui_font(size,k!=0);lv_font_glyph_dsc_t glyph;
  assert(lv_font_get_glyph_dsc(font,&glyph,'8',0));size_t n=(glyph.box_w*glyph.box_h*glyph.bpp+7)/8;
  const uint8_t *bitmap=lv_font_get_glyph_bitmap(font,'8');assert(bitmap);memcpy(expected_glyph,bitmap,n);
  for(unsigned i=0;i<30;i++)assert(lv_font_get_glyph_bitmap(screen_ui_font(16+i*4,k==0),'0'+i%10));
  bitmap=lv_font_get_glyph_bitmap(font,'8');assert(bitmap&&!memcmp(expected_glyph,bitmap,n));
 }
 assert(screen_ui_font_stats().evictions>0);
 // The six production palettes and per-widget roles reach actual LVGL pixels.
 scene(SC_VALUE,2,2);model.scene.item[0].value_mode=SC_VALUE_NUMBER;screen_sample_set(&model,MET_CO2,628,true,90000000);
 for(unsigned k=0;k<6;k++) {
  model.appearance.theme=k;assert(screen_ui(&model,90000000,false));draw();
  assert(frame[20*480]==lv_color_hex(appearance_color(&model.appearance,AP_CANVAS)).full);rail(1,0);
  char name[32];snprintf(name,sizeof(name),"theme-%u",k);artifact(name);
 }
 appearance_default(&model.appearance);model.appearance.count=1;
 model.appearance.items[0]=(appearance_item){.id=1,.mask=31,.opacity=45,.colors={0x224466,0xfedcba,0xabcdef,0x89abcd,0xa1b2c3}};
 assert(screen_ui(&model,90000000,false));draw();
 assert(lv_obj_get_style_text_color(find(card(),"CO2"),0).full==lv_color_hex(0xfedcba).full);
 assert(lv_obj_get_style_text_color(find(card(),"628"),0).full==lv_color_hex(0xabcdef).full);
 assert(lv_obj_get_style_text_color(find(card(),"ppm"),0).full==lv_color_hex(0x89abcd).full);
 assert(lv_obj_get_style_bg_opa(card(),0)==45*255/100);
 assert(lv_obj_get_style_text_opa(find(card(),"628"),0)==255);artifact("widget-colors");
 model.appearance.wallpaper=1;model.appearance.dim=0;wallpaper.pixels=image;wallpaper.kind=1;wallpaper.ready=true;
 for(unsigned i=0;i<480*854;i++)image[i]=lv_color_hex(0xff0000).full;
 assert(screen_ui(&model,90000000,false));draw();assert(frame[20*480]==lv_color_hex(0xff0000).full);rail(1,0);
 assert(frame[2*480]!=lv_color_hex(0xff0000).full); /* Rail blends over the wallpaper. */
 model.appearance.items[0].opacity=0;
 assert(screen_ui(&model,90000000,false));draw();assert(frame[305*480+100]==lv_color_hex(0xff0000).full);
 model.appearance.items[0].opacity=50;model.appearance.dim=24;
 assert(screen_ui(&model,90000000,false));draw();assert(frame[305*480+100]!=lv_color_hex(0xff0000).full);artifact("wallpaper-alpha");
 wallpaper.ready=false;assert(screen_ui(&model,90000000,false));draw();
 assert(frame[20*480]==lv_color_hex(appearance_color(&model.appearance,AP_CANVAS)).full);artifact("wallpaper-fallback");
 appearance_default(&model.appearance);wallpaper.kind=0;wallpaper.wanted=1;
 for(unsigned k=1;k<=3;k++) {
  wallpaper.wanted=k;wallpaper_poll(&wallpaper);model.appearance.wallpaper=k;
  assert(screen_ui(&model,90000000,false));draw();char name[32];snprintf(name,sizeof(name),"wallpaper-%u",k);artifact(name);
 }
 // Reproduce the user's disabled sixth page, offline only; each unit has visible pixels.
 const char *layout=getenv("SCREEN_UI_LAYOUT");
 if(layout){FILE *f=fopen(layout,"rb");assert(f);uint8_t bytes[SCENE_BYTES];assert(fread(bytes,1,sizeof(bytes),f)==sizeof(bytes));fclose(f);assert(scene_decode(&model.scene,bytes,sizeof(bytes)));}
 else {scene(SC_LINE,4,6);model.scene.item[0].chart=SC_CH_MULTI;model.scene.item[0].series_count=3;model.scene.item[0].binding2=MET_TVOC;model.scene.item[0].binding3=MET_PM25;}
 model.page=layout?5:0;model.count=model.head=0;model.have_minute=false;appearance_default(&model.appearance);
 for(unsigned i=0;i<12;i++) {
  unsigned now=90000000+i*60000;screen_sample_set(&model,MET_CO2,600+i*20,true,now);screen_sample_set(&model,MET_TVOC,480-i*30,true,now);screen_sample_set(&model,MET_PM25,2+i%7,true,now);screen_record(&model,now);
 }
 assert(screen_ui(&model,90660000,false));draw();
 artifact("sixth-page-three-units");
 for(unsigned k=0;k<3;k++) {
  probe=(lv_area_t){120,160,352,600};probe_color=appearance_color(&model.appearance,AP_CHART_A+k);probing=true;plotted=0;lv_obj_invalidate(lv_scr_act());draw();probing=false;assert(plotted>5);
 }
 artifact("sixth-page-three-units");
 printf("PASS UI: 16px margins, 448x772 content, full-width translucent rail for 1-8 pages, disabled-page filtering, retained label/card identity, isolated minute chart, 144 KiB bounded glyph LRU, square/row growth, 4x1, seven 4x2 charts, singleton pixels, drizzle icon pixels in both weather sizes, freshness/clock/TTL invalidation, 4000 unchanged frames %.2f ms, 200 local updates %.2f ms, heap stable %u bytes free, peak %u / %u bytes\n",skipped_ms,redraw_ms,(unsigned)after.free_size,(unsigned)after.max_used,(unsigned)after.total_size);
 compact_widgets();puts("PASS compact widgets: 4x1 clock/weather, 4x2 weather, 2x1 readings; real label bounds, numeric fitting, missing/expired data, title hiding and both languages");
 ota_text();puts("PASS OTA dialog: line breaks have zero glyph width and no question-mark fallback");
 lv_deinit();return 0;
}
