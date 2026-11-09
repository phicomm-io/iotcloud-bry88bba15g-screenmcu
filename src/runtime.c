#include "runtime.h"
#include "wallpaper.h"
#include "font_bank.h"
#include "power.h"
#include <string.h>
static bool module_reply(screen_runtime *s, const uint8_t *payload) {
  if (!s->caps.ready || !(s->caps.capabilities & SCREEN_CAP_MODULE_CONTROL) ||
      !s->send || s->tx_sequence == UINT32_MAX) return false;
  char wire[128];
  size_t n = screen_link_encode(wire, sizeof(wire), s->caps.session,
          s->tx_sequence + 1, payload, SCREEN_MODULE_STATUS_BYTES);
  if (!n || !s->send(s->context, wire, n)) return false;
  s->tx_sequence++; return true;
}
bool screen_runtime_module_report(screen_runtime *s) {
  uint8_t reply[SCREEN_MODULE_STATUS_BYTES];
  screen_module_status(&s->module, s->now, 0, 0, reply);
  return module_reply(s, reply);
}
bool screen_runtime_page_report(screen_runtime *s,uint32_t request,unsigned status){
  if(!s->caps.ready||!(s->caps.capabilities&SCREEN_CAP_PAGE_CONTROL)||
      !s->send||s->tx_sequence==UINT32_MAX)return false;
  uint8_t p[SCREEN_PAGE_STATUS_BYTES]={SCREEN_MSG_PAGE_STATUS,1,status,s->model.asleep?1:0};
  scene_put32(p+4,request);p[8]=s->model.page+1;p[10]=s->model.scene.pages;
  for(unsigned i=0;i<s->model.scene.pages;i++)if(s->model.scene.page[i].enabled)p[9]|=1u<<i;
  scene_put32(p+12,s->model.scene.revision);
  char wire[128];size_t n=screen_link_encode(wire,sizeof(wire),s->caps.session,
                                          s->tx_sequence+1,p,sizeof(p));
  if(!n||!s->send(s->context,wire,n))return false;
  ++s->tx_sequence;return true;
}
static bool page_request(screen_runtime *s,const uint8_t *p,size_t n){
  if(!screen_page_request_valid(p,n))return false;
  unsigned status=0,target=s->model.page;
  if(p[2]&&s->model.ota)status=2;
  else if(p[2]==1){
    target=p[8]-1;
    if(target>=s->model.scene.pages||!s->model.scene.page[target].enabled)status=1;
  }else if(p[2]>=2){
    for(unsigned i=0;i<s->model.scene.pages;i++){
      target=(target+s->model.scene.pages+(p[2]==2?1:-1))%s->model.scene.pages;
      if(s->model.scene.page[target].enabled)break;
    }
  }
  if(p[2]&&!status){s->model.page=target;s->model.asleep=false;s->reminder_wake=true;}
  screen_runtime_page_report(s,scene_u32(p+4),status);return true;
}
static void receive(void *context, const char *line, size_t length) {
  screen_runtime *s = context;
  if (length == sizeof("+screen_link_reset:1") - 1 &&
      !memcmp(line, "+screen_link_reset:1",
              sizeof("+screen_link_reset:1") - 1)) {
    screen_module_restore(&s->module);
    screen_caps_reset(&s->caps);
    s->last_sequence = s->tx_sequence = 0;
    s->offered = false;
    s->diag_pending = false;
    s->display.configured = false;
    return;
  }
  if (length >= 13 && !memcmp(line, "+screen_caps:", 13)) {
    if (!s->caps.ready && screen_caps_accept(&s->caps, line, length)) {
      s->last_sequence = s->tx_sequence = 0;
    }
    return;
  }
  if (length >= 11 && !memcmp(line, "screen_ext ", 11)) {
    uint8_t payload[SCREEN_LINK_CHUNK];
    size_t used = sizeof(payload);
    uint32_t session, seq;
    if (!s->caps.ready ||
        !screen_link_decode(line, length, &session, &seq, payload, &used) ||
        session != s->caps.session || seq <= s->last_sequence)
      return;
    bool ok = false;
    if(used&&payload[0]==SCREEN_MSG_PAGE_REQUEST&&(s->caps.capabilities&SCREEN_CAP_PAGE_CONTROL)){
      ok=page_request(s,payload,used);
    }else if (used && payload[0] == SCREEN_MSG_DIAG_REQUEST &&
        (s->caps.capabilities & SCREEN_CAP_DISPLAY_DIAG)) {
      ok = !s->diag_pending && screen_diag_request_valid(payload,used);
      if(ok){memcpy(s->diag_request,payload,used);s->diag_pending=true;}
    } else if (used && payload[0] == SCREEN_MSG_APPEARANCE &&
        (s->caps.capabilities & SCREEN_CAP_APPEARANCE)) {
      ok = used==1+APPEARANCE_BYTES && (!payload[57]||(s->caps.capabilities&SCREEN_CAP_LANGUAGE)) &&
           (!payload[58]||(s->caps.capabilities&SCREEN_CAP_PAGE_RAIL)) && appearance_decode(&s->model.appearance,payload+1,used-1);
      if(ok)wallpaper.wanted=s->model.appearance.wallpaper;
      uint8_t ack[7]={SC_MSG_ACK,SCREEN_MSG_APPEARANCE,ok?0:1};
      scene_put32(ack+3,s->model.appearance.revision);
      char reply[128];size_t n=screen_link_encode(reply,sizeof(reply),s->caps.session,
                               ++s->tx_sequence,ack,sizeof(ack));
      if(n&&s->send)s->send(s->context,reply,n);
    } else if (used && payload[0] == SCREEN_MSG_FONT &&
               (s->caps.capabilities & SCREEN_CAP_FONT_BANK)) {
      uint8_t status[20];
      /* NOR writers cannot overlap, even for direct extension clients. */
      if(used>2&&payload[2]==1&&wallpaper.active)return;
      ok=font_bank_receive(&font_bank,payload,used,status);
      char reply[128];size_t n=ok?screen_link_encode(reply,sizeof(reply),s->caps.session,
                              ++s->tx_sequence,status,sizeof(status)):0;
      if(n&&s->send)s->send(s->context,reply,n);
    } else if (used && payload[0] == SCREEN_MSG_WALLPAPER &&
               (s->caps.capabilities & SCREEN_CAP_WALLPAPER)) {
      if(used>2&&payload[2]==1&&font_bank.active)return;
      uint8_t status[20];ok=wallpaper_receive(&wallpaper,payload,used,status);
      char reply[128];size_t n=ok?screen_link_encode(reply,sizeof(reply),s->caps.session,
                              ++s->tx_sequence,status,sizeof(status)):0;
      if(n&&s->send)s->send(s->context,reply,n);
    } else if (used && payload[0] == SCREEN_MSG_MODULE_REQUEST &&
        (s->caps.capabilities & SCREEN_CAP_MODULE_CONTROL)) {
      uint8_t reply[SCREEN_MODULE_STATUS_BYTES];
      ok = screen_module_request(&s->module, payload, used, s->now, reply);
      if (ok) module_reply(s, reply);
    } else if (used && payload[0] == SCREEN_MSG_POLICY &&
        (s->caps.capabilities & SCREEN_CAP_DISPLAY)) {
      display_policy candidate;
      ok = display_policy_decode(&candidate, payload, used);
      if (ok) display_configure(&s->display, &candidate, s->now);
      uint8_t ack[7] = {SC_MSG_ACK, SCREEN_MSG_POLICY, ok ? 0 : 1};
      scene_put32(ack + 3, s->display.policy.revision);
      char reply[128];
      size_t n = screen_link_encode(reply, sizeof(reply), s->caps.session,
                                    ++s->tx_sequence, ack, sizeof(ack));
      if (n && s->send) s->send(s->context, reply, n);
    } else
    if (used && payload[0] >= SC_MSG_BEGIN && payload[0] <= SC_MSG_COMMIT &&
        (s->caps.capabilities & SCREEN_CAP_SCENE)) {
      bool title_ok=true;
      if(payload[0]==SC_MSG_COMMIT && !(s->caps.capabilities&SCREEN_CAP_HIDE_TITLE) &&
         s->scene_transfer.total==SCENE_BYTES)
        for(unsigned i=0;i<s->scene_transfer.bytes[9] && i<SCENE_ITEMS;i++)
          if(s->scene_transfer.bytes[304+288*i+83])title_ok=false;
      bool points_ok=true;
      if(payload[0]==SC_MSG_COMMIT && !(s->caps.capabilities&SCREEN_CAP_CHART_POINTS) && s->scene_transfer.total==SCENE_BYTES)
        for(unsigned i=0;i<s->scene_transfer.bytes[9]&&i<SCENE_ITEMS;i++)
          if(s->scene_transfer.bytes[304+288*i+84]||s->scene_transfer.bytes[304+288*i+85])points_ok=false;
      bool compact_ok=true;
      if(payload[0]==SC_MSG_COMMIT && !(s->caps.capabilities&SCREEN_CAP_COMPACT_WIDGETS) && s->scene_transfer.total==SCENE_BYTES)
        for(unsigned i=0;i<s->scene_transfer.bytes[9]&&i<SCENE_ITEMS;i++) {
          const uint8_t *p=s->scene_transfer.bytes+304+288*i;
          scene_item item={.type=p[3],.w=p[7],.h=p[8]};
          if(scene_requires_compact(&item))compact_ok=false;
        }
      ok = compact_ok && points_ok && title_ok && !(payload[0] == SC_MSG_BEGIN && used == 13 &&
             scene_u32(payload + 5) == SCENE_BYTES &&
             !(s->caps.capabilities & SCREEN_CAP_SCENE_V3)) &&
           scene_receive(&s->scene_transfer, &s->model.scene, payload, used);
      uint8_t ack[7] = {SC_MSG_ACK, payload[0], ok ? 0 : 1};
      scene_put32(ack + 3, s->model.scene.revision);
      char reply[128];
      size_t n = screen_link_encode(reply, sizeof(reply), s->caps.session,
                                    ++s->tx_sequence, ack, sizeof(ack));
      if (n && s->send)
        s->send(s->context, reply, n);
      if (ok && payload[0] == SC_MSG_COMMIT) {
        s->model.page = 0;
        memset(s->model.data, 0, sizeof(s->model.data));
      }
    } else if (used && payload[0] == SC_MSG_SERIES &&
               (s->caps.capabilities & SCREEN_CAP_SCENE_V3)) {
      uint16_t id = used > 3 ? (uint16_t)(payload[2] | payload[3] << 8) : 0;
      for (unsigned i = 0; i < s->model.scene.count; i++)
        if (s->model.scene.item[i].id == id) {
          ok = scene_series_receive(&s->model.data[i], payload, used, s->now);
          break;
        }
    } else if (used && payload[0] == SC_MSG_DATA &&
               (s->caps.capabilities & SCREEN_CAP_SCENE)) {
      uint16_t id = used > 2 ? (uint16_t)(payload[1] | payload[2] << 8) : 0;
      for (unsigned i = 0; i < s->model.scene.count; i++)
        if (s->model.scene.item[i].id == id) {
          ok = scene_data_receive(&s->model.data[i], payload, used, s->now);
          break;
        }
    } else if (used && payload[0] == SC_MSG_WEATHER &&
               (s->caps.capabilities & SCREEN_CAP_SERVICES))
      ok = weather_receive(&s->model.weather, payload, used, s->now);
    else if (used && payload[0] == SC_MSG_REMINDER &&
             (s->caps.capabilities & SCREEN_CAP_SERVICES)) {
      ok = reminder_receive(s->model.reminders, payload, used);
      if (ok && reminder_should_wake(payload[9], s->model.dnd, s->model.ota)) {
        s->model.asleep = false;
        s->reminder_wake = true;
        for (unsigned i = 0; i < s->model.scene.count; i++)
          if (s->model.scene.item[i].type == SC_REMINDERS) {
            s->model.page = s->model.scene.item[i].page;
            break;
          }
      }
    } else if (used == 7 && payload[0] == SC_MSG_CLOCK &&
               (s->caps.capabilities & SCREEN_CAP_SERVICES)) {
      uint32_t epoch = scene_u32(payload + 1);
      int16_t offset = (int16_t)(payload[5] | payload[6] << 8);
      ok = screen_runtime_clock(s, epoch, offset, s->now, true);
    } else if (s->caps.capabilities & SCREEN_CAP_WIDGETS)
      ok = screen_widget_apply(&s->model.external, payload, used, s->now);
    if (ok)
      s->last_sequence = seq;
    return;
  }
  if (s->factory)
    s->factory(s->context, line, length);
}
void screen_runtime_init(screen_runtime *s) {
  screen_init(&s->model);
  memset(&s->display, 0, sizeof(s->display));
  s->reminder_wake = false;
  screen_caps_reset(&s->caps);
  s->last_sequence = 0;
  s->tx_sequence = 0;
  s->now = 0;
  s->clock_epoch = 0;
  s->clock_at = 0;
  s->clock_revision = s->network_clock_at = 0;
  s->network_clock = false;
  s->timezone = 480;
  s->offered = false;
    s->diag_pending = false;
  memset(&s->scene_transfer, 0, sizeof(s->scene_transfer));
  memset(&s->sht, 0, sizeof(s->sht));
  s->sensor_config_valid = false;
  screen_module_init(&s->module, NULL, NULL, NULL);
  calibration_init(&s->calibration);
  screen_link_init(&s->parser, receive, s);
}
bool screen_runtime_offer(screen_runtime *s, uint32_t nonce) {
  if (s->offered || !nonce || !s->send)
    return false;
  char line[80];
  screen_caps previous = s->caps;
  size_t used = screen_caps_offer(&s->caps, line, sizeof(line), nonce,
                                  SCREEN_CAP_WIDGETS | SCREEN_CAP_SCENE |
                                      SCREEN_CAP_SERVICES | SCREEN_CAP_BACKLIGHT |
                                      SCREEN_CAP_DISPLAY | SCREEN_CAP_SCENE_V3 |
                                      SCREEN_CAP_VALUE_MODE | SCREEN_CAP_RESPONSIVE |
                                      SCREEN_CAP_MODULE_CONTROL | SCREEN_CAP_APPEARANCE |
                                      SCREEN_CAP_WALLPAPER | SCREEN_CAP_MULTI_UNITS |
                                      SCREEN_CAP_HIDE_TITLE | SCREEN_CAP_FONT_BANK |
                                      SCREEN_CAP_LANGUAGE | SCREEN_CAP_CHART_POINTS | SCREEN_CAP_PAGE_RAIL | SCREEN_CAP_RESOURCE_IO | SCREEN_CAP_DISPLAY_DIAG | SCREEN_CAP_PAGE_CONTROL | SCREEN_CAP_COMPACT_WIDGETS);
  if (!used || !s->send(s->context, line, used)) {
    screen_caps_reset(&s->caps);
    return false;
  }
  if (previous.ready && previous.session == nonce)
    s->caps = previous;
  s->offered = true;
  return true;
}
bool screen_runtime_backlight(screen_runtime *s, unsigned requested,
                              unsigned ceiling, unsigned applied, bool asleep) {
  uint8_t payload[SCREEN_BACKLIGHT_BYTES];
  if (!s || !s->caps.ready ||
      !(s->caps.capabilities & SCREEN_CAP_BACKLIGHT) || !s->send ||
      s->tx_sequence == UINT32_MAX ||
      !screen_backlight_encode(payload, requested, ceiling, applied, asleep))
    return false;
  char wire[128];
  size_t n = screen_link_encode(wire, sizeof(wire), s->caps.session,
                                s->tx_sequence + 1, payload, sizeof(payload));
  if (!n || !s->send(s->context, wire, n))
    return false;
  s->tx_sequence++;
  return true;
}
bool screen_runtime_display_report(screen_runtime *s, unsigned normal,
                                   unsigned ceiling, unsigned applied) {
  unsigned target = s && s->model.ota ? SCREEN_OTA_BRIGHTNESS :
                    s ? s->display.target : 0;
  if (!s || !s->display.configured || !s->caps.ready || !s->send ||
      !(s->caps.capabilities & SCREEN_CAP_DISPLAY) ||
      s->tx_sequence == UINT32_MAX || normal > 100 || ceiling > 100 ||
      applied > ceiling || applied > target || s->model.page >= 8)
    return false;
  uint8_t payload[13] = {SCREEN_MSG_BACKLIGHT, 2, normal, ceiling, applied,
                         s->model.ota ? false : s->display.sleeping, target,
                         s->model.ota ? 0 : s->display.reason, s->model.page};
  scene_put32(payload + 9, s->display.policy.revision);
  char wire[128];
  size_t n = screen_link_encode(wire, sizeof(wire), s->caps.session,
                                s->tx_sequence + 1, payload, sizeof(payload));
  if (!n || !s->send(s->context, wire, n)) return false;
  s->tx_sequence++; return true;
}
void screen_runtime_feed(screen_runtime *s, const void *data, size_t n,
                         uint32_t now) {
  s->now = now;
  screen_link_feed(&s->parser, data, n);
}
void screen_runtime_tick(screen_runtime *s, uint32_t now) {
  s->now = now;
  if (screen_module_tick(&s->module, now)) screen_runtime_module_report(s);
  if (s->clock_epoch) {
    uint32_t seconds = (now - s->clock_at) / 1000;
    if (seconds > UINT32_MAX - s->clock_epoch) {
      s->clock_epoch = 0;
      s->model.clock_valid = false;
    } else {
      s->clock_epoch += seconds;
      s->clock_at += seconds * 1000;
      uint64_t minute = ((int64_t)s->clock_epoch + s->timezone * 60) / 60;
      s->model.clock_valid = true;
      s->model.local_day = (uint32_t)(((int64_t)s->clock_epoch + s->timezone * 60) / 86400);
      s->model.hour = minute / 60 % 24;
      s->model.minute = minute % 60;
    }
  }
  /* Latch expiry so data cannot become fresh again after a full uptime wrap. */
  for (unsigned i = 0; i < SCREEN_METRICS; i++)
    if (!screen_sample_valid(&s->model, i, now))
      s->model.samples[i].valid = false;
  for (unsigned i = 0; i < SCREEN_WIDGETS; i++)
    if (!screen_widget_valid(&s->model.external.items[i], now))
      s->model.external.items[i].expired = true;
  for (unsigned i = 0; i < SCENE_ITEMS; i++)
    if (!scene_data_fresh(&s->model.data[i], now))
      s->model.data[i].expired = true;
  if (s->model.weather.valid &&
      (uint32_t)(now - s->model.weather.at) >= s->model.weather.ttl)
    s->model.weather.expired = true;
  screen_record(&s->model, now);
}

