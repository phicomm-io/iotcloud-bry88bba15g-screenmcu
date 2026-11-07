#include "screen.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static screen_model model;
int main(int argc, char **argv) {
  /* Export all three previous-state transitions for a machine-code differential
   * comparison; the ordinary test checks hysteresis and stale UI semantics. */
  if (argc == 2) {
    FILE *f = fopen(argv[1], "wb");
    assert(f);
    for (unsigned prior = 1; prior <= 3; prior++)
      for (unsigned raw = 0; raw <= 65535; raw++)
        assert(fputc(screen_tvoc_level(prior, raw), f) != EOF);
    assert(!fclose(f));
  }
  screen_init(&model);
  assert(!model.tvoc_level);
  const unsigned raw[] = {0, 690, 691, 510, 509, 1990, 1991, 1691, 1690, 510, 509};
  const unsigned level[] = {1, 1, 2, 2, 1, 2, 3, 3, 2, 2, 1};
  for (unsigned i = 0; i < sizeof(raw) / sizeof(raw[0]); i++) {
    screen_sample_set(&model, MET_TVOC, raw[i], true, i);
    assert(model.tvoc_level == level[i]);
  }
  screen_sample_set(&model, MET_TVOC, NAN, true, 20);
  assert(!screen_sample_valid(&model, MET_TVOC, 20));
  assert(model.tvoc_level == 1);
  screen_sample_set(&model, MET_TVOC, 2000, true, UINT32_MAX - 10);
  assert(model.tvoc_level == 3);
  assert(screen_sample_valid(&model, MET_TVOC, 10));
  assert(!screen_sample_valid(&model, MET_TVOC, 20000));
  assert(!strcmp(screen_tvoc_text(1), "优"));
  assert(!strcmp(screen_tvoc_text(2), "良"));
  assert(!strcmp(screen_tvoc_text(3), "差"));
  assert(!strcmp(screen_tvoc_text(0), "--"));
  puts("TVOC hysteresis, grade labels, invalid samples and expiry passed");
}
