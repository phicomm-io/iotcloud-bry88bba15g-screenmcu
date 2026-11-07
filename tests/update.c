#include "update.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char nor[0x68000];
static bool write_fail,read_fail,erase_fail;
static bool erase(void *ctx,uint32_t address,size_t n){(void)ctx;assert(address==SCREEN_OTA_STAGE&&n<=sizeof(nor));memset(nor,0xff,n);return !erase_fail;}
static bool write(void *ctx,uint32_t address,const void *p,size_t n){(void)ctx;assert(address>=SCREEN_OTA_STAGE&&address+n<=SCREEN_OTA_STAGE+sizeof(nor));memcpy(nor+address-SCREEN_OTA_STAGE,p,n);return !write_fail;}
static bool read(void *ctx,uint32_t address,void *p,size_t n){(void)ctx;assert(address>=SCREEN_OTA_STAGE&&address+n<=SCREEN_OTA_STAGE+sizeof(nor));memcpy(p,nor+address-SCREEN_OTA_STAGE,n);return !read_fail;}
static screen_update u={.erase=erase,.write=write,.read=read};
static void checksum(const void *p,size_t n,uint8_t digest[16]){screen_md5 h;screen_md5_init(&h);screen_md5_update(&h,p,n);screen_md5_final(&h,digest);}
int main(void){
 const char *inputs[]={"","a","abc","message digest","abcdefghijklmnopqrstuvwxyz","ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789","12345678901234567890123456789012345678901234567890123456789012345678901234567890"};
 const char *expected[]={"d41d8cd98f00b204e9800998ecf8427e","0cc175b9c0f1b6a831c399e269772661","900150983cd24fb0d6963f7d28e17f72","f96b697d7cb7938d525a2f31aaf161d0","c3fcd3d76192e4007dfb496cca67e13b","d174ab98d277d9f5a5611c2c9f419d9f","57edf4a22be3c955ac49da2e2107b67a"};
 uint8_t md5[16];char hex[33];
 for(unsigned i=0;i<7;i++){screen_md5 h;screen_md5_init(&h);for(size_t j=0;j<strlen(inputs[i]);j++)screen_md5_update(&h,inputs[i]+j,1);screen_md5_final(&h,md5);for(unsigned j=0;j<16;j++)sprintf(hex+j*2,"%02x",md5[j]);assert(!strcmp(hex,expected[i]));}
 unsigned char image[2048]={0};memcpy(image,"\x78\xb2\x00\x20\x75\x43\x01\x00",8);checksum(image,sizeof(image),md5);
 assert(!screen_update_begin(&u,SCREEN_APP_LIMIT+16,md5));assert(screen_update_begin(&u,sizeof(image),md5));
 assert(!screen_update_chunk(&u,1024,image,1024));assert(screen_update_chunk(&u,0,image,1024));assert(!screen_update_finish(&u));assert(screen_update_chunk(&u,1024,image+1024,1024));assert(screen_update_finish(&u)&&u.transfer.verified);
 assert(screen_update_begin(&u,sizeof(image),md5));assert(screen_update_chunk(&u,0,image,1024));assert(screen_update_chunk(&u,1024,image+1024,1024));nor[1500]^=1;assert(!screen_update_finish(&u)&&u.failed);
 erase_fail=true;assert(!screen_update_begin(&u,sizeof(image),md5)&&u.failed);erase_fail=false;
 assert(screen_update_begin(&u,sizeof(image),md5));write_fail=true;assert(!screen_update_chunk(&u,0,image,1024)&&u.failed);write_fail=false;
 /* Partial chunks can split the initial vector words; no alignment assumption. */
 assert(screen_update_begin(&u,sizeof(image),md5));for(unsigned i=0;i<sizeof(image);i++)assert(screen_update_chunk(&u,i,image+i,1));read_fail=true;assert(!screen_update_finish(&u));read_fail=false;
 image[4]&=0xfe;checksum(image,sizeof(image),md5);assert(screen_update_begin(&u,sizeof(image),md5));assert(screen_update_chunk(&u,0,image,1024));assert(screen_update_chunk(&u,1024,image+1024,1024));assert(!screen_update_finish(&u));
 puts("update: MD5 vectors, NOR readback, vector validation, storage errors and split chunks passed");
}