bool screen_runtime_clock(screen_runtime *s, uint32_t utc, int16_t offset,
                          uint32_t now, bool network) {
  if (utc < 1700000000u || offset < -720 || offset > 840)
    return false;
  s->clock_epoch = utc;
  s->clock_at = now;
  s->timezone = offset;
  if (network) {
    s->clock_revision++;
    s->network_clock_at = now;
    s->network_clock = true;
  }
  uint64_t minute = ((int64_t)utc + offset * 60) / 60;
  s->model.clock_valid = true;
  s->model.local_day = (uint32_t)(((int64_t)utc + offset * 60) / 86400);
  s->model.hour = minute / 60 % 24;
  s->model.minute = minute % 60;
  return true;
}

void screen_runtime_sensor_config(screen_runtime *s,
                                  const sensor_sht_config *config) {
  if (!s)
    return;
  s->sensor_config_valid = config != NULL;
  if (config)
    s->sensor_config = *config;
}
bool screen_runtime_sht(screen_runtime *s, const uint8_t frame[6],
                        uint8_t status, bool ramp, bool force, uint32_t now) {
  sensor_sht_result result;
  if (!s || !s->sensor_config_valid ||
      !sensor_sht_compensate(&s->sht, &s->sensor_config, frame, status, ramp,
                             force, &result))
    return false;
  screen_sample_set(&s->model, MET_TEMP, result.temperature10 / 10.0f, true,
                    now);
  screen_sample_set(&s->model, MET_HUM, result.humidity10 / 10.0f, true, now);
  return true;
}
static bool calibration_fresh(const screen_runtime *s, unsigned i) {
  return !s->model.ota && screen_sample_valid(&s->model,
      i ? MET_TVOC : MET_CO2, s->now) &&
      (!i || (!s->module.inhibited && !s->module.duration));
}
uint32_t screen_runtime_calibration_warmup(const screen_runtime *s, unsigned i) {
  uint32_t required = i ? 320000 : 180000;
  if (!calibration_fresh(s, i) || !s->calibration_valid_since[i]) return required;
  uint32_t elapsed = s->now - s->calibration_valid_since[i];
  return elapsed >= required ? 0 : required - elapsed;
}
bool screen_runtime_calibrate(screen_runtime *s, unsigned channels) {
  if (!s || !s->calibration_event || !channels || (channels & ~3u) ||
      s->calibration.pending) return false;
  for (unsigned i=0;i<2;i++) if (channels & (1u<<i)) {
    if (screen_runtime_calibration_warmup(s, i)) return false;
  }
  if (!calibration_request(&s->calibration, channels)) return false;
  for (unsigned i=0;i<2;i++) if (channels & (1u<<i)) {
    s->calibration.jobs[i].status = 0x51;
    s->calibration_result[i] = SCREEN_CAL_IDLE;
  }
  return true;
}
static bool calibration_event(void *context, unsigned channel, unsigned event,
                               const uint8_t *data, size_t size) {
  screen_runtime *s = context;
  unsigned i = channel == CAL_CO2 ? 0 : 1;
  bool ok = s->calibration_event(s->context, channel, event, data, size);
  if (event == CAL_COMMAND && ok) s->calibration_result[i] = SCREEN_CAL_SENT;
  if (event == CAL_REPORT && size == 1 &&
      s->calibration_result[i] != SCREEN_CAL_ACKNOWLEDGED &&
      s->calibration_result[i] != SCREEN_CAL_SENSOR_LOST)
    s->calibration_result[i] = data[0];
  return ok;
}
void screen_runtime_calibration_poll(screen_runtime *s, uint32_t age,
                                     uint16_t co2, uint16_t tvoc) {
  if (!s) return;
  for (unsigned i=0;i<2;i++) {
    bool fresh = calibration_fresh(s, i);
    if (!fresh) s->calibration_valid_since[i] = 0;
    else if (!s->calibration_valid_since[i])
      s->calibration_valid_since[i] = s->now ? s->now : 1;
    calibration_job *j = &s->calibration.jobs[i];
    if (!fresh && (s->calibration.pending & (1u<<i)) && j->stage < 8) {
      j->status = 0x55; j->issued = 0; j->stage = 8;
      s->calibration_result[i] = SCREEN_CAL_SENSOR_LOST;
    }
  }
  calibration_poll(&s->calibration, age, co2, tvoc,
                    s->calibration_event ? calibration_event : NULL, s);
}
bool screen_runtime_calibration_service(screen_runtime *s, bool hc, int co2,
                                        bool ht, int tvoc) {
  return hc && ht && (co2==0 || co2==1) && (tvoc==0 || tvoc==1) &&
         screen_runtime_calibrate(s, (co2 ? CAL_CO2 : 0) | (tvoc ? CAL_TVOC : 0));
}
bool screen_runtime_calibration_cancel(screen_runtime *s) {
  if (!s || !s->calibration.pending) return false;
  for (unsigned i=0;i<2;i++) if ((s->calibration.pending & (1u<<i)) &&
      s->calibration.jobs[i].issued) return false;
  for (unsigned i=0;i<2;i++) if (s->calibration.pending & (1u<<i)) {
    s->calibration_result[i] = SCREEN_CAL_CANCELLED;
    s->calibration.jobs[i].stage = 1;
    s->calibration.jobs[i].saved_status = 0;
  }
  s->calibration.pending = 0;
  return true;
}
void screen_runtime_calibration_ack(screen_runtime *s) {
  if (s->calibration.co2_command_pending &&
      s->calibration_result[0] == SCREEN_CAL_SENT) {
    s->calibration_result[0] = SCREEN_CAL_ACKNOWLEDGED;
    s->calibration.co2_command_pending = false;
  }
}
