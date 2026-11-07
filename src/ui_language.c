#include "ui_language.h"
#include <string.h>
static const char *const labels[][2]={
 {"时钟","Clock"},{"空气数据","Air quality"},{"变化趋势","Trends"},{"户外天气","Outdoor weather"},
 {"提醒事项","Reminders"},{"外部数据","External data"},{"外部统计","External statistics"},{"组合概览","Overview"},
 {"温度","Temperature"},{"湿度","Humidity"},{"甲醛","HCHO"},{"外部","External"},
 {"体感","Feels like"},{"风速 m/s","Wind m/s"},{"已过期","Expired"},
 {"暂无有效数据","No data"},{"暂无提醒","No reminders"},{"优","Good"},{"良","Fair"},{"差","Poor"},
 {"正在更新\n请保持供电","Updating\nKeep power connected"}
};
const char *screen_translate(bool en,const char *s) {
 if(!en)return s;
 for(unsigned i=0;i<sizeof(labels)/sizeof(labels[0]);i++)if(!strcmp(s,labels[i][0]))return labels[i][1];
 return s;
}
static const char *const conditions[][2]={
 {"晴","Clear sky"},{"晴","Sunny"},{"晴","Clear"},{"晴间多云","Mainly clear"},
 {"晴间多云","Partly cloudy"},{"晴间多云","Few clouds"},{"多云","Scattered clouds"},
 {"多云","Broken clouds"},{"多云","Cloudy"},{"阴","Overcast"},{"阴","Overcast clouds"},
 {"雾","Fog"},{"雾","Mist"},{"霾","Haze"},{"冻雾","Rime fog"},
 {"小毛毛雨","Light drizzle"},{"毛毛雨","Moderate drizzle"},{"大毛毛雨","Dense drizzle"},{"毛毛雨","Drizzle"},
 {"小冻毛毛雨","Light freezing drizzle"},{"大冻毛毛雨","Dense freezing drizzle"},
 {"小雨","Light rain"},{"中雨","Moderate rain"},{"大雨","Heavy rain"},{"大雨","Heavy intensity rain"},
 {"小冻雨","Light freezing rain"},{"大冻雨","Heavy freezing rain"},{"冻雨","Freezing rain"},
 {"小雪","Light snow"},{"中雪","Moderate snow"},{"大雪","Heavy snow"},{"雪粒","Snow grains"},{"雪","Snow"},
 {"小阵雨","Light rain showers"},{"中阵雨","Moderate rain showers"},{"强阵雨","Heavy rain showers"},
 {"阵雨","Shower rain"},{"阵雨","Rain showers"},{"小阵雪","Light snow showers"},{"强阵雪","Heavy snow showers"},
 {"雷阵雨","Thunderstorm"},{"雷阵雨伴小冰雹","Thunderstorm with light hail"},
 {"强雷阵雨","Heavy thunderstorm"},{"雷阵雨伴大冰雹","Thunderstorm with heavy hail"},
 {"阵雪","Snow showers"},{"雨夹雪","Sleet"},{"小到中雨","Light to moderate rain"},
 {"暴雨","Rainstorm"},{"大暴雨","Heavy rainstorm"},{"特大暴雨","Severe rainstorm"},
 {"沙尘暴","Sandstorm"},{"浮尘","Dust"},{"扬沙","Sand"},{"强沙尘暴","Severe sandstorm"},
 {"大风","Windy"},{"未知","Unknown"}
};
static bool same(const char *a,const char *b) {
 while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}
 return !*a&&!*b;
}
const char *screen_weather_text(bool en,const char *s) {
 for(unsigned i=0;i<sizeof(conditions)/sizeof(conditions[0]);i++)
  if(same(s,conditions[i][0])||same(s,conditions[i][1]))return conditions[i][en?1:0];
 return s;
}
