#include "boot_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t image[SCREEN_APP_LIMIT], flag = 0xff;
static unsigned erases, writes;
static bool read_fail, erase_fail, program_fail;
static uint32_t dirty_tail;
static bool tail_read_fail;
static bool read_stage(void *c, uint32_t a, void *p, size_t n) {
  (void)c;
  assert(a >= SCREEN_OTA_STAGE && a - SCREEN_OTA_STAGE + n <= sizeof(image));
  memcpy(p, image + a - SCREEN_OTA_STAGE, n);
  return true;
}
static bool read_flag(void *c, uint32_t a, void *p, size_t n) {
  (void)c;
  if (n == 256) {
    assert(a >= SCREEN_OTA_STAGE + SCREEN_APP_LIMIT &&
           a + n <= SCREEN_OTA_STAGE + SCREEN_BOOT_COPY_BYTES);
    memset(p, 0xff, n);
    if (dirty_tail >= a && dirty_tail < a + n)
      ((uint8_t *)p)[dirty_tail - a] = 0;
    return !tail_read_fail;
  }
  assert(a == SCREEN_BOOT_FLAG && n == 1);
  *(uint8_t *)p = flag;
  return !read_fail;
}
static bool erase_flag(void *c, uint32_t a, size_t n) {
  (void)c;
  assert(a == SCREEN_BOOT_FLAG && n == SCREEN_BOOT_FLAG_BLOCK);
  erases++;
  if (!erase_fail) flag = 0xff;
  return !erase_fail;
}
static bool write_flag(void *c, uint32_t a, const void *p, size_t n) {
  (void)c;
  assert(a == SCREEN_BOOT_FLAG && n == 1 && *(const uint8_t *)p == 0xa1);
  writes++;
  /* Simulate a silent program failure, caught by readback. */
  if (!program_fail) flag = *(const uint8_t *)p;
  return true;
}
int main(void) {
  screen_boot_control b = {.read = read_flag, .erase = erase_flag, .write = write_flag};
  assert(screen_boot_ack(&b) && !erases && !writes);
  flag = 0x12;
  assert(!screen_boot_ack(&b) && !erases);
  flag = 0xa1;
  erase_fail = true;
  assert(!screen_boot_ack(&b) && flag == 0xa1);
  erase_fail = false;
  assert(screen_boot_ack(&b) && flag == 0xff);
  flag = 0xa2;
  assert(screen_boot_ack(&b) && flag == 0xff);
  screen_update u = {.read = read_stage};
  unsigned before = erases;
  assert(!screen_boot_commit(&b, &u) && erases == before);
  for (unsigned i = 0; i < sizeof(image); i++) image[i] = i * 137 ^ (i >> 8);
  u.transfer.size = u.transfer.received = sizeof(image);
  u.transfer.verified = true;
  screen_md5 m;
  screen_md5_init(&m);
  screen_md5_update(&m, image, sizeof(image));
  screen_md5_final(&m, u.expected);
  image[20000] ^= 1;
  assert(!screen_boot_commit(&b, &u) && erases == before && !writes);
  image[20000] ^= 1;
  u.failed = false;
  u.transfer.verified = true;
  assert(screen_boot_tail_blank(&b));
  tail_read_fail = true;
  assert(!screen_boot_commit(&b, &u) && erases == before && !writes);
  tail_read_fail = false;
  const uint32_t bad[] = {SCREEN_OTA_STAGE + SCREEN_APP_LIMIT,
                         0x7ea000, SCREEN_BOOT_FLAG,
                         SCREEN_OTA_STAGE + SCREEN_BOOT_COPY_BYTES - 1};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    dirty_tail = bad[i];
    assert(!screen_boot_commit(&b, &u) && erases == before && !writes);
  }
  dirty_tail = 0;
  program_fail = true;
  assert(!screen_boot_commit(&b, &u) && flag == 0xff && writes == 1);
  program_fail = false;
  assert(screen_boot_commit(&b, &u) && flag == 0xa1 && writes == 2);
  read_fail = true;
  before = erases;
  assert(!screen_boot_ack(&b) && erases == before);
  puts("factory flag: exact range, unknown state, readback, corrupt image and nonblank factory-copy tail rejection passed");
}
