#include "maintenance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t flash[0x68000];
static char output[65536];
static unsigned used;
static unsigned business;
static uint32_t ticks;
static bool slow_flash;
static bool erase_fail;
static bool inhibited;
static unsigned entries, exits;
static void activity(void *context, bool active) {
  (void)context;
  inhibited = active;
  if (active) entries++; else exits++;
}
static uint32_t clock_ms(void *context) { (void)context; return ticks; }
static bool send(void *c, const char *p, size_t n) {
  (void)c;
  assert(used + n < sizeof(output));
  memcpy(output + used, p, n);
  used += n;
  output[used] = 0;
  return true;
}
static bool read_nor(void *c, uint32_t a, void *p, size_t n) {
  (void)c;
  if (a < SCREEN_OTA_STAGE || a - SCREEN_OTA_STAGE + n > sizeof(flash))
    return false;
  memcpy(p, flash + a - SCREEN_OTA_STAGE, n);
  if (slow_flash) ticks += 20;
  return true;
}
static bool write_nor(void *c, uint32_t a, const void *p, size_t n) {
  (void)c;
  assert(inhibited);
  if (a < SCREEN_OTA_STAGE || a - SCREEN_OTA_STAGE + n > sizeof(flash))
    return false;
  memcpy(flash + a - SCREEN_OTA_STAGE, p, n);
  return true;
}
static bool erase(void *c, uint32_t a, size_t n) {
  (void)c;
  assert(inhibited);
  assert(a == SCREEN_OTA_STAGE && n <= sizeof(flash));
  if (erase_fail) return false;
  memset(flash, 255, n);
  if (slow_flash) ticks += 45000;
  return true;
}
static void line(void *c, const char *p, size_t n) {
  (void)c;
  (void)p;
  (void)n;
  business++;
}
static void feed(screen_maintenance *m, const void *v, size_t n) {
  const uint8_t *p = v;
  while (n--)
    maintenance_feed(m, *p++, 100);
}
static void text(screen_maintenance *m, const char *p) {
  feed(m, p, strlen(p));
}
int main(void) {
  screen_maintenance m = {0};
  m.send = send;
  m.line = line;
  m.activity = activity;
  m.update.read = read_nor;
  m.update.write = write_nor;
  m.update.erase = erase;
  maintenance_init(&m);
  static uint8_t app[SCREEN_APP_LIMIT];
  for (unsigned i = 0; i < sizeof(app); i++)
    app[i] = (i * 137) ^ 3;
  app[0] = 0;
  app[1] = 0;
  app[2] = 1;
  app[3] = 0x20;
  app[4] = 1;
  app[5] = 0x44;
  app[6] = 1;
  app[7] = 0;
  /* Text-like binary and NUL never reach the stock/extension parser. */
  static const char embedded[] =
      "\0\r\nscreen_caps 1 bad\r\n+download_bin:END\r\n";
  memcpy(app + 32, embedded, sizeof(embedded));
  screen_md5 md5;
  uint8_t digest[16];
  screen_md5_init(&md5);
  screen_md5_update(&md5, app, sizeof(app));
  screen_md5_final(&md5, digest);
  char summary[100] = "+bin_summary:test,";
  for (unsigned i = 0; i < 16; i++)
    sprintf(summary + 18 + 2 * i, "%02x", digest[i]);
  /* Prefix is 18 bytes including comma. */
  strcat(summary, "\r\n");
  char ending[80] = "+end_ota:";
  for (unsigned i = 0; i < 16; i++)
    sprintf(ending + 9 + 2 * i, "%02x", digest[i]);
  strcat(ending, "\r\n");
  assert(maintenance_query(&m, 0));
  text(&m, summary);
  assert(m.state == 2);
  text(&m, "+start_ota:OK\r\n");
  assert(m.state == 3);
  for (unsigned at = 0; at < sizeof(app); at += 1024) {
    char header[80];
    snprintf(header, sizeof(header), "+download_bin:%u,1024\r\n", at);
    text(&m, header);
    assert(m.receiving);
    text(&m, "+bin:");
    feed(&m, app + at, 1024);
    text(&m, "\r\n");
    assert(m.state == 3);
  }
  text(&m, "+download_bin:END\r\n");
  assert(m.staged && m.update.transfer.verified);
  assert(!memcmp(app, flash, sizeof(app)));
  text(&m, ending);
  assert(!m.state && !inhibited && entries == 1 && exits == 1);
  assert(!business);
  /* Repeat a full-size image without resetting either UART owner. */
  used = 0;
  m.clock = clock_ms;
  slow_flash = true;
  ticks = 100;
  assert(maintenance_query(&m, 500));
  text(&m, summary);
  maintenance_tick(&m, ticks);
  assert(m.state == 2); /* Erase took longer than the old 30 s deadline. */
  text(&m, "+start_ota:OK\r\n");
  for (unsigned at = 0; at < sizeof(app); at += 1024) {
    unsigned n = sizeof(app) - at < 1024 ? sizeof(app) - at : 1024;
    char header[80];
    snprintf(header, sizeof(header), "+download_bin:%u,%u\r\n", at, n);
    text(&m, header);
    text(&m, "+bin:");
    feed(&m, app + at, n);
    /* Factory +bin packets have no CR/LF terminator. */
  }
  text(&m, "+download_bin:END\r\n");
  maintenance_tick(&m, ticks);
  assert(m.state == 5); /* Whole NOR re-read also exceeded 30 s. */
  maintenance_tick(&m, ticks + 29999);
  assert(m.staged && m.update.transfer.verified);
  assert(!memcmp(app, flash, sizeof(app)));
  text(&m, ending);
  assert(!m.state && !business && !inhibited);
  slow_flash = false;
  /* Completion metadata is checked even after a successful full NOR hash. */
  m.state = 5;
  text(&m, "+end_ota:00000000000000000000000000000000\r\n");
  assert(!m.state && !m.staged && !m.update.transfer.verified);
  /* Full re-read digest detects a NOR write that silently changed one bit. */
  used = 0;
  assert(maintenance_query(&m, 600));
  text(&m, summary);
  text(&m, "+start_ota:OK\r\n");
  for (unsigned at = 0; at < sizeof(app); at += 1024) {
    unsigned n = sizeof(app) - at < 1024 ? sizeof(app) - at : 1024;
    char header[80];
    snprintf(header, sizeof(header), "+download_bin:%u,%u\r\n", at, n);
    text(&m, header);
    text(&m, "+bin:");
    feed(&m, app + at, n);
    text(&m, "\r\n");
  }
  flash[1025] ^= 1;
  text(&m, "+download_bin:END\r\n");
  assert(!m.staged && !m.state);
  used = 0;
  assert(maintenance_query(&m, 700));
  text(&m, summary);
  text(&m, "+start_ota:OK\r\n");
  text(&m, "+download_bin:1024,1024\r\n");
  assert(!m.state && !m.staged);
  assert(maintenance_query(&m, UINT32_MAX - 100));
  maintenance_tick(&m, 30000);
  assert(!m.state);
  assert(strstr(output,"cancel_ota\r\n"));
  assert(!inhibited);
  used = 0; erase_fail = true;
  assert(maintenance_query(&m, 800)); text(&m, summary);
  assert(!m.state && !m.staged && !m.receiving && !m.update.transfer.verified);
  assert(strstr(output,"cancel_ota\r\n") && !strstr(output,"start_ota"));
  assert(!inhibited);
  erase_fail = false;used=0;
  assert(maintenance_query(&m, 900));text(&m, summary);
  assert(m.state==2 && strstr(output,"start_ota 1 20"));
  maintenance_init(&m);
  assert(!m.state && !inhibited);
  puts("factory raw UART OTA replay, second transfer, bad offset and timeout "
       "passed; staging never activates the image");
}
