/* Native 32px grayscale from verified SDRAM; internal common glyphs and a
 * visible missing-character placeholder remain available without NOR. */
#include "lvgl.h"
#include "resources.h"
LV_FONT_DECLARE(zh16);
static uint8_t bitmap[512];
static bool glyph(const lv_font_t *f,lv_font_glyph_dsc_t *out,uint32_t c,uint32_t next) {
  (void)f;
  if(c<0x20)return false; /* Let LVGL handle line breaks without a placeholder. */
  resource_glyph g;
  if(resources_glyph(&screen_font_resources,c,&g)){
    *out=(lv_font_glyph_dsc_t){.adv_w=g.advance,.box_w=g.width,.box_h=g.height,
        .ofs_x=g.x,.ofs_y=g.y,.bpp=g.bpp};return true;
  }
  if(c>=0xff01 && c<=0xff5e)c-=0xfee0;
  if(!lv_font_get_glyph_dsc(&zh16,out,c,next)&&!lv_font_get_glyph_dsc(&zh16,out,'?',0))return false;
  out->adv_w*=2;out->box_w*=2;out->box_h*=2;out->ofs_x*=2;out->ofs_y*=2;
  out->bpp=1;return true;
}
static const uint8_t *pixels(const lv_font_t *f,uint32_t c) {
  (void)f;if(c<0x20)return NULL;resource_glyph g;
  if(resources_glyph(&screen_font_resources,c,&g))return g.pixels;
  if(c>=0xff01 && c<=0xff5e)c-=0xfee0;
  lv_font_glyph_dsc_t d;
  if(!lv_font_get_glyph_dsc(&zh16,&d,c,0)){c='?';if(!lv_font_get_glyph_dsc(&zh16,&d,c,0))return NULL;}
  const uint8_t *src=lv_font_get_glyph_bitmap(&zh16,c);
  unsigned width=d.box_w*2,height=d.box_h*2,size=(width*height+7)/8;
  if(!src||size>sizeof(bitmap))return NULL;
  lv_memset_00(bitmap,size);
  for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
    unsigned old=(y/2)*d.box_w+x/2,bit=y*width+x;
    if(src[old/8]&(0x80u>>(old%8)))bitmap[bit/8]|=0x80u>>(bit%8);
  }
  return bitmap;
}
const lv_font_t zh32={.get_glyph_dsc=glyph,.get_glyph_bitmap=pixels,
  .line_height=36,.base_line=2,.subpx=LV_FONT_SUBPX_NONE,.underline_position=-4,.underline_thickness=2};
/* Grades use the same visual weight as a numeric reading. */
static uint8_t bitmap64[4096];
static bool glyph64(const lv_font_t *f,lv_font_glyph_dsc_t *out,uint32_t c,uint32_t next) {
  if(!glyph(f,out,c,next))return false;
  out->adv_w*=2;out->box_w*=2;out->box_h*=2;out->ofs_x*=2;out->ofs_y*=2;
  return true;
}
static const uint8_t *pixels64(const lv_font_t *f,uint32_t c) {
  lv_font_glyph_dsc_t d;if(!glyph(f,&d,c,0))return NULL;
  const uint8_t *src=pixels(f,c);unsigned w=d.box_w*2,h=d.box_h*2,n=(w*h*d.bpp+7)/8;
  if(!src||n>sizeof(bitmap64))return NULL;
  lv_memset_00(bitmap64,n);
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    unsigned old=y/2*d.box_w+x/2,bit=y*w+x;
    unsigned shift=8-d.bpp-(old*d.bpp)%8,value=(src[old*d.bpp/8]>>shift)&((1u<<d.bpp)-1);
    bitmap64[bit*d.bpp/8]|=value<<(8-d.bpp-(bit*d.bpp)%8);
  }
  return bitmap64;
}
const lv_font_t zh64={.get_glyph_dsc=glyph64,.get_glyph_bitmap=pixels64,
  .line_height=72,.base_line=4,.subpx=LV_FONT_SUBPX_NONE,.underline_position=-8,.underline_thickness=4};
