#include "lvgl.h"
#include "screen.h"
#include "ui_layout.h"
#include "responsive_font.h"
#include "wallpaper.h"
#include "resources.h"
#include "ui_language.h"
#include "media.h"
#include "stream.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(clock_font);
LV_FONT_DECLARE(clock72);
LV_FONT_DECLARE(metric_font);
LV_FONT_DECLARE(zh32);
LV_FONT_DECLARE(zh64);
/* Native 480x854 tokens, shared with the WebUI preview. */
static unsigned BG,CARD,INK,MUTED,GRID,colors[3];
static const screen_appearance *appearance;
static const appearance_item *custom;
static bool have_wallpaper;
static lv_obj_t *box(lv_obj_t *,int,int,int,int,unsigned,int);
static lv_img_dsc_t media_image_descriptor,stream_image_descriptor;
static void picture_cover(lv_event_t *event) {
  lv_cover_check_info_t *info=lv_event_get_param(event);
  if(info->res==LV_COVER_RES_MASKED)return;
  lv_obj_t *image=lv_obj_get_child(lv_event_get_target(event),0);
  if(!image||!lv_obj_check_type(image,&lv_img_class)||
     lv_img_get_angle(image)||lv_img_get_antialias(image)||
     lv_obj_get_style_img_opa(image,0)!=LV_OPA_COVER||
     lv_obj_get_style_opa(image,0)!=LV_OPA_COVER)return;
  const lv_img_dsc_t *descriptor=lv_img_get_src(image);
  unsigned zoom=lv_img_get_zoom(image);
  if(!zoom||!descriptor||descriptor->header.cf!=LV_IMG_CF_TRUE_COLOR)return;
  lv_area_t coords;lv_obj_get_coords(image,&coords);
  lv_point_t pivot;lv_img_get_pivot(image,&pivot);
  int32_t inverse=65536/zoom;
  /* Check the exact nearest-neighbor samples, excluding transformed padding.
   * This clip may cover opaque image pixels while its letterbox stays clear.
   * LVGL 8's scaled image object retains unscaled bounds during top-object
   * search, so the clip supplies this conservative coverage information. */
  const lv_area_t *area=info->area;
  int32_t x1=((area->x1-coords.x1-pivot.x)*inverse+pivot.x*256+128)>>8;
  int32_t x2=((area->x2-coords.x1-pivot.x)*inverse+pivot.x*256+128)>>8;
  int32_t y1=((area->y1-coords.y1-pivot.y)*inverse+pivot.y*256+128)>>8;
  int32_t y2=((area->y2-coords.y1-pivot.y)*inverse+pivot.y*256+128)>>8;
  if(x1>=0&&y1>=0&&x2<(int32_t)descriptor->header.w&&y2<(int32_t)descriptor->header.h)
    info->res=LV_COVER_RES_COVER;
}
static lv_obj_t *picture(lv_obj_t *parent,lv_img_dsc_t *descriptor,const uint16_t *pixels,unsigned width,unsigned height,int x,int y,int w,int h,bool cover){
  if(!width||!height||w<=0||h<=0)return NULL;
  lv_obj_t *clip=box(parent,x,y,w,h,0,0);
  lv_obj_set_style_bg_opa(clip,LV_OPA_TRANSP,0);
  lv_obj_add_event_cb(clip,picture_cover,LV_EVENT_COVER_CHECK,NULL);
  *descriptor=(lv_img_dsc_t){.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=width,.h=height},.data_size=width*height*2u,.data=(const uint8_t *)pixels};
  lv_obj_t *image=lv_img_create(clip);lv_img_set_src(image,descriptor);
  unsigned a=(unsigned)w*256/width,b=(unsigned)h*256/height,zoom=cover?(a>b?a:b):(a<b?a:b);
  if(!zoom)zoom=1;
  lv_img_set_zoom(image,zoom);lv_img_set_antialias(image,false);lv_obj_center(image);
  return image;
}
/* Only fixed-shape clock/value cards reuse label children. Their text and
 * fitted font change without deleting objects or touching other cards. */
