/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Execute the real rtc_bus.c against a wire-level I2C slave. This models
 * protocol/electrical faults; it does not measure physical rise times. */
#include "board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GPIO_TypeDef test_c, test_d;
SYS_TypeDef test_sys;
uint32_t CyclesPerUs = 4;
enum phase {RX, ACK_FALL, ACK_RISE, ACK_END, TX, TX_FALL, MASTER_ACK,
            MASTER_ACK_END, WAIT};
static struct {
  bool scl, sda, slave_low, active, address, read, expect_reg, ack;
  bool stuck_clock, stuck_data;
  unsigned phase, bits, value, reg, nops, clock_reads, starts, stops, init;
  unsigned recovery_clocks, recover_after, nack_at, used, sent;
  uint8_t bytes[16], received[64];
  bool master_acks[16], more;
} bus;
void test_nop(void) { assert(++bus.nops < 100000); }
static bool clock_line(void) {return bus.scl && !bus.stuck_clock;}
static bool data_line(void) {
  return bus.sda && !bus.slave_low && !bus.stuck_data;
}
static void pin(GPIO_TypeDef *p, uint32_t n) {
  assert((p == GPIOC && n == 7) || (p == GPIOD && n == 2));
}
void GPIO_Init(GPIO_TypeDef *p, uint32_t n, uint32_t dir, uint32_t up,
               uint32_t down, uint32_t od) {
  pin(p, n);
  assert(dir == (bus.init >= 2) && up && !down && od && bus.scl && bus.sda);
  bus.init++;
}
static void receive_byte(void) {
  assert(bus.used < sizeof(bus.received));
  bus.received[bus.used++] = (uint8_t)bus.value;
  bus.ack = bus.used != bus.nack_at;
  if (bus.address) {
    bus.address = false;
    bus.read = bus.value & 1;
    bus.expect_reg = !bus.read;
    bus.ack = bus.ack && (bus.value >> 1 == 0x51);
  } else if (bus.ack && bus.expect_reg) {
    bus.reg = bus.value;
    bus.expect_reg = false;
  } else if (bus.ack) {
    assert(bus.reg < sizeof(bus.bytes));
    bus.bytes[bus.reg++] = (uint8_t)bus.value;
  }
}
static void tx_bit(void) {
  assert(bus.reg < sizeof(bus.bytes));
  bus.slave_low = !(bus.bytes[bus.reg] & (0x80u >> bus.bits));
}
static void rising(void) {
  if (bus.stuck_data) {
    bus.recovery_clocks++;
    if (bus.recover_after && bus.recovery_clocks == bus.recover_after)
      bus.stuck_data = false;
  }
  if (!bus.active) return;
  switch (bus.phase) {
  case RX:
    bus.value = bus.value << 1 | data_line();
    if (++bus.bits == 8) {receive_byte(); bus.phase = ACK_FALL;}
    break;
  case ACK_RISE: bus.phase = ACK_END; break;
  case TX: if (++bus.bits == 8) bus.phase = TX_FALL; break;
  case MASTER_ACK:
    assert(bus.sent < sizeof(bus.master_acks));
    bus.more = !data_line();
    bus.master_acks[bus.sent++] = bus.more;
    bus.phase = MASTER_ACK_END;
    break;
  default: break;
  }
}
static void falling(void) {
  if (!bus.active) return;
  switch (bus.phase) {
  case ACK_FALL:
    bus.slave_low = bus.ack; bus.phase = ACK_RISE; break;
  case ACK_END:
    bus.slave_low = false; bus.bits = bus.value = 0;
    bus.phase = bus.ack ? (bus.read ? TX : RX) : WAIT;
    if (bus.phase == TX) tx_bit();
    break;
  case TX: tx_bit(); break;
  case TX_FALL:
    bus.slave_low = false; bus.phase = MASTER_ACK; break;
  case MASTER_ACK_END:
    bus.bits = 0; bus.reg++; bus.phase = bus.more ? TX : WAIT;
    if (bus.phase == TX) tx_bit();
    break;
  default: break;
  }
}
static void set_pin(GPIO_TypeDef *p, uint32_t n, bool high) {
  pin(p, n);
  bool old_clock = clock_line(), old_data = data_line();
  if (p == GPIOD) {
    bus.scl = high;
    if (!old_clock && clock_line()) rising();
    if (old_clock && !clock_line()) falling();
  } else {
    bus.sda = high;
    if (old_clock && old_data != data_line()) {
      if (!data_line()) {
        bus.starts++; bus.active = bus.address = true;
        bus.read = bus.slave_low = false;
        bus.bits = bus.value = 0; bus.phase = RX;
      } else {
        bus.stops++; bus.active = bus.slave_low = false;
      }
    }
  }
}
void GPIO_SetBit(GPIO_TypeDef *p, uint32_t n) {set_pin(p, n, true);}
void GPIO_ClrBit(GPIO_TypeDef *p, uint32_t n) {set_pin(p, n, false);}
uint32_t GPIO_GetBit(GPIO_TypeDef *p, uint32_t n) {
  pin(p, n);
  if (p == GPIOD) {bus.clock_reads++; return clock_line();}
  return data_line();
}
static void setup(void) {
  memset(&bus, 0, sizeof(bus));
  bus.scl = bus.sda = true;
  board_rtc_init();
  assert(bus.init == 4 && (SYS->CLKEN0 & 12) == 12);
  for (unsigned i = 0; i < 16; i++) bus.bytes[i] = (uint8_t)(i * 17);
}
static void released(void) {assert(bus.scl && bus.sda && !bus.active);}
int main(void) {
  uint8_t out[9], data[7] = {0, 1, 2, 3, 4, 5, 6};
  setup();
  assert(board_rtc_read(NULL, 0, out, 9));
  assert(!memcmp(out, bus.bytes, 9));
  assert(bus.starts == 2 && bus.stops == 1 && bus.used == 3);
  assert(!memcmp(bus.received, (uint8_t[]){0xa2, 0, 0xa3}, 3));
  assert(bus.sent == 9 && !bus.master_acks[8]);
  for (unsigned i = 0; i < 8; i++) assert(bus.master_acks[i]);
  released();
  setup();
  assert(board_rtc_write(NULL, 2, data, 7));
  assert(!memcmp(bus.bytes + 2, data, 7));
  assert(bus.bytes[0] == 0 && bus.bytes[1] == 17 && bus.bytes[9] == 153);
  assert(bus.starts == 1 && bus.stops == 1 && bus.used == 9);
  assert(!memcmp(bus.received, (uint8_t[]){0xa2, 2, 0, 1, 2, 3, 4, 5, 6}, 9));
  released();
  for (unsigned n = 1; n <= 9; n++) {
    setup(); bus.nack_at = n;
    assert(!board_rtc_write(NULL, 2, data, 7));
    assert(bus.used == n && bus.stops == 1); released();
  }
  for (unsigned n = 1; n <= 3; n++) {
    setup(); bus.nack_at = n;
    assert(!board_rtc_read(NULL, 0, out, 9));
    assert(bus.used == n && bus.stops == 1); released();
  }
  setup(); bus.stuck_clock = true;
  assert(!board_rtc_read(NULL, 0, out, 9));
  assert(bus.clock_reads == 200 && bus.nops < 3000 && !bus.used); released();
  setup(); bus.stuck_data = true;
  assert(!board_rtc_read(NULL, 0, out, 9));
  assert(bus.recovery_clocks == 10 && !bus.used && bus.nops < 1000);
  assert(bus.scl && bus.sda); /* Host releases even when a slave holds low. */
  setup(); bus.stuck_data = true; bus.recover_after = 9;
  assert(board_rtc_read(NULL, 0, out, 9));
  assert(bus.recovery_clocks == 9 && !memcmp(out, bus.bytes, 9)); released();
  setup();
  assert(!board_rtc_read(NULL, 8, out, 2));
  assert(!board_rtc_read(NULL, 0, NULL, 1));
  assert(!board_rtc_read(NULL, 0, out, 0));
  assert(!board_rtc_write(NULL, 9, data, 7));
  assert(!board_rtc_write(NULL, 0, (uint8_t[]){0x20}, 1));
  assert(!bus.starts && !bus.nops);
  puts("Actual RTC GPIO bus: 0x51 repeated START, final NACK, data writes, all "
       "NACK positions, stuck SCL/SDA, nine-clock recovery and pin isolation passed");
}
