#include "lvgl.h"
#include "fast_image.h"
#include "screen.h"
#include "widget_fixtures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
static lv_color_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT],
    fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) {
      if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
        fb[y * SCREEN_WIDTH + x] = *p;
      p++;
    }
  lv_disp_flush_ready(d);
}
int main(int argc, char **argv) {
  lv_init();
  static lv_disp_draw_buf_t b;
  lv_disp_draw_buf_init(&b, pixels, NULL, SCREEN_WIDTH * SCREEN_HEIGHT);
  static lv_disp_drv_t d;
  lv_disp_drv_init(&d);
  d.hor_res = SCREEN_WIDTH;
  d.ver_res = SCREEN_HEIGHT;
  d.draw_buf = &b;
  d.flush_cb = flush;
  d.draw_ctx_init = screen_draw_ctx_init;
  lv_disp_drv_register(&d);
  screen_model m;
  screen_init(&m);
  m.page = argc > 1 ? (unsigned)atoi(argv[1]) % SCREEN_PAGES : 0;
  bool demo = argc > 2 && atoi(argv[2]);
  uint32_t now = 120000;
  if (demo) {
    float v[] = {23.6, 48, 628, 12, 0.12, 0.03};
    for (unsigned k = 0; k < 120; k++) {
      for (unsigned i = 0; i < 6; i++)
        screen_sample_set(&m, i, v[i] + (i == 2 ? k % 12 : 0), true, k * 60000);
      screen_record(&m, k * 60000);
    }
    now = 119 * 60000;
    m.network = true;
    m.battery_valid = true;
    m.battery_percent = 78;
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    m.clock_valid = true;
    m.hour = tm->tm_hour;
    m.minute = tm->tm_min;
    m.local_day = (uint32_t)t / 86400;
  }
  if (demo) {
    unsigned kind = argc > 3 ? (unsigned)atoi(argv[3]) % 5 : 4;
    m.scene.page[m.page].enabled = true;
    const unsigned char *fixtures[] = {demo_widget_0, demo_widget_1,
                                       demo_widget_2, demo_widget_3,
                                       demo_widget_4};
    const size_t sizes[] = {sizeof(demo_widget_0), sizeof(demo_widget_1),
                            sizeof(demo_widget_2), sizeof(demo_widget_3),
                            sizeof(demo_widget_4)};
    screen_widget_apply(&m.external, fixtures[kind], sizes[kind], now);
    screen_widget_apply(&m.external, demo_widget_0, sizeof(demo_widget_0), now);
    /* Move the numeric fixture to slot zero without changing decoded content.
     */
    m.external.items[0] = m.external.items[1];
    screen_widget_apply(&m.external, fixtures[kind], sizes[kind], now);
  }

  if (demo) {
    m.weather = (screen_weather){.valid = true,
                                 .at = now - 120000,
                                 .ttl = 900000,
                                 .fields = 15,
                                 .temperature = 18.5,
                                 .feels = 17.2,
                                 .humidity = 56,
                                 .wind = 2.1};
    snprintf(m.weather.provider, sizeof(m.weather.provider), "QWeather");
    snprintf(m.weather.location, sizeof(m.weather.location), "广东 · 广州");
    snprintf(m.weather.text, sizeof(m.weather.text), "多云，空气清新");
    for (unsigned i = 0; i < 3; i++) {
      weather_day *d = &m.weather.days[i];
      d->valid = true;
      d->low = 14 + i;
      d->high = 22 + i;
      snprintf(d->date, sizeof(d->date), "2026-11-%02u", 6 + i);
      snprintf(d->text, sizeof(d->text), "多云");
    }
    m.reminders[0] = (reminder_notice){.id = 1, .unread = true};
    strcpy(m.reminders[0].title, "打开窗户，通风换气");
    strcpy(m.reminders[0].text, "空气质量提醒：现在适合开窗 15 分钟。");
    m.reminders[1] = (reminder_notice){.id = 2, .unread = true};
    strcpy(m.reminders[1].title, "记得给植物浇水");
    strcpy(m.reminders[1].text, "每天 09:00 · 家里的小事，准时提醒。");
    if (argc > 4) {
      unsigned kind = (unsigned)atoi(argv[4]) % 7;
      scene_item *it = NULL;
      for(unsigned i=0;i<m.scene.count;i++)if(m.scene.item[i].id==16)it=&m.scene.item[i];
      if(!it)return 2;
      m.page=5;it->type=SC_LINE;it->chart=kind;it->series_count=kind==0||kind==3?1:3;
      it->additive=kind>=4;strcpy(it->title,"室内用电");strcpy(it->unit,"W");
      it->h = 6;
      m.data[0] = (scene_data){
          .id = 16, .at = now, .ttl = 300000, .count = 12, .valid = true,
          .series_count=it->series_count};
      const char *names[]={"照明","空调","其他"};
      for(unsigned k=0;k<it->series_count;k++){
        strcpy(m.data[0].names[k],names[k]);strcpy(m.data[0].units[k],kind==2&&k?"%":"W");
        m.data[0].masks[k]=0xfff;
        for(unsigned i=0;i<12;i++)m.data[0].series[k][i]=30+18*k+15*sinf(i*.7f+k);
      }
      if(argc>5){
        const char *mode=argv[5];
        if(!strcmp(mode,"gaps"))m.data[0].masks[1]&=~(7u<<4);
        if(!strcmp(mode,"expired"))m.data[0].expired=true;
        if(!strcmp(mode,"negative"))m.data[0].series[0][0]=-2;
        if(!strcmp(mode,"strip"))it->h=2;
        if(!strcmp(mode,"composite")){
          scene_item *text=&m.scene.item[m.scene.count++];
          *text=(scene_item){.id=30,.page=5,.type=SC_TEXT,.binding=6,.x=0,.y=4,.w=2,.h=2};
          strcpy(text->title,"提醒");strcpy(text->text,"每小时通风\n让空气流动");
          scene_item *weather=&m.scene.item[m.scene.count++];
          *weather=(scene_item){.id=31,.page=5,.type=SC_WEATHER,.x=2,.y=4,.w=2,.h=2};strcpy(weather->title,"广州天气");
          it->h=4;
        }
      }
    }
  }
  if(demo&&argc>3&&(!strcmp(argv[3],"value-number")||!strcmp(argv[3],"value-grade"))){
    m.page=1;screen_sample_set(&m,MET_TVOC,375,true,now);
    for(unsigned i=0;i<m.scene.count;i++)if(m.scene.item[i].binding==SC_BIND_TVOC)
      m.scene.item[i].value_mode=!strcmp(argv[3],"value-number")?SC_VALUE_NUMBER:SC_VALUE_GRADE;
  }
  if(demo&&argc>3&&!strncmp(argv[3],"responsive-",11)) {
    const char *kind=argv[3]+11;
    m.scene.pages=1;m.scene.count=1;m.page=0;
    m.scene.page[0]=(scene_page){.enabled=true,.id=1};strcpy(m.scene.page[0].title,"室内空气");
    scene_item *it=&m.scene.item[0];
    *it=(scene_item){.id=1,.type=SC_VALUE,.binding=MET_CO2,.w=2,.h=2,.value_mode=SC_VALUE_NUMBER};
    strcpy(it->title,"CO2");
    if(!strcmp(kind,"row")){it->w=4;it->h=1;}
    else if(!strcmp(kind,"square")){it->w=4;it->h=4;}
    else if(!strcmp(kind,"grade")){it->w=4;it->h=4;it->binding=MET_TVOC;it->value_mode=SC_VALUE_GRADE;strcpy(it->title,"TVOC");}
    else if(!strcmp(kind,"weather")){it->w=4;it->h=6;it->type=SC_WEATHER;strcpy(it->title,"广州天气");}
    else if(!strcmp(kind,"weather-small")){it->type=SC_WEATHER;strcpy(it->title,"广州天气");}
    else if(!strcmp(kind,"strip")){
      it->w=4;it->h=2;it->type=SC_LINE;it->hours=24;
      for(unsigned i=0;i<120;i++)m.history[i].values[MET_CO2]=600+150*sinf(i*.1f);
    }
  }
  screen_ui(&m, now, demo);
  lv_refr_now(NULL);
  printf("P6\n480 854\n255\n");
  for (unsigned i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
    uint32_t c = lv_color_to32(fb[i]);
    putchar(c >> 16 & 255);
    putchar(c >> 8 & 255);
    putchar(c & 255);
  }
  return 0;
}