static lv_obj_t *reuse_labels;
static unsigned reuse_index;
static unsigned wc(unsigned role) {return appearance_widget_color(appearance,custom,role);}
static unsigned legend_color(unsigned k) {return custom&&(custom->mask&16)?wc(4):colors[k];}
static bool english;
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,int h,const char *s,
                       const lv_font_t *font,unsigned color) {
  s=screen_translate(english,s);
  lv_obj_t *o=NULL;
  if(parent==reuse_labels&&reuse_index<lv_obj_get_child_cnt(parent))
    o=lv_obj_get_child(parent,reuse_index);
  if(parent==reuse_labels)reuse_index++;
  if(!o)o=lv_label_create(parent);
  if(strcmp(lv_label_get_text(o),s))lv_label_set_text(o,s);
  lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
  lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(o,font,0);
  lv_obj_set_style_text_color(o,lv_color_hex(color),0);
  return o;
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,unsigned color,int radius) {
  lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o);
  lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
  lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
  lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0); lv_obj_set_style_radius(o,radius,0);
  lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE); return o;
}
static void number(char *b,size_t n,float v,unsigned binding) {
  if (fabsf(v)>=1e6f) snprintf(b,n,"%.1fM",(double)v/1e6);
  else if (fabsf(v)>=10000) snprintf(b,n,"%.0fk",(double)v/1000);
  else if (binding==MET_TEMP || (fabsf(v)<100 && v!=floorf(v))) snprintf(b,n,"%.1f",(double)v);
  else snprintf(b,n,"%.0f",(double)v);
}
static const scene_data *data_for(const screen_model *m,unsigned id) {
  for(unsigned i=0;i<SCENE_ITEMS;i++) if(m->data[i].id==id && m->data[i].valid) return &m->data[i];
  return NULL;
}
static const char *metric_name(unsigned n) {
  static const char *names[]={"温度","湿度","CO2","PM2.5","TVOC","甲醛","外部"};
  return names[n<=6?n:6];
}
static const char *metric_unit(unsigned n) {
  static const char *units[]={"°C","%","ppm","µg/m³","ppb","",""};
  return units[n<=6?n:6];
}
/* Vector weather symbols use a single draw object, no image bank or bitmap. */
static unsigned weather_kind(const char *s) {
  s=screen_weather_text(false,s);
  char normalized[64];unsigned n=0;
  for(;s[n]&&n+1<sizeof(normalized);n++)
    normalized[n]=s[n]>='A'&&s[n]<='Z'?s[n]+'a'-'A':s[n];
  normalized[n]=0;s=normalized;
  if(strstr(s,"雷")||strstr(s,"thunder"))return 5;
  if(strstr(s,"雪")||strstr(s,"snow"))return 6;
  if(strstr(s,"雨")||strstr(s,"rain")||strstr(s,"drizzle"))return 3;
  if(strstr(s,"雾")||strstr(s,"霾")||strstr(s,"fog"))return 4;
  if(strstr(s,"晴间")||strstr(s,"少云")||strstr(s,"partly"))return 1;
  if(strstr(s,"云")||strstr(s,"阴")||strstr(s,"cloud")||strstr(s,"overcast"))return 2;
  if(strstr(s,"晴")||strstr(s,"clear"))return 7;
  return 0;
}
static void weather_line(lv_draw_ctx_t *ctx,const lv_area_t *a,int x,int y,int xx,int yy,unsigned color) {
  int size=lv_area_get_width(a);lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
  line.color=lv_color_hex(color);line.width=size>=80?4:2;line.round_start=line.round_end=1;
  lv_point_t p={a->x1+x*size/100,a->y1+y*size/100},q={a->x1+xx*size/100,a->y1+yy*size/100};
  lv_draw_line(ctx,&line,&p,&q);
}
static void weather_circle(lv_draw_ctx_t *ctx,const lv_area_t *a,int x,int y,int size,unsigned color) {
  int s=lv_area_get_width(a);lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
  d.bg_opa=LV_OPA_TRANSP;d.border_color=lv_color_hex(color);d.border_width=s>=80?4:2;d.radius=LV_RADIUS_CIRCLE;
  lv_area_t r={a->x1+x*s/100,a->y1+y*s/100,a->x1+(x+size)*s/100,a->y1+(y+size)*s/100};
  lv_draw_rect(ctx,&d,&r);
}
static void draw_weather(lv_event_t *e) {
  unsigned flags=(uintptr_t)lv_event_get_user_data(e),kind=flags&15;
  unsigned cloud=flags&16?MUTED:INK,sun=flags&16?MUTED:colors[2];
  lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);
  if(kind==7||kind==1) {
    weather_circle(ctx,&a,kind==7?28:12,kind==7?28:12,kind==7?44:36,sun);
    const int rays[8][4]={{50,5,50,17},{50,83,50,95},{5,50,17,50},{83,50,95,50},{18,18,26,26},{74,74,82,82},{18,82,26,74},{74,26,82,18}};
    if(kind==7)for(unsigned i=0;i<8;i++)weather_line(ctx,&a,rays[i][0],rays[i][1],rays[i][2],rays[i][3],sun);
  }
  if(kind>=1&&kind<=6) {
    static const uint8_t outline[][2]={{25,76},{17,73},{12,66},{11,57},{14,48},{21,42},{30,41},{35,30},{44,24},{54,22},{64,24},{73,30},{78,40},{78,47},{86,48},{91,54},{93,62},{90,69},{85,74},{78,76},{25,76}};
    for(unsigned i=1;i<sizeof(outline)/sizeof(outline[0]);i++)
      weather_line(ctx,&a,outline[i-1][0],outline[i-1][1],outline[i][0],outline[i][1],cloud);
    if(kind==3||kind==5)for(unsigned i=0;i<3;i++)weather_line(ctx,&a,35+i*18,85,30+i*18,96,colors[1]);
    if(kind==4)for(unsigned i=0;i<2;i++)weather_line(ctx,&a,18,86+i*10,84,86+i*10,cloud);
    if(kind==5){weather_line(ctx,&a,54,70,44,85,sun);weather_line(ctx,&a,44,85,57,85,sun);weather_line(ctx,&a,57,85,46,99,sun);}
    if(kind==6)for(unsigned i=0;i<2;i++){weather_line(ctx,&a,32+i*30,84,42+i*30,96,cloud);weather_line(ctx,&a,42+i*30,84,32+i*30,96,cloud);}
  }
  if(!kind)weather_line(ctx,&a,30,50,70,50,cloud);
}
static void weather_icon(lv_obj_t *parent,int x,int y,int size,const char *text,bool valid) {
  lv_obj_t *o=box(parent,x,y,size,size,CARD,0);
  lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);
  lv_obj_add_event_cb(o,draw_weather,LV_EVENT_DRAW_MAIN,(void*)(uintptr_t)(weather_kind(text)|(valid?0:16)));
}
static const lv_font_t *fit_font(const char *,unsigned,bool,int,int);
static void weather_card(lv_obj_t *c,const screen_weather *v,const scene_item *it,int w,int h) {
  bool full=it->w==4&&it->h==6,good=v->valid&&!v->expired;char b[96];
  int reclaim=it->hide_title?52:0;
  if(v->valid&&(v->fields&1))snprintf(b,sizeof(b),"%.0f°",(double)v->temperature);else strcpy(b,"--");
  if(it->w==4&&it->h==1) {
    const char *title=it->hide_title?"":v->location[0]?v->location:it->title;
    label(c,0,4,130,36,title,screen_ui_font(28,false),wc(1));
    label(c,0,it->hide_title?32:56,130,36,v->expired?"已过期":v->valid?screen_weather_text(english,v->text):"--",screen_ui_font(24,false),wc(4));
    weather_icon(c,142,(h-64)/2,64,v->text,good);
    const lv_font_t *font=fit_font(b,72,true,w-220,h);
    lv_obj_t *o=label(c,220,(h-font->line_height)/2,w-220,font->line_height,b,font,good?wc(2):MUTED);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_RIGHT,0);
  } else if(it->w==4&&it->h==2) {
    int top=it->hide_title?0:48;
    weather_icon(c,0,top+8,88,v->text,good);
    label(c,108,top,150,96,b,fit_font(b,80,true,150,96),good?wc(2):MUTED);
    label(c,270,top+12,w-270,72,v->expired?"已过期":v->valid?screen_weather_text(english,v->text):"--",screen_ui_font(28,false),wc(4));
    const char *names[]={"体感","湿度","风速 m/s"};
    for(unsigned i=0;i<3;i++) {
      int x=i*w/3,ww=w/3;
      lv_obj_t *o=label(c,x,top+100,ww,32,names[i],screen_ui_font(24,false),wc(3));
      lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
      if(v->valid&&(v->fields&(i==0?2:i==1?4:8)))snprintf(b,sizeof(b),i==0?"%.0f°":i==1?"%.0f%%":"%.1f",(double)(i==0?v->feels:i==1?v->humidity:v->wind));else strcpy(b,"--");
      o=label(c,x,top+132,ww,40,b,&zh32,good?wc(2):MUTED);
      lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    }
  } else if(full) {
    weather_icon(c,(w-100)/2,52-reclaim,100,v->text,good);
    lv_obj_t *o=label(c,0,166-reclaim,w,120,b,screen_ui_font(104,true),good?wc(2):MUTED);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    o=label(c,0,292-reclaim,w,44,v->expired?"已过期":v->valid?screen_weather_text(english,v->text):"--",&zh32,wc(4));
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    box(c,0,352-reclaim,w,1,GRID,0);box(c,0,460-reclaim,w,1,GRID,0);
    const char *names[]={"体感","湿度","风速 m/s"};
    for(unsigned i=0;i<3;i++) {
      int x=i*w/3,ww=w/3;
      o=label(c,x,368-reclaim,ww,32,names[i],screen_ui_font(24,false),wc(3));lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
      if(v->valid&&(v->fields&(i==0?2:i==1?4:8)))snprintf(b,sizeof(b),i==0?"%.0f°":i==1?"%.0f%%":"%.1f",(double)(i==0?v->feels:i==1?v->humidity:v->wind));else strcpy(b,"--");
      o=label(c,x,408-reclaim,ww,42,b,screen_ui_font(32,false),good?wc(2):MUTED);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    }
    for(unsigned i=1;i<3;i++) {
      const weather_day *day=&v->days[i];int y=482-reclaim+(i-1)*92;
      label(c,0,y,120,42,day->valid?day->date+5:"--",&zh32,wc(3));
      weather_icon(c,136,y-4,48,day->valid?day->text:"",day->valid&&good);
      if(day->valid)snprintf(b,sizeof(b),"%.0f / %.0f°",(double)day->low,(double)day->high);else strcpy(b,"--");
      o=label(c,200,y,w-200,44,b,&zh32,good?wc(2):MUTED);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_RIGHT,0);
    }
  } else {
    weather_icon(c,w-40,0,40,v->text,good);
    label(c,0,56-reclaim,w,88,b,screen_ui_font(80,true),good?wc(2):MUTED);
    label(c,0,154-reclaim,w,40,v->expired?"已过期":v->valid?screen_weather_text(english,v->text):"--",screen_ui_font(28,false),wc(4));
    if(it->h>=4) {
      box(c,0,222-reclaim,w,1,GRID,0);const weather_day *day=&v->days[1];
      label(c,0,246-reclaim,w,40,day->valid?day->date+5:"--",&zh32,wc(3));
      weather_icon(c,0,304-reclaim,56,day->valid?day->text:"",day->valid&&good);
      if(day->valid)snprintf(b,sizeof(b),"%.0f / %.0f°",(double)day->low,(double)day->high);else strcpy(b,"--");
      label(c,0,380-reclaim,w,44,b,&zh32,good?wc(2):MUTED);
    }
  }
  (void)h;
}
static const lv_font_t *fit_font(const char *s,unsigned target,bool numeric,int w,int h) {
  s=screen_translate(english,s);
  for(;;) {
    const lv_font_t *font=screen_ui_font(target,numeric);lv_point_t size;
    lv_txt_get_size(&size,s,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    if((size.x<=w&&size.y<=h)||target<=24)return font;
    target-=4;
  }
}
static void value_card(lv_obj_t *c,const scene_item *it,const char *title,
                       const char *unit,const char *value,bool grade,unsigned color,int w,int h) {
  bool row=it->w>it->h;
  unsigned scale=it->w<it->h?it->w:it->h; /* 2x2 -> 4x4 doubles every size. */
  unsigned title_size=row?32:16*scale,unit_size=row?24:12*scale,target=row?(it->h==1?80:144):48*scale;
  if(title_size>64)title_size=64;
  if(unit_size>48)unit_size=48;
  if(target>192)target=192;
  if(row) {
    int left=it->hide_title?(unit[0]?w/4:0):w*2/5;
    label(c,0,0,left?left:1,40,title,screen_ui_font(title_size,false),wc(1));
    label(c,0,it->hide_title?(h-32)/2:48,left?left:1,32,unit,screen_ui_font(unit_size,false),wc(3));
    const lv_font_t *font=fit_font(value,target,!grade,w-left-(left?12:0),h);
    lv_obj_t *o=label(c,left+(left?12:0),(h-font->line_height)/2,w-left-(left?12:0),font->line_height,value,font,color);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_RIGHT,0);
  } else {
    const lv_font_t *uf=screen_ui_font(unit_size,false);lv_point_t us;
    lv_txt_get_size(&us,unit,uf,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    int uw=unit[0]?us.x:0;if(uw>w/2)uw=w/2;
    int gap=uw?8*(int)scale/2:0,header=it->hide_title?(unit[0]?(int)uf->line_height:0):(int)title_size*5/4;
    label(c,0,0,w-uw-gap,header,title,screen_ui_font(title_size,false),wc(1));
    /* Keep the same three labels when an expired API value gains/loses its
     * unit. Empty labels occupy no visible pixels and retain their identity. */
    label(c,w-uw,header-uf->line_height,uw?uw:1,uf->line_height,unit,uf,wc(3));
    const lv_font_t *font=fit_font(value,target,!grade,w,h-header-12);
    int top=header+(h-header-font->line_height)/2;
    lv_obj_t *o=label(c,0,top,w,font->line_height,value,font,color);
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
  }
}
/* Chart data is owned until the next root rebuild. Drawing callbacks never
 * point at a stack buffer. */
typedef struct {
  unsigned kind,n,count,minutes; float values[3][256]; uint32_t mask[3][8];
  float lo[3],hi[3]; uint8_t axis[3];
  unsigned palette[3], grid;
  char names[3][32],units[3][16];
} plot_data;
/* Only the visible page needs plots. SCN3 fits three; retain eight slots for
 * up to eight widgets per page. */
static plot_data plots[8];
static unsigned plot_used;
static bool valid_at(const plot_data *p,unsigned k,unsigned i) {return (p->mask[k][i/32]>>(i%32))&1u;}
static int py(const plot_data *p,unsigned k,float v,int top,int height) {
  unsigned axis=p->axis[k]; return top+height-(int)((v-p->lo[axis])/(p->hi[axis]-p->lo[axis])*height);
}
static bool stack_at(const plot_data *p,unsigned i) {
  for(unsigned k=0;k<p->count;k++) if(!valid_at(p,k,i)) return false;
  return true;
}
static void draw_plot(lv_event_t *e) {
  plot_data *p=lv_event_get_user_data(e); lv_obj_t *o=lv_event_get_target(e);
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e); lv_area_t a; lv_obj_get_coords(o,&a);
  int w=lv_area_get_width(&a),h=lv_area_get_height(&a),left=a.x1,top=a.y1;
  lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
  line.color=lv_color_hex(p->grid); line.width=1;
  for(unsigned j=0;j<3;j++) {lv_point_t x={left,top+(int)j*h/2},y={left+w, x.y};lv_draw_line(ctx,&line,&x,&y);}
  bool stack=p->kind==SC_CH_STACK_BAR||p->kind==SC_CH_STACK_AREA;
  bool bar=p->kind==SC_CH_BAR||p->kind==SC_CH_STACK_BAR;
  for(unsigned k=0;k<p->count;k++) {
    line.color=lv_color_hex(p->palette[k]);line.width=3;
    line.dash_width=p->axis[k]?8:0;line.dash_gap=p->axis[k]?6:0;
    for(unsigned i=0;i<p->n;i++) {
      if(!valid_at(p,k,i)||(stack&&!stack_at(p,i))) continue;
      float base=0; if(stack) for(unsigned j=0;j<k;j++) base+=p->values[j][i];
      int x=left+(bar?(int)((2*i+1)*w/(2*p->n)):(p->n>1?(int)(i*w/(p->n-1)):w/2));
      int y=py(p,k,base+p->values[k][i],top,h);
      if(bar) {
        lv_draw_rect_dsc_t fill;lv_draw_rect_dsc_init(&fill);fill.bg_color=lv_color_hex(p->palette[k]);fill.border_width=0;fill.radius=2;
        int bw=w/(int)p->n*3/5;if(bw<2) bw=2;
        int by=py(p,k,base,top,h); if(!stack && p->count>1) {bw/=p->count;x+=(int)k*bw-(int)(p->count-1)*bw/2;}
        lv_area_t rect={x-bw/2,y,x+bw/2,by};if(rect.y1<=rect.y2) lv_draw_rect(ctx,&fill,&rect);
      } else if(!stack && (!i || !valid_at(p,k,i-1)) &&
                (i+1==p->n || !valid_at(p,k,i+1))) {
        /* A new history, or samples separated by gaps, still has a visible
         * measurement without inventing a connecting segment. */
        lv_draw_rect_dsc_t dot;lv_draw_rect_dsc_init(&dot);
        dot.bg_color=line.color;dot.border_width=0;dot.radius=LV_RADIUS_CIRCLE;
        lv_area_t rect={x-3,y-3,x+3,y+3};lv_draw_rect(ctx,&dot,&rect);
      } else if(i && valid_at(p,k,i-1)&&(!stack||stack_at(p,i-1))) {
        float previous=0;if(stack)for(unsigned j=0;j<k;j++)previous+=p->values[j][i-1];
        int px=left+(int)((i-1)*w/(p->n-1));
        lv_point_t from={px,py(p,k,previous+p->values[k][i-1],top,h)},to={x,y};
        if(stack) {
          lv_point_t poly[4]={from,to,{x,py(p,k,base,top,h)},{px,py(p,k,previous,top,h)}};
          lv_draw_rect_dsc_t fill;lv_draw_rect_dsc_init(&fill);fill.bg_color=lv_color_hex(p->palette[k]);fill.border_width=0;
          lv_draw_polygon(ctx,&fill,poly,4);
        } else lv_draw_line(ctx,&line,&from,&to);
      }
    }
  }
}
static bool prepare_plot(plot_data *p,const screen_model *m,const scene_item *it,
                          const scene_data *d,uint32_t now) {
  memset(p,0,sizeof(*p));p->kind=it->chart;p->grid=GRID;memcpy(p->palette,colors,sizeof(colors));
  if(m->scene.schema!=3) p->kind=it->type==SC_BAR?SC_CH_BAR:it->type==SC_PIE?SC_CH_RING:SC_CH_LINE;
  p->count=(p->kind==SC_CH_LINE||p->kind==SC_CH_BAR)?1:it->series_count?it->series_count:2;
  if(p->count>3)p->count=3;
  unsigned binding[3]={it->binding,it->binding2,it->binding3};
  if(it->binding==SC_BIND_EXTERNAL) {
    if(!d||!scene_data_fresh(d,now))return false;
    if(d->series_count) {
      p->count=d->series_count;p->n=d->count;
      for(unsigned k=0;k<p->count;k++) {
        memcpy(p->values[k],d->series[k],p->n*sizeof(float));p->mask[k][0]=d->masks[k];
        strcpy(p->names[k],d->names[k]);strcpy(p->units[k],d->units[k]);
      }
    } else {
      p->count=1;p->n=d->count>32?32:d->count;
      for(unsigned i=0;i<p->n;i++){p->values[0][i]=d->values[(uint64_t)i*d->count/p->n];p->mask[0][i/32]|=1u<<(i%32);}
      snprintf(p->names[0],32,"%.31s",it->title);snprintf(p->units[0],16,"%.15s",it->unit);
    }
  } else {
    unsigned hours=it->hours?it->hours:24,minutes=hours*60;
    /* Stretch recorded history across the plot until the configured maximum
     * window is available. One-minute recording cadence remains unchanged. */
    unsigned span=1;
    if(m->count) {
      unsigned age=now/60000-m->history[(m->head+SCREEN_HISTORY-m->count)%SCREEN_HISTORY].minute;
      if(age>=minutes)span=minutes;else if(age>=span)span=age+1;
    }
    if(span<minutes)minutes=span;
    p->minutes=minutes;
    unsigned limit=it->points?it->points:32;
    p->n=minutes<limit?minutes:limit;
    for(unsigned k=0;k<p->count;k++) {
      unsigned b=binding[k];if(b>=SCREEN_METRICS||b==MET_HCHO)return false;
      snprintf(p->names[k],32,"%.31s",screen_translate(english,metric_name(b)));strcpy(p->units[k],metric_unit(b));
      for(unsigned i=0;i<p->n;i++) {
        uint32_t end=(uint32_t)((uint64_t)(i+1)*minutes*60000/p->n);
        uint32_t begin=(uint32_t)((uint64_t)i*minutes*60000/p->n);
        double sum=0;unsigned n=0;bool gap=false;
        for(unsigned j=0;j<m->count;j++) {
          const screen_point *point=&m->history[(m->head+SCREEN_HISTORY-m->count+j)%SCREEN_HISTORY];
          uint32_t age=now/60000-point->minute;
          if(age*60000<minutes*60000-end || age*60000>=minutes*60000-begin) continue;
          if(point->valid&(1u<<b)){sum+=point->values[b];n++;}else gap=true;
        }
        if(n&&!gap){p->values[k][i]=sum/n;p->mask[k][i/32]|=1u<<(i%32);}
        else if(i+1==p->n && !n && screen_sample_valid(m,b,now)) {
          p->values[k][i]=m->samples[b].value;p->mask[k][i/32]|=1u<<(i%32);
        }
      }
    }
  }
  if(!p->n)return false;
  bool additive=p->kind==SC_CH_STACK_BAR||p->kind==SC_CH_STACK_AREA||p->kind==SC_CH_RING;
  bool have[3]={false,false,false};
  unsigned axes=1;
  for(unsigned k=0;k<p->count;k++) {
    if(k&&strcmp(p->units[0],p->units[k])) {
      if(additive||(p->kind!=SC_CH_DUAL&&p->kind!=SC_CH_MULTI))return false;
      unsigned j=0;for(;j<k;j++)if(!strcmp(p->units[j],p->units[k]))break;
      if(j<k)p->axis[k]=p->axis[j];
      else {if(axes>=(p->kind==SC_CH_DUAL?2u:3u))return false;p->axis[k]=axes++;}
    }
    for(unsigned i=0;i<p->n;i++) if(valid_at(p,k,i)) {
      float value=p->values[k][i];
      if(additive&&value<0)return false;
      if(additive) {if(!stack_at(p,i))continue;value=0;for(unsigned j=0;j<p->count;j++) value+=p->values[j][i];}
      unsigned a=p->axis[k];
      if(!have[a]){p->lo[a]=p->hi[a]=value;have[a]=true;}
      if(value<p->lo[a])p->lo[a]=value;
      if(value>p->hi[a])p->hi[a]=value;
    }
  }
  for(unsigned a=0;a<3;a++) {
    if(additive||p->kind==SC_CH_BAR)p->lo[a]=0;
    if(p->hi[a]<=p->lo[a])p->hi[a]=p->lo[a]+1;
    p->hi[a]+=(p->hi[a]-p->lo[a])*.08f;
  }
  return have[0]||have[1]||have[2];
}
static void plot_range(char *b,size_t n,const plot_data *p,const scene_item *it) {
  unsigned minutes=p->minutes?p->minutes:(it->hours?it->hours:24)*60;
  if(minutes<60)snprintf(b,n,"%um",minutes);
  else if(minutes%60)snprintf(b,n,"%.1fh",minutes/60.0);
  else snprintf(b,n,"%uh",minutes/60);
}
static void chart(lv_obj_t *c,const screen_model *m,const scene_item *it,
                   const scene_data *d,uint32_t now,int w,int h) {
  if(plot_used>=sizeof(plots)/sizeof(plots[0]))return;
  plot_data *p=&plots[plot_used++];
  bool compact=it->h<4;
  const int plot_left=UI_COLUMN_PITCH-12,summary_width=plot_left-UI_GRID_GAP;
  int reclaim=it->hide_title?52:0;
  /* Keep chart geometry bounded at the minimum supported size. */
  if(w<160||h<160){label(c,0,0,w,h,it->title,&zh32,wc(1));return;}
  if(compact&&!it->hide_title)label(c,0,0,summary_width,40,it->title,screen_ui_font(28,false),wc(1));
  if(!prepare_plot(p,m,it,d,now)) {
    label(c,compact?plot_left:0,64,compact?w-plot_left:w,80,d&&d->expired?"已过期":"--",&zh32,wc(3));return;
  }
  char b[96];int legend=h-(int)p->count*38;
  if(compact) {
    int plot_x=plot_left,plot_w=w-plot_x,plot_h=h-44;
    if(p->kind==SC_CH_RING) {
      unsigned last=p->n-1;double total=0;
      if(stack_at(p,last))for(unsigned k=0;k<p->count;k++)total+=p->values[k][last];
      if(total>0) {
        int side=plot_h<plot_w?plot_h:plot_w;double sum=0;unsigned start=0;
        for(unsigned k=0;k<p->count;k++) {
          sum+=p->values[k][last];unsigned end=(unsigned)(360*sum/total);
          lv_obj_t *arc=lv_arc_create(c);lv_obj_set_pos(arc,plot_x+(plot_w-side)/2,8);lv_obj_set_size(arc,side,side);
          lv_arc_set_rotation(arc,270);lv_arc_set_bg_angles(arc,0,360);lv_arc_set_angles(arc,start,end);
          lv_obj_set_style_arc_opa(arc,LV_OPA_TRANSP,LV_PART_MAIN);
          lv_obj_set_style_arc_color(arc,lv_color_hex(colors[k]),LV_PART_INDICATOR);
          lv_obj_set_style_arc_width(arc,24,LV_PART_INDICATOR);lv_obj_set_style_arc_rounded(arc,false,LV_PART_INDICATOR);
          lv_obj_remove_style(arc,NULL,LV_PART_KNOB);lv_obj_clear_flag(arc,LV_OBJ_FLAG_CLICKABLE);start=end;
        }
      } else label(c,plot_x,64,plot_w,40,"--",&zh32,wc(3));
    } else {
      lv_obj_t *plot=box(c,plot_x,8,plot_w,plot_h,CARD,0);
      lv_obj_set_style_bg_opa(plot,LV_OPA_TRANSP,0);
      lv_obj_add_event_cb(plot,draw_plot,LV_EVENT_DRAW_MAIN,p);
    }
    for(unsigned k=0;k<p->count;k++) {
      char value[32]="--";if(valid_at(p,k,p->n-1))number(value,sizeof(value),p->values[k][p->n-1],MET_CO2);
      if(p->count==1) {
        label(c,0,it->hide_title?10:58,summary_width,64,value,fit_font(value,48,true,summary_width,64),custom&&(custom->mask&4)?wc(2):colors[k]);
        label(c,0,it->hide_title?78:126,summary_width,36,p->units[k],screen_ui_font(24,false),wc(3));
      } else {
        int y=(it->hide_title?0:38)+(int)k*(h-(it->hide_title?0:38))/(int)p->count;
        label(c,0,y,summary_width,26,p->names[k],screen_ui_font(20,false),legend_color(k));
        snprintf(b,sizeof(b),"%s %s",value,p->units[k]);
        label(c,0,y+26,summary_width,30,b,screen_ui_font(20,false),wc(2));
      }
    }
    plot_range(b,sizeof(b),p,it);
    lv_obj_t *range=label(c,plot_x,h-28,plot_w,28,b,screen_ui_font(24,false),wc(3));
    lv_obj_set_style_text_align(range,LV_TEXT_ALIGN_RIGHT,0);return;
  }
  if(p->kind==SC_CH_RING) {
    double total=0;unsigned last=p->n-1;for(unsigned k=0;k<p->count;k++)if(valid_at(p,k,last))total+=p->values[k][last];
    if(total<=0){label(c,0,64,w,80,"暂无有效数据",&zh32,wc(3));return;}
    int side=legend-54+reclaim;if(side>w)side=w;double sum=0;unsigned start=0;
    for(unsigned k=0;k<p->count;k++) {
      if(!valid_at(p,k,last))continue;
      sum+=p->values[k][last];unsigned end=(unsigned)(360*sum/total);
      lv_obj_t *arc=lv_arc_create(c);lv_obj_set_pos(arc,(w-side)/2,48-(reclaim?40:0));lv_obj_set_size(arc,side,side);
      lv_arc_set_rotation(arc,270);lv_arc_set_bg_angles(arc,0,360);lv_arc_set_angles(arc,start,end);
      lv_obj_set_style_arc_opa(arc,LV_OPA_TRANSP,LV_PART_MAIN);
      lv_obj_set_style_arc_color(arc,lv_color_hex(colors[k]),LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(arc,30,LV_PART_INDICATOR);lv_obj_set_style_arc_rounded(arc,false,LV_PART_INDICATOR);
      lv_obj_remove_style(arc,NULL,LV_PART_KNOB);lv_obj_clear_flag(arc,LV_OBJ_FLAG_CLICKABLE);start=end;
    }
  } else {
    int right=p->axis[1]||p->axis[2]?80:8;
    int plot_height=legend-140+reclaim;
    lv_obj_t *plot=box(c,82,60-reclaim,w-82-right,plot_height,CARD,0);
    lv_obj_set_style_bg_opa(plot,LV_OPA_TRANSP,0);
      lv_obj_add_event_cb(plot,draw_plot,LV_EVENT_DRAW_MAIN,p);
    for(unsigned a=0;a<2;a++) {
      if(a&&!p->axis[1]&&!p->axis[2])continue;
      for(unsigned i=0;i<3;i++) {
        float v=p->hi[a]-(p->hi[a]-p->lo[a])*i/2;
        if(fabsf(v)>=10000)snprintf(b,sizeof(b),"%.0fk",(double)v/1000);
        else if(fabsf(v)>=1000)snprintf(b,sizeof(b),"%.1fk",(double)v/1000);
        else if(fabsf(v)>=10)snprintf(b,sizeof(b),"%.0f",(double)v);
        else snprintf(b,sizeof(b),"%.1f",(double)v);
        label(c,a?w-right:0,(reclaim?0:44)+i*plot_height/2,78,40,b,&zh32,wc(3));
      }
    }
    plot_range(b,sizeof(b),p,it);
    label(c,82,legend-44,w-90,40,b,&zh32,wc(3));
  }
  for(unsigned k=0;k<p->count;k++) {
    char value[32]="--";if(valid_at(p,k,p->n-1))number(value,sizeof(value),p->values[k][p->n-1],MET_CO2);
    if(p->axis[2]==2)snprintf(b,sizeof(b),"%s %s %s [%.0f-%.0f]",p->names[k],value,p->units[k],(double)p->lo[p->axis[k]],(double)p->hi[p->axis[k]]);
    else snprintf(b,sizeof(b),"%s %s %s",p->names[k],value,p->units[k]);
    label(c,0,legend+k*38,w,38,b,p->axis[2]==2?screen_ui_font(24,false):&zh32,legend_color(k));
  }
}
static lv_obj_t *component(lv_obj_t *root,const screen_model *m,const scene_item *it,uint32_t now,lv_obj_t *existing) {
  if(existing&&it->type==SC_IMAGE&&stream.width&&stream.height) {
    lv_obj_t *clip=lv_obj_get_child(existing,-1);
    lv_obj_t *image=clip?lv_obj_get_child(clip,0):NULL;
    if(image&&lv_obj_check_type(image,&lv_img_class)&&
       lv_obj_get_width(image)==(int)stream.width&&lv_obj_get_height(image)==(int)stream.height) {
      /* The decoded arena changes in place. Keep the card and its letterbox
       * intact; invalidate only the transformed image's visible bounds. */
      lv_img_cache_invalidate_src(&stream_image_descriptor);
      lv_obj_invalidate(image);
      return existing;
    }
  }
  int x=UI_MARGIN+it->x*UI_COLUMN_PITCH,y=UI_GRID_TOP+it->y*UI_ROW_PITCH;
  int w=it->w*UI_COLUMN_PITCH-UI_GRID_GAP,h=it->h*UI_ROW_PITCH-UI_GRID_GAP;
  bool clock=it->type==SC_CLOCK;
  custom=appearance_find(appearance,it->id);
  lv_obj_t *c=existing?existing:box(root,x,y,w,h,wc(0),12);
  reuse_labels=(clock||it->type==SC_VALUE)?c:NULL;reuse_index=0;
  if(existing&&!reuse_labels)lv_obj_clean(c);
  unsigned alpha=it->type==SC_IMAGE?0:appearance_opacity(appearance,custom,it->type,it->h,have_wallpaper);
  lv_obj_set_style_bg_opa(c,alpha*255/100,0);
  if(have_wallpaper) {lv_obj_set_style_border_width(c,it->type==SC_IMAGE?0:1,0);lv_obj_set_style_border_color(c,lv_color_hex(appearance_color(appearance,AP_LINE)),0);lv_obj_set_style_border_opa(c,alpha*100/100,0);}
  int pad=clock?0:12;w-=pad*2;h-=24;
  lv_obj_set_style_pad_left(c,pad,0);lv_obj_set_style_pad_top(c,12,0);
  const scene_data *d=data_for(m,it->id);bool fresh=scene_data_fresh(d,now);
  char b[256];
  if(clock) {
    bool compact=it->h==1;int time_width=compact?w*3/5:w;
    const lv_font_t *time_font=compact||it->w<4?&clock72:&clock_font;
    snprintf(b,sizeof(b),m->clock_valid?"%02u:%02u":"--:--",m->hour,m->minute);
    lv_obj_t *o=label(c,0,compact?(h-time_font->line_height)/2:10,time_width,compact?time_font->line_height:it->w==4?144:100,b,time_font,wc(2));
    lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_letter_space(o,-2,0);
    if(m->clock_valid) {
      /* Gregorian civil date from days since 1970; independent of libc TZ. */
      unsigned z=m->local_day+719468,era=z/146097,doe=z-era*146097;
      unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
      unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
      unsigned day=doy-(153*mp+2)/5+1,month=mp<10?mp+3:mp-9;
      static const char *days[]={"日","一","二","三","四","五","六"};
      if(english) {
        static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        static const char *week[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        snprintf(b,sizeof(b),"%s %u · %s",months[month-1],day,week[(m->local_day+4)%7]);
      } else snprintf(b,sizeof(b),"%u月%u日 · 周%s",month,day,days[(m->local_day+4)%7]);
    } else strcpy(b,"-- / --");
    int date_x=compact?time_width+12:0,date_width=w-date_x;
    o=label(c,date_x,compact?(h-40)/2:it->w==4?164:114,date_width,40,b,compact?fit_font(b,28,false,date_width,40):&zh32,wc(3));lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    return c;
  }
  bool full=it->w==4&&it->h==6;
  const char *title=it->hide_title?"":it->type==SC_WEATHER&&m->weather.location[0]?m->weather.location:it->title;
  bool wide_weather=it->type==SC_WEATHER&&it->w==4&&it->h<=2;
  if(title[0]&&it->type!=SC_VALUE&&!(wide_weather&&it->h==1)&&(!(it->type==SC_LINE||it->type==SC_BAR||it->type==SC_PIE)||it->h>=4)&&
     (!full||strcmp(title,m->scene.page[it->page].title)))label(c,0,0,it->type==SC_WEATHER&&!full&&!wide_weather?w-46:w,40,title,&zh32,wc(1));
  if(it->type==SC_VALUE) {
    bool local=it->binding<SCREEN_METRICS;
    bool ok=local?screen_sample_valid(m,it->binding,now):fresh&&d->count;
    if(it->binding==MET_HCHO)ok=false;
    float v=local?m->samples[it->binding].value:ok?d->values[0]:0;
    bool grade=it->value_mode==SC_VALUE_GRADE||
               (local&&it->binding==MET_TVOC&&it->value_mode==SC_VALUE_AUTO);
    if(local&&it->binding==MET_TVOC&&grade) strcpy(b,screen_tvoc_text(ok?m->tvoc_level:0));
    else if(grade)snprintf(b,sizeof(b),"%s",fresh&&d->text[0]?d->text:"--");
    else if(ok) number(b,sizeof(b),v,it->binding);else strcpy(b,"--");
    unsigned color=ok?wc(2):MUTED;
    if(it->binding==MET_TVOC&&m->tvoc_level==3)color=appearance_color(appearance,AP_DANGER);
    const char *unit=it->unit;if(local)unit=metric_unit(it->binding);
    if(grade||it->binding==MET_HCHO)unit="";
    value_card(c,it,title,!local&&d&&!fresh?"已过期":unit,b,grade,color,w,h);
  } else if(it->type==SC_TEXT) {
    const char *body=fresh?d->text:it->text[0]?it->text:d?"已过期":"--";
    int top=it->hide_title?0:52;
    label(c,0,top,w,h-top,body,&zh32,fresh||it->text[0]?wc(4):MUTED);
  } else if(it->type==SC_LINE||it->type==SC_BAR||it->type==SC_PIE) {
    chart(c,m,it,d,now,w,h);
  } else if(it->type==SC_WEATHER) {
    weather_card(c,&m->weather,it,w,h);
  } else if(it->type==SC_REMINDERS) {
    unsigned row=0;int top=it->hide_title?0:52;
    for(unsigned i=0;i<REMINDER_MAX;i++)if(m->reminders[i].unread&&top+(int)row*174<h-40) {
      const reminder_notice *r=&m->reminders[i];int y=top+row*174;
      label(c,0,y,w,42,r->title,&zh32,wc(1));label(c,0,y+48,w,h-y-48<108?h-y-48:108,r->text,&zh32,wc(4));row++;
    }
    if(!row)label(c,0,64,w,80,"暂无提醒",&zh32,MUTED);
  } else if(it->type==SC_IMAGE) {
    int top=it->hide_title?0:48;
    if(stream.width&&stream.height)picture(c,&stream_image_descriptor,stream.pixels,stream.width,stream.height,0,top,w,h-top,it->image_fit!=0);
    else label(c,0,top,w,h-top,english?"Waiting for camera":"等待摄像头画面",&zh32,MUTED);
  }
  return c;
}
static uint64_t hash_bytes(uint64_t h,const void *v,size_t n) {
  const uint8_t *p=v;while(n--){h^=*p++;h*=1099511628211ULL;}return h;
}
/* Exclude sample timestamps and unrelated pages. Freshness, minute history,
 * TTL expiry and every visible payload still invalidate the cached tree. */
static uint64_t structure_hash(const screen_model *m) {
  uint64_t hash=14695981039346656037ULL;
#define HASH(value) hash=hash_bytes(hash,&(value),sizeof(value))
  HASH(screen_font_resources.state);HASH(screen_font_resources.crc);
  HASH(m->appearance);HASH(wallpaper.kind);HASH(wallpaper.ready);HASH(wallpaper.crc);
  HASH(m->page);HASH(m->scene);HASH(m->ota);
#undef HASH
  return hash;
}
static uint64_t content_hash(const screen_model *m,const scene_item *it,uint32_t now) {
  uint64_t hash=14695981039346656037ULL;
#define HASH(value) hash=hash_bytes(hash,&(value),sizeof(value))
    if(it->type==SC_CLOCK){HASH(m->clock_valid);HASH(m->hour);HASH(m->minute);HASH(m->local_day);}
    else if(it->type==SC_VALUE&&it->binding<SCREEN_METRICS) {
      bool valid=screen_sample_valid(m,it->binding,now);HASH(valid);
      char text[32]="--";
      bool grade=it->binding==MET_TVOC&&it->value_mode!=SC_VALUE_NUMBER;
      if(valid&&!grade)number(text,sizeof(text),m->samples[it->binding].value,it->binding);
      hash=hash_bytes(hash,text,strlen(text));if(it->binding==MET_TVOC)HASH(m->tvoc_level);
    } else if(it->type==SC_WEATHER) {
      HASH(m->weather.valid);HASH(m->weather.expired);
      /* Provider refresh timestamps do not change the displayed forecast. */
      hash=hash_bytes(hash,&m->weather.fields,sizeof(m->weather)-offsetof(screen_weather,fields));
    }
    else if(it->type==SC_REMINDERS)hash=hash_bytes(hash,m->reminders,sizeof(m->reminders));
    else if(it->type==SC_IMAGE){const media_notice *notice=media_current(&media);if(!notice||!notice->source[0]){HASH(stream.pixel_crc);HASH(media.source_ready);HASH(stream.width);HASH(stream.height);}}
    if((it->type==SC_LINE||it->type==SC_BAR||it->type==SC_PIE)&&it->binding!=SC_BIND_EXTERNAL) {
      uint32_t minute=now/60000;HASH(minute);HASH(m->head);HASH(m->count);
      unsigned bindings[]={it->binding,it->binding2,it->binding3};
      for(unsigned k=0;k<(it->series_count?it->series_count:1)&&k<3;k++) {
        unsigned b=bindings[k];bool valid=screen_sample_valid(m,b,now);HASH(valid);
        /* Once history exists, plots represent minute samples. A live sensor
         * packet must not repeatedly rescan 24 hours or redraw every chart. */
        if(valid&&!m->count){char text[32];number(text,sizeof(text),m->samples[b].value,b);hash=hash_bytes(hash,text,strlen(text));}
      }
    }
    const scene_data *d=data_for(m,it->id);bool fresh=scene_data_fresh(d,now);HASH(fresh);
    if(d){HASH(d->count);hash=hash_bytes(hash,d->text,sizeof(*d)-offsetof(scene_data,text));}
#undef HASH
  return hash;
}
static void draw_network(lv_event_t *e) {
  bool online=*(const bool *)lv_event_get_user_data(e);
  lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
  lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);
  lv_draw_arc_dsc_t arc;lv_draw_arc_dsc_init(&arc);
  arc.color=lv_color_hex(online?colors[0]:MUTED);arc.width=3;arc.rounded=1;
  lv_point_t center={a.x1+16,a.y1+27};
  for(unsigned radius=8;radius<=20;radius+=6)lv_draw_arc(ctx,&arc,&center,radius,225,315);
  lv_draw_rect_dsc_t dot;lv_draw_rect_dsc_init(&dot);
  dot.bg_color=arc.color;dot.radius=LV_RADIUS_CIRCLE;
  lv_area_t point={center.x-1,center.y-1,center.x+1,center.y+1};lv_draw_rect(ctx,&dot,&point);
  if(!online) {
    lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
    line.color=arc.color;line.width=2;line.round_start=line.round_end=1;
    lv_point_t from={a.x1+3,a.y1+4},to={a.x1+29,a.y1+29};lv_draw_line(ctx,&line,&from,&to);
  }
}
static void draw_charging(lv_event_t *e) {
  lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
  lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);
  line.color=lv_color_hex(INK);line.width=3;line.round_start=line.round_end=1;
  lv_point_t points[]={{a.x1+11,a.y1+2},{a.x1+5,a.y1+11},{a.x1+12,a.y1+9},{a.x1+6,a.y1+18}};
  for(unsigned i=1;i<4;i++)lv_draw_line(lv_event_get_draw_ctx(e),&line,&points[i-1],&points[i]);
}
/* This layer is independent of the business page. Stable LVGL objects let
 * text, queue metadata and live pixels invalidate only their own rectangles. */
