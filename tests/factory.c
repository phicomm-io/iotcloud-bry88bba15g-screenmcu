#include "factory.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char wire[8192];
static size_t used;
static uint32_t nonce(void *context) {
  (void)context;
  static uint32_t next = 0xabcd0000u;
  return ++next;
}
static bool send(void *p, const char *s, size_t n) {
  (void)p;
  assert(used + n < sizeof(wire));
  memcpy(wire + used, s, n);
  used += n;
  wire[used] = 0;
  return true;
}
static void input(factory_owner *f, const char *s) {
  factory_line(f, s, strlen(s));
}
static cJSON *body(void) {
  char *p = strchr(wire, ' ');
  assert(p);
  p = strchr(p + 1, ' ');
  assert(p);
  p = strchr(p + 1, ' ');
  assert(p);
  cJSON *doc = cJSON_Parse(p + 1);
  assert(doc);
  return doc;
}
static void service(factory_owner *f, const char *name, const char *json) {
  char line[2048];
  snprintf(line, sizeof(line),
           "+sub_recv:SENSOR/AABBCCDDEEFF/mqtt/service/%s,%lu,%s", name,
           (unsigned long)strlen(json), json);
  used = 0;
  input(f, line);
}
int main(void) {
  static screen_runtime r;
  screen_runtime_init(&r);
  r.send = send;
  screen_power p = {0};
  screen_maintenance m = {0};
  factory_owner f = {.runtime = &r, .power = &p, .maintenance = &m};
  factory_init(&f, NULL);
  assert(p.brightness == 10 && f.settings[16] == 10);
  uint8_t retained[64];
  memcpy(retained, f.settings, sizeof(retained));
  retained[16] = 60;
  factory_init(&f, retained);
  assert(p.brightness == 10 && f.settings[16] == 10 && retained[16] == 60);
  retained[16] = 0;
  factory_init(&f, retained);
  assert(p.brightness == 0 && f.settings[16] == 0);
  factory_init(&f, NULL);
  input(&f, "+mac:AABBCCDDEEFF");
  input(&f, "+gateway_mac:001122334455");
  input(&f, "+version:stock-ESP");
  assert(f.have_mac);
  screen_sample_set(&r.model, MET_TEMP, 23.6f, true, 1000);
  screen_sample_set(&r.model, MET_HUM, 47.9f, true, 1000);
  r.now = 1000;
  used = 0;
  factory_snapshot(&f, "snapshot-token", true);
  cJSON *doc = body(), *data = cJSON_GetObjectItem(doc, "data"),
        *status = cJSON_GetObjectItem(data, "status"),
        *physics = cJSON_GetObjectItem(data, "physics");
  assert(cJSON_GetObjectItem(status, "temp")->valuedouble > 235.9 &&
         cJSON_GetObjectItem(status, "temp")->valuedouble < 236.1);
  assert(cJSON_GetObjectItem(status, "humidity")->valueint == 47);
  assert(cJSON_GetObjectItem(status, "hcho_status")->valueint == 0x59);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(status, "charging")));
  assert(!strcmp(cJSON_GetObjectItem(physics, "esp32_version")->valuestring,
                 "stock-ESP"));
  cJSON_Delete(doc);
  r.model.charging = true;
  used = 0;
  factory_snapshot(&f, "charging-token", true);
  doc = body();
  assert(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(
      cJSON_GetObjectItem(doc, "data"), "status"), "charging")));
  cJSON_Delete(doc);
  service(&f, "set",
          "{\"client_token\":\"save\",\"data\":{\"display\":{\"luminance\":70,"
          "\"data_type\":1,\"power_screen_time\":0,\"battery_screen_time\":2},"
          "\"no_disturb\":{\"state\":true,\"start_tm\":\"22:00\",\"end_tm\":"
          "\"08:00\"}}}");
  assert(p.brightness == 70 && p.power_timeout == 0 && p.battery_timeout == 2);
  doc = body();
  assert(cJSON_GetObjectItem(doc, "code")->valueint == 0);
  cJSON_Delete(doc);
  uint8_t saved[64];
  memcpy(saved, f.settings, 64);
  service(
      &f, "set",
      "{\"client_token\":\"bad\",\"data\":{\"display\":{\"luminance\":101,"
      "\"data_type\":1,\"power_screen_time\":0,\"battery_screen_time\":2}}}");
  assert(!memcmp(saved, f.settings, 64));
  doc = body();
  assert(cJSON_GetObjectItem(doc, "code")->valueint == 1);
  cJSON_Delete(doc);
  sensor_calibration before = r.calibration;
  service(&f, "hcho_install", "{\"client_token\":\"noop\",\"data\":{}}");
  doc = body();
  assert(cJSON_GetObjectItem(doc, "code")->valueint == 0);
  cJSON_Delete(doc);
  assert(!memcmp(&before, &r.calibration, sizeof(before)));
  input(&f, "+sntp:1793880000");
  used = 0;
  factory_snapshot(&f, "clock-before-tick", true);
  doc = body();
  assert(cJSON_GetObjectItem(doc, "timestamp")->valuedouble == 1793880000000.0);
  cJSON_Delete(doc);
  factory_tick(&f, 1000);
  assert(r.model.clock_valid);
  assert(r.clock_epoch == 1793880000 && r.clock_revision == 1);
  const char *bad_times[] = {"+sntp:-1", "+sntp: 1793880000", "+sntp:0",
      "+sntp:4294967296", "+sntp:99999999999999999999", "+sntp:1793880000x"};
  for (unsigned i = 0; i < sizeof(bad_times) / sizeof(*bad_times); i++)
    input(&f, bad_times[i]);
  assert(r.clock_epoch == 1793880000 && r.clock_revision == 1);
  screen_runtime_tick(&r, 2000);
  factory_tick(&f, 2000);
  assert(r.clock_epoch == 1793880001 && f.epoch == r.clock_epoch);
  used = 0;
  factory_provision(&f);
  assert(!strcmp(wire, "ble_cfg\r\n"));
  /* Stock ESP does not ACK: retain fallback without periodic offers. A real
   * link reset may start one new negotiation with a fresh platform nonce. */
  f.phase = 8;
  f.configured = true;
  f.nonce = nonce;
  used = 0;
  factory_tick(&f, 15000);
  assert(strstr(wire, "screen_caps 1 abcd0001 ") && r.offered && !r.caps.ready);
  used = 0;
  factory_tick(&f, 30000);
  assert(!strstr(wire, "screen_caps ") && r.caps.session == 0xabcd0001u);
  const char reset[] = "+screen_link_reset:1\r\n";
  screen_runtime_feed(&r, reset, sizeof(reset) - 1, 30001);
  used = 0;
  factory_tick(&f, 45000);
  assert(strstr(wire, "screen_caps 1 abcd0002 ") && r.offered);
  puts("factory UART snapshot scales, presence, token, atomic settings, "
       "invalid bounds, HCHO no-op, clock, provision and one offer per link passed");
}
