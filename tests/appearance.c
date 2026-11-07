#include "appearance.h"
#include "scene.h"
#include <assert.h>
#include <string.h>
int main(void) {
 screen_appearance a,b;appearance_default(&a);uint8_t wire[APPEARANCE_BYTES];
 assert(appearance_color(&a,AP_CANVAS)==0x101611);
 assert(appearance_rail_height(&a)==12);
 const unsigned heights[]={0,6,12,20};
 for(unsigned i=0;i<4;i++){a.page_rail=heights[i];assert(appearance_encode(&a,wire,sizeof(wire)));assert(wire[57]==heights[i]);assert(appearance_decode(&b,wire,sizeof(wire))&&b.page_rail==heights[i]);}
 a.page_rail=7;assert(!appearance_encode(&a,wire,sizeof(wire)));a.page_rail=0;
 assert(appearance_encode(&a,wire,sizeof(wire)));wire[57]=7;assert(!appearance_decode(&b,wire,sizeof(wire)));
 for(unsigned t=0;t<6;t++){a.theme=t;assert(appearance_encode(&a,wire,sizeof(wire)));assert(appearance_decode(&b,wire,sizeof(wire)));assert(b.theme==t);}
 a.count=1;a.items[0]=(appearance_item){.id=106,.mask=4,.opacity=255,.colors={0,0,0x123456}};
 a.mask=1u<<AP_UNIT;a.colors[AP_UNIT]=0xabcdef;
 const appearance_item *it=appearance_find(&a,106);assert(it);
 assert(appearance_widget_color(&a,it,2)==0x123456);
 assert(appearance_widget_color(&a,it,3)==0xabcdef);
 assert(appearance_opacity(&a,it,SC_CLOCK,2,false)==0);
 assert(appearance_opacity(&a,it,SC_VALUE,2,false)==100);
 assert(appearance_opacity(&a,it,SC_WEATHER,6,true)==80);
 a.items[0].opacity=0;assert(appearance_opacity(&a,it,SC_VALUE,2,true)==0);
 assert(appearance_encode(&a,wire,sizeof(wire)));assert(appearance_decode(&b,wire,sizeof(wire)));
 a.language=1;assert(appearance_encode(&a,wire,sizeof(wire))&&wire[56]==1);
 assert(appearance_decode(&b,wire,sizeof(wire))&&b.language==1);
 wire[56]=2;assert(!appearance_decode(&b,wire,sizeof(wire)));wire[56]=1;
 screen_appearance before=b;wire[10]=6;assert(!appearance_decode(&b,wire,sizeof(wire)));assert(!memcmp(&b,&before,sizeof(b)));
 wire[10]=0;wire[14]=33;assert(!appearance_decode(&b,wire,sizeof(wire)));
 wire[14]=1;wire[64+3]=101;assert(!appearance_decode(&b,wire,sizeof(wire)));
 wire[64+3]=255;wire[64+19]=1;assert(!appearance_decode(&b,wire,sizeof(wire)));
 return 0;
}