static bool notification_ui(lv_obj_t *root,const screen_model *m,bool reset) {
  static struct {
    lv_obj_t *overlay,*heading,*back[2],*card,*title,*text,*loading,*image,*dots[5];
    unsigned count,view,width,height,image_height;
    uint32_t crc;
    bool remote;
  } n;
  if(reset)memset(&n,0,sizeof(n));
  const media_notice *notice=media_current(&media);
  if(!notice||media.suspended||m->ota) {
    if(!n.overlay)return false;
    lv_obj_del(n.overlay);memset(&n,0,sizeof(n));return true;
  }
  bool created=!n.overlay,changed=created;
  if(created) {
    n.overlay=box(root,0,0,480,854,BG,0);
    n.heading=label(n.overlay,24,24,432,44,"",&zh32,INK);
    n.back[0]=box(n.overlay,40,80,400,660,lv_color_to32(lv_color_mix(lv_color_hex(CARD),lv_color_hex(BG),110))&0xffffff,28);
    n.back[1]=box(n.overlay,28,96,424,660,lv_color_to32(lv_color_mix(lv_color_hex(CARD),lv_color_hex(BG),180))&0xffffff,28);
    n.card=box(n.overlay,16,112,448,662,CARD,28);
    n.title=label(n.card,20,20,408,80,"",&zh32,INK);
    n.text=label(n.card,20,112,408,526,"",screen_ui_font(28,false),INK);
    n.loading=label(n.card,20,108,408,44,english?"Loading image…":"正在加载画面…",&zh32,MUTED);
    label(n.overlay,24,786,432,25,english?"Tap to dismiss · Double-tap to switch":"单击确认 · 双击切换",screen_ui_font(20,false),MUTED);
    for(unsigned i=0;i<5;i++)n.dots[i]=box(n.overlay,0,830,12,12,GRID,6);
  }
  unsigned count=media_count(&media),view=media_view(&media),limit=count<MEDIA_STACK?count:MEDIA_STACK;
  if(created||n.count!=count||n.view!=view) {
    char heading[64];snprintf(heading,sizeof(heading),english?"Notifications  %u/%u":"通知  %u/%u",view,count);
    lv_label_set_text(n.heading,heading);
    for(unsigned i=0;i<2;i++) {
      bool show=count>(i?1u:2u);
      if(show)lv_obj_clear_flag(n.back[i],LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(n.back[i],LV_OBJ_FLAG_HIDDEN);
    }
    int left=(480-(int)(limit+1)*28)/2;
    for(unsigned i=0;i<5;i++) {
      if(i<=limit)lv_obj_clear_flag(n.dots[i],LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(n.dots[i],LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(n.dots[i],left+(int)i*28+8);
      lv_obj_set_style_bg_color(n.dots[i],lv_color_hex(i==view?INK:GRID),0);
    }
    n.count=count;n.view=view;changed=true;
  }
  const char *title=screen_translate(english,notice->title),*text=screen_translate(english,notice->text);
  if(strcmp(lv_label_get_text(n.title),title)) {lv_label_set_text(n.title,title);changed=true;}
  bool remote=notice->source[0]!=0;
  unsigned width=remote?(media.source_ready?stream.width:0):media.width;
  unsigned height=remote?(media.source_ready?stream.height:0):media.height;
  unsigned image_height=text[0]?280:520;
  bool layout=created||width!=n.width||height!=n.height||remote!=n.remote||image_height!=n.image_height;
  if(layout) {
    if(n.image) {lv_obj_del(lv_obj_get_parent(n.image));n.image=NULL;}
    if(width&&height)n.image=picture(n.card,&media_image_descriptor,remote?stream.pixels:media.pixels,width,height,12,108,424,image_height,false);
    if(remote&&(!width||!height))lv_obj_clear_flag(n.loading,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(n.loading,LV_OBJ_FLAG_HIDDEN);
    n.width=width;n.height=height;n.remote=remote;n.image_height=image_height;changed=true;
  }
  int text_top=(width&&height)?108+(int)image_height+12:remote?168:112;
  if(layout||strcmp(lv_label_get_text(n.text),text)) {
    int available=638-text_top;unsigned size=28;lv_point_t bounds;
    while(size>16) {lv_txt_get_size(&bounds,text,screen_ui_font(size,false),0,0,408,LV_TEXT_FLAG_NONE);if(bounds.y<=available)break;size-=2;}
    lv_label_set_text(n.text,text);lv_obj_set_y(n.text,text_top);lv_obj_set_height(n.text,available);
    lv_obj_set_style_text_font(n.text,screen_ui_font(size,false),0);changed=true;
  }
  uint32_t crc=remote?stream.pixel_crc:media.pixel_crc;
  if(n.image&&(layout||n.crc!=crc)) {
    lv_img_cache_invalidate_src(&media_image_descriptor);lv_obj_invalidate(n.image);changed=true;
  }
  n.crc=crc;
  return changed;
}
bool screen_ui(const screen_model *m,uint32_t now,bool demo) {
  english=m->appearance.language==1;
  (void)demo;static uint64_t previous;static bool have;
  static struct {lv_obj_t *card;uint64_t content;} visible[8];
  static lv_obj_t *network,*battery_fill,*charging;
  static bool network_on,battery_valid,battery_charging;
  static unsigned battery_percent;
  lv_obj_t *root=lv_scr_act();
  if(m->asleep&&!m->ota)return false;
  uint64_t hash=structure_hash(m);
  bool rebuild=!have||hash!=previous||!lv_obj_get_child_cnt(root),changed=rebuild;
  if(rebuild){previous=hash;have=true;lv_obj_clean(root);memset(visible,0,sizeof(visible));}
  appearance=&m->appearance;custom=NULL;
  BG=appearance_color(appearance,AP_CANVAS);CARD=appearance_color(appearance,AP_BACKGROUND);
  INK=appearance_color(appearance,AP_VALUE);MUTED=appearance_color(appearance,AP_UNIT);GRID=appearance_color(appearance,AP_LINE);
  for(unsigned k=0;k<3;k++)colors[k]=appearance_color(appearance,AP_CHART_A+k);
  have_wallpaper=appearance->wallpaper&&wallpaper.ready&&wallpaper.kind==appearance->wallpaper&&
                 (wallpaper.kind!=4||wallpaper.crc==appearance->custom_crc);
  if(rebuild) {
  static lv_img_dsc_t image={.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=480,.h=854},.data_size=480*854*2};
  image.data=(const uint8_t*)wallpaper.pixels;
  lv_obj_set_style_bg_img_src(root,have_wallpaper?&image:NULL,0);
  if(have_wallpaper&&appearance->dim) {lv_obj_t *dim=box(root,0,0,480,854,0,0);lv_obj_set_style_bg_opa(dim,appearance->dim*255/100,0);}
  lv_obj_set_style_bg_color(root,lv_color_hex(BG),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
  lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
  unsigned page=m->page<m->scene.pages?m->page:0;
  bool has_clock=false;
  for(unsigned i=0;i<m->scene.count;i++)if(m->scene.item[i].page==page&&m->scene.item[i].type==SC_CLOCK&&m->scene.item[i].y==0)has_clock=true;
  int title_y=appearance_rail_height(appearance)+2;
  if(title_y<UI_MARGIN)title_y=UI_MARGIN;
  if(!has_clock)label(root,UI_MARGIN,title_y,314,UI_HEADER_HEIGHT,m->scene.page[page].title,&zh32,MUTED);
  network=box(root,SCREEN_WIDTH-UI_MARGIN-128,UI_MARGIN+2,32,32,BG,0);
  lv_obj_set_style_bg_opa(network,LV_OPA_TRANSP,0);
  lv_obj_add_event_cb(network,draw_network,LV_EVENT_DRAW_MAIN,&network_on);
  lv_obj_t *battery=box(root,SCREEN_WIDTH-UI_MARGIN-78,UI_MARGIN+6,72,27,GRID,5);
  battery_fill=box(battery,3,3,1,21,colors[0],2);
  charging=box(battery,27,3,18,21,GRID,2);
  lv_obj_set_style_bg_opa(charging,LV_OPA_TRANSP,0);
  lv_obj_add_event_cb(charging,draw_charging,LV_EVENT_DRAW_MAIN,NULL);
  box(root,SCREEN_WIDTH-UI_MARGIN-5,UI_MARGIN+13,5,13,GRID,1);
  }
  const media_notice *notice=media_current(&media);
  bool covered=notice&&!media.suspended&&!m->ota;
  /* The opaque notification owns the visible surface. Keep business objects
   * intact and catch their data up once when the overlay is removed. */
  if(!covered||rebuild) {
  if(rebuild||network_on!=m->network) {
    network_on=m->network;lv_obj_invalidate(network);changed=true;
  }
  if(rebuild||battery_charging!=m->charging) {
    battery_charging=m->charging;
    if(battery_charging)lv_obj_clear_flag(charging,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(charging,LV_OBJ_FLAG_HIDDEN);
    changed=true;
  }
  if(rebuild||battery_valid!=m->battery_valid||battery_percent!=m->battery_percent) {
    battery_valid=m->battery_valid;battery_percent=m->battery_percent;
    unsigned width=(battery_percent>100?100:battery_percent)*66/100;
    lv_obj_set_width(battery_fill,width?width:1);
    lv_obj_set_style_bg_opa(battery_fill,battery_valid&&width?LV_OPA_COVER:LV_OPA_TRANSP,0);changed=true;
  }
  unsigned page=m->page<m->scene.pages?m->page:0,slot=0;
  for(unsigned i=0;i<m->scene.count&&slot<8;i++)if(m->scene.item[i].page==page) {
    const scene_item *it=&m->scene.item[i];uint64_t content=content_hash(m,it,now);
    if(rebuild||visible[slot].content!=content) {
      plot_used=slot; /* Stable plot storage for draw callbacks on other cards. */
      visible[slot].card=component(root,m,it,now,visible[slot].card);
      visible[slot].content=content;changed=true;
    }
    slot++;
  }
  }
  reuse_labels=NULL;
  if(rebuild) {
  unsigned total=0;for(unsigned i=0;i<m->scene.pages;i++)total+=m->scene.page[i].enabled;
  unsigned page=m->page<m->scene.pages?m->page:0;
  unsigned j=0;for(unsigned i=0;i<m->scene.pages;i++)if(m->scene.page[i].enabled) {
    /* Integer boundaries cover every edge pixel, even with seven pages. The
     * rail overlays the top edge and reserves no row beneath the widgets. */
    int left=SCREEN_WIDTH*j/total,right=SCREEN_WIDTH*(j+1)/total;
    bool active=i==page;j++;
    lv_obj_t *segment=box(root,left,0,right-left,appearance_rail_height(appearance),
                         active?appearance_color(appearance,AP_ACCENT):MUTED,0);
    lv_obj_set_style_bg_opa(segment,(active?UI_PAGE_RAIL_ACTIVE_OPACITY:UI_PAGE_RAIL_IDLE_OPACITY)*255/100,0);
  }
  if(m->ota){lv_obj_t *o=box(root,32,300,416,160,CARD,20);label(o,16,24,384,110,"正在更新\n请保持供电",&zh32,colors[0]);}
  }
  changed=notification_ui(root,m,rebuild)||changed;
  return changed;
}
