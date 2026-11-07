#include "boot_control.h"
static bool clear(const screen_boot_control *b) {
  uint8_t flag;
  return b && b->read && b->erase &&
         b->erase(b->context, SCREEN_BOOT_FLAG, SCREEN_BOOT_FLAG_BLOCK) &&
         b->read(b->context, SCREEN_BOOT_FLAG, &flag, 1) && flag == 0xff;
}
bool screen_boot_ack(const screen_boot_control *b) {
  uint8_t flag;
  if (!b || !b->read || !b->read(b->context, SCREEN_BOOT_FLAG, &flag, 1))
    return false;
  if (flag == 0xff)
    return true;
  /* Preserve unknown state instead of interpreting it as an OTA. */
  return (flag == 0xa1 || flag == 0xa2) && clear(b);
}
bool screen_boot_tail_blank(const screen_boot_control *b) {
  uint8_t bytes[256];
  if (!b || !b->read)
    return false;
  /* Check before changing the flag. The acknowledged flag block is included.
   * FF does not clear bits in the un-erased internal configuration pages.
   * Do not erase unknown NOR contents to make this check pass. */
  for (uint32_t at = SCREEN_APP_LIMIT; at < SCREEN_BOOT_COPY_BYTES;
       at += sizeof(bytes)) {
    if (!b->read(b->context, SCREEN_OTA_STAGE + at, bytes, sizeof(bytes)))
      return false;
    for (unsigned i = 0; i < sizeof(bytes); i++)
      if (bytes[i] != 0xff)
        return false;
  }
  return true;
}
bool screen_boot_commit(const screen_boot_control *b, screen_update *u) {
  if (!b || !b->write || !u || !u->transfer.verified ||
      u->transfer.size != SCREEN_APP_LIMIT || !screen_update_recheck(u) ||
      !screen_boot_tail_blank(b) || !clear(b))
    return false;
  const uint8_t flag = 0xa1;
  uint8_t check = 0;
  return b->write(b->context, SCREEN_BOOT_FLAG, &flag, 1) &&
         b->read(b->context, SCREEN_BOOT_FLAG, &check, 1) && check == flag;
}
