#ifndef SCREEN_STARTUP_UI_H
#define SCREEN_STARTUP_UI_H
#include "screen.h"
#define SCREEN_SENSOR_STARTUP_MS 60000u
void screen_startup_begin(uint32_t);
/* Nonblocking LVGL overlay. Page changes and the UART owner keep running. */
void screen_startup_ui(const screen_model *,uint32_t);
#endif
