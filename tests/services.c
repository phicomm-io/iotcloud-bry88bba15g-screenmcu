#include "services.h"
#include "power.h"
#include "scene.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  screen_scene s, decoded;
  scene_defaults(&s);
  assert(scene_validate(&s));
  uint8_t bytes[SCENE_BYTES];
  s.revision=2;
  assert(scene_encode(&s, bytes));
  assert(scene_decode(&decoded, bytes, sizeof(bytes)));
  assert(decoded.count == s.count);
  scene_transaction transfer = {0};
  uint8_t begin[13] = {SC_MSG_BEGIN}, chunk[520] = {SC_MSG_CHUNK},
          commit[5] = {SC_MSG_COMMIT};
  scene_put32(begin + 1, 2);
  scene_put32(begin + 5, SCENE_BYTES);
  scene_put32(begin + 9, scene_crc32(bytes, sizeof(bytes)));
  scene_put32(commit + 1, 2);
  assert(scene_receive(&transfer, &decoded, begin, sizeof(begin)));
  assert(!scene_receive(&transfer, &decoded, commit, sizeof(commit)));
  assert(decoded.revision == s.revision);
  assert(scene_receive(&transfer, &decoded, begin, sizeof(begin)));
  for (unsigned at = 0; at < sizeof(bytes);) {
    unsigned n = sizeof(bytes) - at;
    if (n > 500)
      n = 500;
    scene_put32(chunk + 1, at);
    memcpy(chunk + 5, bytes + at, n);
    assert(scene_receive(&transfer, &decoded, chunk, n + 5));
    at += n;
  }
  assert(scene_receive(&transfer, &decoded, commit, sizeof(commit)));
  assert(decoded.revision == 2);
  decoded.item[1].id = decoded.item[0].id;
  assert(!scene_validate(&decoded));
  decoded = s;
  decoded.item[1].x = decoded.item[0].x;
  decoded.item[1].y = decoded.item[0].y;
  assert(!scene_validate(&decoded));
  uint8_t data[16] = {SC_MSG_DATA, 42, 0, 1};
  scene_put32(data + 4, 2);
  data[8] = 3;
  memcpy(data + 9, "abc", 3);
  float v = 12.5;
  memcpy(data + 12, &v, 4);
  scene_data d = {0};
  assert(scene_data_receive(&d, data, 16, UINT32_MAX - 100));
  assert(scene_data_fresh(&d, 1));
  assert(!scene_data_fresh(&d, 2000));
  v = NAN;
  memcpy(data + 12, &v, 4);
  assert(!scene_data_receive(&d, data, 16, 1));
  assert(!scene_utf8("\xc0\x80", 4));
  screen_power p = {.asleep = true,
                    .brightness = 60,
                    .power_timeout = 2,
                    .battery_timeout = 1};
  unsigned event = power_tick(&p, true, false, true, false);
  assert(event & POWER_WAKE);
  assert(!(power_tick(&p, false, false, true, false) & POWER_PAGE));
  assert(!(power_tick(&p, true, false, true, false) & POWER_PAGE));
  assert(power_tick(&p, false, false, true, false) & POWER_PAGE);
  for (unsigned i = 0; i < 100; i++)
    event = power_tick(&p, true, false, true, false);
  assert(p.front_long);
  assert(!(power_tick(&p, false, false, true, false) & POWER_PAGE));
  p = (screen_power){.ota = true};
  for (unsigned i = 0; i < 100; i++)
    assert(!power_tick(&p, true, true, false, true));
  assert(!p.asleep);
  p.ota = false;
  assert(!(power_tick(&p, false, false, true, false) &
           (POWER_PAGE | POWER_PROVISION)));
  assert(power_in_dnd(23 * 60, 22 * 60, 8 * 60, true));
  assert(power_in_dnd(7 * 60, 22 * 60, 8 * 60, true));
  assert(!power_in_dnd(12 * 60, 22 * 60, 8 * 60, true));
  reminder_schedule schedule = {0};
  uint32_t now = 1762387200, ids[32];
  schedule.rules[0] = (reminder_rule){.id = 7,
                                      .kind = REM_ONCE,
                                      .epoch = now + 10,
                                      .enabled = true,
                                      .title = "test",
                                      .wake = REM_ALWAYS_WAKE};
  assert(reminder_validate(&schedule.rules[0]));
  assert(!reminder_due(&schedule, now, true, ids));
  assert(reminder_due(&schedule, now + 10, true, ids) == 1 && ids[0] == 7);
  assert(!reminder_due(&schedule, now + 11, true, ids));
  assert(!reminder_due(&schedule, now + 1, true, ids));
  assert(!reminder_due(&schedule, now + 10, true, ids));
  assert(!reminder_due(&schedule, now + 1000, true, ids));
  assert(!reminder_should_wake(REM_ALWAYS_WAKE, true, true));
  assert(!reminder_should_wake(REM_RESPECT_DND, true, false));
  assert(reminder_should_wake(REM_ALWAYS_WAKE, true, false));
  puts("atomic scenes, bounded payloads, touch wake, OTA priority and reminder "
       "clock scenarios passed");
}
