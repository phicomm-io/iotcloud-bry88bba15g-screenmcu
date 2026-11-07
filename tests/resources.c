#include "resources.h"
#include "scene.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t bank[SCREEN_RESOURCE_LIMIT], cache[SCREEN_RESOURCE_LIMIT];
static unsigned reads, services, clock_ms, cost, largest_read;
static bool broken;
static bool read_bank(void *c, uint32_t at, void *out, size_t n) {
  (void)c; reads++; clock_ms += cost;if(n>largest_read)largest_read=n;
  if (broken || at < SCREEN_RESOURCE_BASE || n > sizeof(bank) ||
      at - SCREEN_RESOURCE_BASE > sizeof(bank) - n) return false;
  memcpy(out, bank + at - SCREEN_RESOURCE_BASE, n); return true;
}
static uint32_t now(void *c) { (void)c; return clock_ms; }
static void service(void *c) { (void)c; services++; }
static screen_resources setup(void) {
  memset(bank, 0, sizeof(bank)); memset(cache, 0, sizeof(cache));
  reads = services = clock_ms = cost = largest_read = 0; broken = false;
  memcpy(bank, "IOTFNT01", 8); scene_put32(bank + 8, 57);
  bank[16] = 1; bank[18] = 24; bank[20] = 36; bank[21] = 2;
  scene_put32(bank + 32, 0x4e2d); scene_put32(bank + 36, 56);
  bank[40] = 1; bank[42] = 32; bank[44] = bank[46] = 2; bank[56] = 0x90;
  scene_put32(bank + 12, scene_crc32(bank + 32, 25));
  return (screen_resources){.read=read_bank,.now=now,.service=service,
                             .cache=cache,.capacity=sizeof(cache)};
}
int main(int argc,char **argv) {
  screen_resources r = setup(); resource_glyph g;
  assert(resources_load(&r) && r.state == RESOURCE_READY && services);
  unsigned before = reads;
  assert(resources_glyph(&r, 0x4e2d, &g) && g.advance == 32 && g.pixels[0] == 0x90);
  assert(!resources_glyph(&r, 0x4e00, &g));
  broken = true;
  for (unsigned i=0;i<1000;i++) assert(resources_glyph(&r,0x4e2d,&g));
  assert(reads == before); /* disconnect after loading: RAM render survives */
  r=setup(); broken=true; assert(!resources_load(&r) && r.state==RESOURCE_IO);
  broken=false; before=reads;
  for(unsigned i=0;i<1000;i++)assert(!resources_load(&r));
  assert(reads==before); /* failures are latched, not retried by redraw */
  r=setup(); bank[56]^=1; assert(!resources_load(&r) && r.state==RESOURCE_INVALID);
  r=setup(); scene_put32(bank+36,0xfffffff0); scene_put32(bank+12,scene_crc32(bank+32,25));
  assert(!resources_load(&r) && r.state==RESOURCE_INVALID);
  r=setup(); bank[18]=25; assert(!resources_load(&r) && r.state==RESOURCE_INVALID);
  r=setup(); memset(bank,255,sizeof(bank)); assert(!resources_load(&r) && r.state==RESOURCE_ABSENT);
  r=setup(); cost=2600; assert(!resources_load(&r) && r.state==RESOURCE_TIMEOUT);
  assert(clock_ms>=5200 && reads<=4);
  if(argc>1){
    r=setup();FILE *f=fopen(argv[1],"rb");assert(f);
    size_t n=fread(bank,1,sizeof(bank),f);assert(fgetc(f)==EOF);fclose(f);
    assert(resources_load(&r)&&r.bytes==n&&r.count>30);
    assert(largest_read<=512);
    assert(resources_glyph(&r,'A',&g)&&g.width&&g.height);
    broken=true;before=reads;
    assert(resources_glyph(&r,'A',&g)&&reads==before);
    /* Full managed-bank CRC and payload CRC must agree independently. Work is
     * bounded by both bytes and clock time, including uint32_t wraparound. */
    for(unsigned paced=0;paced<2;paced++) {
      r=(screen_resources){.read=read_bank,.now=now,.service=service,.cache=cache,.capacity=sizeof(cache)};
      broken=false;clock_ms=UINT32_MAX-20;cost=paced?4:0;reads=0;
      resources_start(&r);assert(r.state==RESOURCE_LOADING);r.expected=scene_crc32(bank,n);
      unsigned calls=0;
      while(r.state==RESOURCE_LOADING){unsigned at=reads;uint32_t received=r.received;resources_poll(&r);assert(reads-at<=(paced?2:16));assert(r.received-received<=8192);calls++;}
      assert(r.state==RESOURCE_READY&&calls>1);
      assert(r.bytes==n&&r.crc==scene_u32(bank+12));
    }
    r=(screen_resources){.read=read_bank,.now=now,.cache=cache,.capacity=sizeof(cache)};cost=0;resources_start(&r);r.expected=scene_crc32(bank,n)^1;
    while(r.state==RESOURCE_LOADING){resources_poll(&r);}assert(r.state==RESOURCE_INVALID);
    r=(screen_resources){.read=read_bank,.now=now,.cache=cache,.capacity=sizeof(cache)};resources_start(&r);bank[n-1]^=1;
    while(r.state==RESOURCE_LOADING){resources_poll(&r);}assert(r.state==RESOURCE_INVALID);bank[n-1]^=1;
    printf("Validated optional font bank: %u glyphs, %lu bytes, bounded incremental CRC and RAM-only rendering\n",bank[16]|bank[17]<<8,(unsigned long)n);
  }
  return 0;
}
