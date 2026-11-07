#include "appearance.h"
#include "scene.h"
#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 if(argc==3&&!strcmp(argv[1],"preset")) {
  unsigned kind=atoi(argv[2]);if(kind<1||kind>3)return 2;
  for(unsigned y=0;y<854;y++)for(unsigned x=0;x<480;x++) {
   unsigned c=wallpaper_preset(kind,x,y),v=((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31);
   putchar(v&255);putchar(v>>8);
  }return 0;
 }
 if(argc==3&&!strcmp(argv[1],"wallpaper")) {
  FILE *f=fopen(argv[2],"rb");if(!f)return 2;
  uint8_t *p=malloc(WALLPAPER_HEADER+WALLPAPER_BYTES);if(!p){fclose(f);return 2;}
  size_t n=fread(p,1,WALLPAPER_HEADER+WALLPAPER_BYTES,f);bool extra=fgetc(f)!=EOF;fclose(f);
  bool ok=!extra&&wallpaper_header(p,n>=32?32:0)&&n==WALLPAPER_HEADER+WALLPAPER_BYTES&&scene_crc32(p+32,WALLPAPER_BYTES)==scene_u32(p+16);free(p);return ok?0:1;
 }
 if(argc!=2)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 2;uint8_t p[APPEARANCE_BYTES];size_t n=fread(p,1,sizeof(p),f);bool extra=fgetc(f)!=EOF;fclose(f);
 screen_appearance a;if(extra||!appearance_decode(&a,p,n)||!appearance_encode(&a,p,sizeof(p)))return 1;
 return fwrite(p,1,sizeof(p),stdout)==sizeof(p)?0:2;
}
