#pragma once
#include "SWM341.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef SCREEN_BACKLIGHT_MAX_PERCENT
#define SCREEN_BACKLIGHT_MAX_PERCENT 100
#endif
#if SCREEN_BACKLIGHT_MAX_PERCENT < 0 || SCREEN_BACKLIGHT_MAX_PERCENT > 100
#error "SCREEN_BACKLIGHT_MAX_PERCENT must be from 0 to 100"
#endif
extern volatile uint32_t board_ms;
typedef struct {
  uint32_t control, reset_value, clock, reset_status;
} board_watchdog_state;
extern board_watchdog_state board_inherited_watchdog;
void board_watchdog_feed(void);
bool board_boot_read(void *, uint32_t, void *, size_t);
void board_early(void);
uint32_t board_nonce(void *);
bool board_sdram(void);
extern volatile uint32_t board_sdram_error;
void board_display(void);
uint32_t board_time_us(void);
void board_lvgl_tick_start(void);
void board_lvgl_tick_stop(void);
void board_display_diag(uint8_t summary[128]);
void board_backlight(unsigned);
unsigned board_backlight_percent(void);
bool board_send(void *, const char *, size_t);
int board_receive(void);
void board_delay(unsigned);
void board_recovery(void) __attribute__((noreturn));
void board_nor_init(void);
unsigned board_nor_read_mode(void);
uint32_t board_jedec(void);
uint8_t board_nor_status(unsigned);
bool board_nor_read(void *, uint32_t, void *, size_t);
bool board_nor_write(void *, uint32_t, const void *, size_t);
bool board_nor_erase(void *, uint32_t, size_t);
bool board_flag_erase(void *, uint32_t, size_t);
bool board_flag_write(void *, uint32_t, const void *, size_t);
void board_sensor_init(void) __attribute__((noinline));
int board_sensor_receive(unsigned);
void board_sensor_flush(void);
void board_module_write(void *, bool);
bool board_module_read(void *);
void board_sensor_checkpoint(unsigned) __attribute__((noinline));
void board_rtc_init(void);
bool board_rtc_read(void *, uint8_t, uint8_t *, size_t);
bool board_rtc_write(void *, uint8_t, const uint8_t *, size_t);
bool board_sht_command(uint16_t);
bool board_sht_read(uint8_t[6]);
unsigned board_battery(void);

bool board_resource_write(void *,uint32_t,const void *,size_t);
bool board_resource_erase(void *,uint32_t,size_t);
