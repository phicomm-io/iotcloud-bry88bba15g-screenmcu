#ifndef SCREEN_SENSOR_RX_H
#define SCREEN_SENSOR_RX_H
#include <stdbool.h>
#include <stdint.h>
#define SENSOR_RX_SIZE 256u
#define SENSOR_RX_GAP 0x100u
/* One interrupt producer and one foreground consumer. No allocation or SDRAM. */
typedef struct {
  volatile uint16_t bytes[SENSOR_RX_SIZE], read, write;
  volatile bool overflow;
  uint32_t at;
  bool started;
} sensor_rx;
typedef struct { uint8_t bytes[32]; unsigned used; } sensor_rx_frame;
void sensor_rx_push(sensor_rx *, uint8_t, uint32_t);
int sensor_rx_pop(sensor_rx *);
/* Caller excludes the UART interrupt while clearing a queue. */
void sensor_rx_clear(sensor_rx *);
/* A returned frame includes its header and checksum; caller validates both. */
unsigned sensor_rx_parse(sensor_rx_frame *, int);
#endif
