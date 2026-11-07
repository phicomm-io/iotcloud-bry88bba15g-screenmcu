#pragma once
#include <stdbool.h>
/* User text is kept verbatim. Only known stock labels and provider weather
 * descriptions are translated. No network request or heap allocation. */
const char *screen_translate(bool english,const char *text);
const char *screen_weather_text(bool english,const char *text);
