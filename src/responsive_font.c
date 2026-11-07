#include "responsive_font.h"
#include <string.h>
LV_FONT_DECLARE(zh32);
LV_FONT_DECLARE(metric_font);
enum { FIRST=16, LAST=192, STEP=4, COUNT=(LAST-FIRST)/STEP+1 };
typedef struct {const lv_font_t *base; unsigned pixels, denominator;} scale;
static lv_font_t fonts[2][COUNT];
static scale scales[2][COUNT];
/* Fixed 144 KiB in SDRAM (.bss), never in the internal recovery SRAM/heap.
 * Sixteen LRU slots keep a whole numeric readout and its units hot. LVGL
 * consumes each bitmap before the next request can evict that slot. */
enum { CACHE_SLOTS=16, BITMAP_BYTES=192*192*2/8 };
static struct {
 const lv_font_t *font;
 uint32_t character, age;
 uint8_t pixels[BITMAP_BYTES];
} cache[CACHE_SLOTS];
static uint32_t clock_age;
static screen_font_stats stats;
static bool initialized;
static unsigned scaled(unsigned n,const scale *s) {return (n*s->pixels+s->denominator/2)/s->denominator;}
static int offset(int n,const scale *s) {return n<0?-(int)scaled((unsigned)-n,s):(int)scaled((unsigned)n,s);}
static uint32_t character(uint32_t c) {return c>=0xff01&&c<=0xff5e?c-0xfee0:c;}
static bool glyph(const lv_font_t *f,lv_font_glyph_dsc_t *d,uint32_t c,uint32_t next) {
 const scale *s=f->dsc;
 if(!lv_font_get_glyph_dsc(s->base,d,character(c),character(next))||(d->bpp!=1&&d->bpp!=2))return false;
 unsigned w=scaled(d->box_w,s),h=scaled(d->box_h,s);
 if((w*h*2+7)/8>BITMAP_BYTES)return false;
 d->adv_w=scaled(d->adv_w,s);d->box_w=w;d->box_h=h;
 d->ofs_x=offset(d->ofs_x,s);d->ofs_y=offset(d->ofs_y,s);d->bpp=2;return true;
}
static const uint8_t *pixels(const lv_font_t *f,uint32_t c) {
 c=character(c);
 /* Renormalize on wrap; cache residency never depends on wall-clock time. */
 if(++clock_age==0){for(unsigned i=0;i<CACHE_SLOTS;i++)cache[i].age=0;clock_age=1;}
 unsigned slot=0;
 for(unsigned i=0;i<CACHE_SLOTS;i++) {
  if(cache[i].font==f&&cache[i].character==c) {
   cache[i].age=clock_age;stats.hits++;return cache[i].pixels;
  }
  if(cache[i].age<cache[slot].age)slot=i;
 }
 const scale *s=f->dsc;lv_font_glyph_dsc_t d;
 if(!lv_font_get_glyph_dsc(s->base,&d,character(c),0)||(d.bpp!=1&&d.bpp!=2))return NULL;
 const uint8_t *source=lv_font_get_glyph_bitmap(s->base,character(c));
 unsigned w=scaled(d.box_w,s),h=scaled(d.box_h,s),n=(w*h*2+7)/8;
 if(!source||n>BITMAP_BYTES)return NULL;
 uint8_t *bitmap=cache[slot].pixels;
 memset(bitmap,0,n);
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
  unsigned old=(y*d.box_h/h)*d.box_w+x*d.box_w/w,bit=y*w+x;
  unsigned gray=(source[old*d.bpp/8]>>(8-d.bpp-(old*d.bpp)%8))&((1u<<d.bpp)-1);
  if(d.bpp==1)gray*=3;
  bitmap[bit/4]|=gray<<(6-2*(bit%4));
 }
 if(cache[slot].font)stats.evictions++;
 cache[slot].font=f;cache[slot].character=c;cache[slot].age=clock_age;stats.misses++;
 return bitmap;
}
void screen_ui_font_invalidate(void) {memset(cache,0,sizeof(cache));}
screen_font_stats screen_ui_font_stats(void) {return stats;}
const lv_font_t *screen_ui_font(unsigned size,bool numeric) {
 if(!initialized) {
  for(unsigned k=0;k<2;k++)for(unsigned i=0;i<COUNT;i++) {
   scale *s=&scales[k][i];*s=(scale){k?&metric_font:&zh32,FIRST+i*STEP,k?64:32};
   fonts[k][i]=(lv_font_t){.get_glyph_dsc=glyph,.get_glyph_bitmap=pixels,
    .line_height=scaled(s->base->line_height,s),.base_line=scaled(s->base->base_line,s),
    .subpx=LV_FONT_SUBPX_NONE,.dsc=s};
  }
  initialized=true;
 }
 if(size<FIRST)size=FIRST;
 if(size>LAST)size=LAST;
 return &fonts[numeric?1:0][(size-FIRST)/STEP];
}
