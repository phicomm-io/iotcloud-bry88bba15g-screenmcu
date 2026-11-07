#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Stock config +4/+6/+8/+10, signed values in tenths. Do not invent board defaults. */
typedef struct { int16_t temperature, secondary, hcho_heat, humidity; } sensor_sht_config;
typedef struct {
 uint32_t samples;
 double base, hcho;
 bool settled;
} sensor_sht_state;
typedef struct {
 int32_t temperature10, humidity10, raw_temperature10, raw_humidity10, unclamped_temperature10;
} sensor_sht_result;
/* One successful SHT measurement per call, not one call per RTOS tick.
 * CRC failure leaves both state and result unchanged. ramp/force reflect stock
 * flags +0x64/+0x65. No elapsed-wall-time approximation is substituted. */
bool sensor_sht_compensate(sensor_sht_state *,const sensor_sht_config *,
 const uint8_t frame[6],uint8_t hcho_status,bool ramp,bool force,sensor_sht_result *);
/* UART3 CMD=1 value and raw status. Wire units intentionally not guessed. */
bool sensor_hcho(const uint8_t *,unsigned,uint16_t *,uint8_t *);
