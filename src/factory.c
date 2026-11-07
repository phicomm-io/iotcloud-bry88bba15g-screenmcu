/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "factory.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool send(factory_owner *f, const char *s) {
  return f->runtime->send &&
         f->runtime->send(f->runtime->context, s, strlen(s));
}
static unsigned u16(const uint8_t *s) { return s[0] | s[1] << 8; }
static void put16(uint8_t *s, unsigned n) {
  s[0] = n;
  s[1] = n >> 8;
}
static bool hexmac(const char *s) {
  if (strlen(s) != 12)
    return false;
  bool any = false;
  for (unsigned i = 0; i < 12; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
          (c >= 'a' && c <= 'f')))
      return false;
    any |= c != '0';
  }
  return any;
}
static void setting_defaults(uint8_t s[64]) {
  memset(s, 0, 64);
  s[0] = 0x5a;
  s[2] = s[3] = 1;
  put16(s + 4, (uint16_t)-25);
  put16(s + 6, 3);
  put16(s + 8, (uint16_t)-4);
  put16(s + 12, 100);
  put16(s + 14, 1);
  s[16] = 60;
  s[18] = 2;
  s[19] = 1;
  put16(s + 20, 2200);
  put16(s + 22, 800);
  put16(s + 24, 30000);
  put16(s + 26, 35536);
  put16(s + 28, 30000);
  put16(s + 30, 35536);
  put16(s + 32, 1000);
  put16(s + 34, 400);
  put16(s + 36, 75);
  put16(s + 40, 1000);
  put16(s + 44, 8);
}
void factory_init(factory_owner *f, const uint8_t settings[64]) {
  setting_defaults(f->settings);
  if (settings && settings[0] == 0x5a)
    memcpy(f->settings, settings, 64);
  if (f->settings[16] > 100)
    f->settings[16] = 60;
  /* The retained factory record can request 60% before ESP restores its saved
   * display group. Start at <=10% until a valid display command arrives;
   * never write or otherwise change the retained calibration/settings bytes. */
  if (f->settings[16] > 10)
    f->settings[16] = 10;
  if (f->settings[18] > 2)
    f->settings[18] = 2;
  if (f->settings[19] > 2)
    f->settings[19] = 1;
  sensor_sht_config c = {
      (int16_t)u16(f->settings + 4), (int16_t)u16(f->settings + 6),
      (int16_t)u16(f->settings + 8), (int16_t)u16(f->settings + 10)};
  if (!c.temperature || abs(c.temperature) >= 201)
    c.temperature = -25;
  if (abs(c.secondary) >= 101)
    c.secondary = 3;
  if (abs(c.hcho_heat) >= 101)
    c.hcho_heat = -4;
  if (abs(c.humidity) >= 101)
    c.humidity = 0;
  screen_runtime_sensor_config(f->runtime, &c);
  f->power->brightness = f->settings[16];
  f->power->power_timeout = f->settings[18];
  f->power->battery_timeout = f->settings[19];
  f->phase = 0;
  f->next = 0;
  f->snapshot_at = 0;
  f->have_mac = false;
  f->connected = false;
  f->configured = false;
}
static void publish(factory_owner *f, const char *suffix, cJSON *doc) {
  if (!f->have_mac || !doc)
    return;
  char *body = cJSON_PrintUnformatted(doc);
  if (!body)
    return;
  char header[256];
  int n = snprintf(header, sizeof(header), "pub SENSOR/%s/%s %u ", f->mac,
                   suffix, (unsigned)strlen(body));
  /* One owner, no task may interleave this header and its bounded payload. */
  if (n > 0 && (unsigned)n < sizeof(header)) {
    send(f, header);
    send(f, body);
    send(f, "\r\n");
  }
  free(body);
}
static cJSON *object(cJSON *parent, const char *key) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddItemToObject(parent, key, o);
  return o;
}
static cJSON *envelope(factory_owner *f, const char *token, bool response) {
  cJSON *o = cJSON_CreateObject();
  if (!o)
    return NULL;
  char value[48];
  if (!token) {
    snprintf(value, sizeof(value), "iotcloud-%lu", (unsigned long)++f->token);
    token = value;
  }
  cJSON_AddStringToObject(o, "client_token", token);
  if (response)
    cJSON_AddNumberToObject(o, "code", 0);
  cJSON_AddNumberToObject(o, "timestamp", (double)f->runtime->clock_epoch * 1000);
  return o;
}
void factory_snapshot(factory_owner *f, const char *token, bool response) {
  if (!f->have_mac)
    return;
  screen_model *m = &f->runtime->model;
  cJSON *o = envelope(f, token, response);
  if (!o)
    return;
  cJSON *d = object(o, "data"), *p = object(d, "physics"),
        *s = object(d, "status"), *cfg = object(d, "setting");
  cJSON_AddNumberToObject(p, "type", 4);
  cJSON_AddStringToObject(p, "terminal_mac", f->gateway);
  cJSON_AddStringToObject(p, "machine_name", "BRY88BBA15G");
  cJSON_AddStringToObject(p, "esp32_version", f->esp_version);
  cJSON_AddStringToObject(p, "mac", f->mac);
  cJSON_AddStringToObject(p, "mcu_version",
                          f->version ? f->version : "iotcloud-dev-dirty");
  cJSON_AddStringToObject(p, "gallery_version", "iotcloud-built-in");
  static const char *names[] = {"temp", "humidity", "co2",
                                "pm25", "tvoc",     "hcho"};
  static const char *states[] = {"temp_status", "humidity_status",
                                 "co2_status",  "pm25_status",
                                 "tvoc_status", "hcho_status"};
  for (unsigned i = 0; i < 6; i++) {
    bool valid = screen_sample_valid(m, i, f->runtime->now);
    double value = valid ? m->samples[i].value : 0;
    if (i == MET_HUM)
      value = (int)value;
    if (i == MET_TEMP)
      value *= 10;
    cJSON_AddNumberToObject(s, names[i], value);
    cJSON_AddNumberToObject(s, states[i],
                            valid ? 0x51
                            : i == MET_HCHO && !(m->present & (1u << i))
                                ? 0x59
                                : 0x52);
  }
  cJSON_AddNumberToObject(s, "tvoc_level", m->tvoc_level);
  cJSON_AddNumberToObject(s, "battery_status", m->battery_valid ? 0x51 : 0x52);
  cJSON_AddNumberToObject(s, "battery", m->battery_percent);
  cJSON_AddBoolToObject(s, "power_state", !f->power->asleep);
  cJSON *display = object(cfg, "display");
  cJSON_AddNumberToObject(display, "luminance", f->settings[16]);
  cJSON_AddNumberToObject(display, "data_type", f->settings[17]);
  cJSON_AddNumberToObject(display, "power_screen_time", f->settings[18]);
  cJSON_AddNumberToObject(display, "battery_screen_time", f->settings[19]);
  cJSON *dnd = object(cfg, "no_disturb");
  char time[6];
  unsigned n = u16(f->settings + 20);
  snprintf(time, sizeof(time), "%02u:%02u", n / 100 % 24, n % 100 % 60);
  cJSON_AddStringToObject(dnd, "start_tm", time);
  n = u16(f->settings + 22);
  snprintf(time, sizeof(time), "%02u:%02u", n / 100 % 24, n % 100 % 60);
  cJSON_AddStringToObject(dnd, "end_tm", time);
  cJSON_AddBoolToObject(dnd, "state", u16(f->settings + 14) != 0);
  cJSON_AddBoolToObject(cfg, "ipbox_push", f->settings[2] != 0);
  cJSON *warning = object(cfg, "warning");
  cJSON_AddBoolToObject(warning, "state", f->settings[3] != 0);
  const char *keys[] = {"temp", "hum", "co2", "pm25", "tvoc", "hcho"};
  const unsigned offsets[] = {24, 28, 32, 36, 40, 44};
  for (unsigned i = 0; i < 6; i++) {
    cJSON *v = object(warning, keys[i]);
    cJSON_AddNumberToObject(v, "upper",
                            i < 2 ? (int)(int16_t)u16(f->settings + offsets[i])
                                  : (int)u16(f->settings + offsets[i]));
    if (i < 2)
      cJSON_AddNumberToObject(v, "lower",
                              (int16_t)u16(f->settings + offsets[i] + 2));
  }
  publish(f,
          response ? "mqtt/service_response/get_snapshot"
                   : "mqtt/event/upload_snapshot",
          o);
  cJSON_Delete(o);
}
static cJSON *get(cJSON *o, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(o, key);
}
static bool number(cJSON *o, const char *key, unsigned max, unsigned *out) {
  cJSON *v = get(o, key);
  if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > max ||
      floor(v->valuedouble) != v->valuedouble)
    return false;
  *out = (unsigned)v->valuedouble;
  return true;
}
static bool timevalue(cJSON *o, const char *key, unsigned *out) {
  cJSON *v = get(o, key);
  unsigned h, m;
  char x;
  if (!cJSON_IsString(v) || strlen(v->valuestring) != 5 ||
      sscanf(v->valuestring, "%2u:%2u%c", &h, &m, &x) != 2 || h > 23 || m > 59)
    return false;
  *out = h * 100 + m;
  return true;
}
static bool settings(factory_owner *f, cJSON *d) {
  uint8_t candidate[64];
  memcpy(candidate, f->settings, 64);
  cJSON *v = get(d, "display");
  unsigned a, b, c, e;
  if (v) {
    if (!number(v, "luminance", 100, &a) || !number(v, "data_type", 1, &b) ||
        !number(v, "power_screen_time", 2, &c) ||
        !number(v, "battery_screen_time", 2, &e))
      return false;
    candidate[16] = a;
    candidate[17] = b;
    candidate[18] = c;
    candidate[19] = e;
  }
  v = get(d, "no_disturb");
  if (v) {
    if (!cJSON_IsBool(get(v, "state")) || !timevalue(v, "start_tm", &a) ||
        !timevalue(v, "end_tm", &b))
      return false;
    put16(candidate + 14, cJSON_IsTrue(get(v, "state")));
    put16(candidate + 20, a);
    put16(candidate + 22, b);
  }
  v = get(d, "ipbox_push");
  if (v) {
    if (!cJSON_IsBool(v))
      return false;
    candidate[2] = cJSON_IsTrue(v);
  }
  if (!get(d, "display") && !get(d, "no_disturb") && !get(d, "ipbox_push"))
    return false;
  memcpy(f->settings, candidate, 64);
  f->power->brightness = candidate[16];
  f->power->power_timeout = candidate[18];
  f->power->battery_timeout = candidate[19];
  return true;
}
static void service(factory_owner *f, const char *topic, const char *text,
                    size_t n) {
  if (n > 4096)
    return;
  const char *end;
  cJSON *doc = cJSON_ParseWithLengthOpts(text, n + 1, &end, 1);
  if (!doc)
    return;
  cJSON *token = get(doc, "client_token"), *d = get(doc, "data");
  if (!cJSON_IsString(token) || strlen(token->valuestring) > 128 ||
      !scene_utf8(token->valuestring, 128)) {
    cJSON_Delete(doc);
    return;
  }
  const char *name = strstr(topic, "/service/");
  if (!name) {
    cJSON_Delete(doc);
    return;
  }
  name += 9;
  if (!strcmp(name, "get_snapshot")) {
    factory_snapshot(f, token->valuestring, true);
    cJSON_Delete(doc);
    return;
  }
  bool ok = false;
  if (!strcmp(name, "set"))
    ok = settings(f, d);
  else if (!strcmp(name, "calibration")) {
    unsigned co2 = 0, tvoc = 0;
    bool hc = get(d, "co2") != NULL, ht = get(d, "tvoc") != NULL;
    if ((!hc || number(d, "co2", 1, &co2)) &&
        (!ht || number(d, "tvoc", 1, &tvoc)))
      ok = screen_runtime_calibration_service(f->runtime, hc, co2, ht, tvoc);
  } else if (!strcmp(name, "hcho_install"))
    ok = calibration_hcho_install(true);
  else if (!strcmp(name, "ota")) {
    ok = !f->maintenance->state;
    if (ok)
      factory_ota(f);
  }
  cJSON *reply = envelope(f, token->valuestring, true);
  cJSON_ReplaceItemInObject(reply, "code", cJSON_CreateNumber(ok ? 0 : 1));
  object(reply, "data");
  char path[96];
  snprintf(path, sizeof(path), "mqtt/service_response/%s", name);
  publish(f, path, reply);
  cJSON_Delete(reply);
  cJSON_Delete(doc);
}
void factory_line(void *ctx, const char *s, size_t n) {
  factory_owner *f = ctx;
  if (n > 9 && n - 9 < sizeof(f->esp_version) && !memcmp(s, "+version:", 9)) {
    memcpy(f->esp_version, s + 9, n - 9);
    f->esp_version[n - 9] = 0;
    return;
  }
  if (n > 5 && !memcmp(s, "+mac:", 5) && hexmac(s + 5)) {
    memcpy(f->mac, s + 5, 13);
    f->have_mac = true;
  } else if (n > 13 && !memcmp(s, "+gateway_mac:", 13) && hexmac(s + 13))
    memcpy(f->gateway, s + 13, 13);
  else if (n > 12 && !memcmp(s, "+wifi_state:", 12))
    f->runtime->model.network = s[12] == '2';
  else if (n > 11 && !memcmp(s, "+mqtt_conn:", 11)) {
    f->connected = s[11] == '2';
  } else if (n > 6 && !memcmp(s, "+sntp:", 6)) {
    uint32_t epoch = 0;
    for (size_t i = 6; i < n; i++) {
      if (s[i] < '0' || s[i] > '9' ||
          epoch > (UINT32_MAX - (unsigned)(s[i] - '0')) / 10)
        return;
      epoch = epoch * 10 + (unsigned)(s[i] - '0');
    }
    if (n <= 16)
      screen_runtime_clock(f->runtime, epoch, f->runtime->timezone,
                           f->runtime->now, true);
  } else if (n > 10 && !memcmp(s, "+sub_recv:", 10)) {
    const char *comma = strchr(s + 10, ',');
    if (!comma || comma - s > 250)
      return;
    char topic[256];
    memcpy(topic, s + 10, comma - s - 10);
    topic[comma - s - 10] = 0;
    char *body;
    unsigned long length = strtoul(comma + 1, &body, 10);
    if (body == comma + 1 || *body != ',' ||
        length != n - (size_t)(body + 1 - s))
      return;
    char root[32];
    snprintf(root, sizeof(root), "SENSOR/%s/", f->mac);
    if (!f->have_mac || strncmp(topic, root, strlen(root)))
      return;
    service(f, topic, body + 1, length);
  }
}
void factory_provision(factory_owner *f) { send(f, "ble_cfg\r\n"); }
void factory_ota(factory_owner *f) {
  maintenance_query(f->maintenance, f->runtime->now);
}
void factory_tick(factory_owner *f, uint32_t now) {
  if (f->maintenance->state)
    return;
  f->epoch = f->runtime->clock_epoch;
  f->clock_at = f->runtime->clock_at;
  f->timezone = f->runtime->timezone;
  if ((int32_t)(now - f->next) >= 0) {
    static const char *const steps[] = {"mac?\r\n",
                                        "version?\r\n",
                                        "gateway_mac?\r\n",
                                        "sta_con\r\n",
                                        "wifi_state?\r\n",
                                        "mqtt_user_cfg 1 iotcloud iotcloud\r\n",
                                        "mqtt_conn iotcloud-local 1883 0\r\n",
                                        "mqtt_conn?\r\n"};
    if (f->phase < 8) {
      send(f, steps[f->phase++]);
      f->next = now + 1000;
    } else if (f->have_mac && !f->configured) {
      char line[192];
      snprintf(line, sizeof(line), "sub SENSOR/%s/mqtt/# 0\r\n", f->mac);
      send(f, line);
      f->configured = true;
      f->next = now + 1000;
    } else {
      send(f, "wifi_state?\r\n");
      send(f, "sntp\r\n");
      f->next = now + 60000;
    }
  }
  if (f->have_mac && f->configured &&
      (uint32_t)(now - f->snapshot_at) >= 15000) {
    factory_snapshot(f, NULL, false);
    f->snapshot_at = now;
    if (!f->runtime->caps.ready && !f->runtime->offered && f->nonce)
      screen_runtime_offer(f->runtime, f->nonce(f->runtime->context));
  }
}
