#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
/* Largest-page rendering and repeated page changes are tested in ui_render. */
#define LV_MEM_SIZE (128U * 1024U)
#define LV_TICK_CUSTOM 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(zh32)
#define LV_FONT_DEFAULT &zh32
/* Absolute native coordinates need only objects, labels and arcs. */
#define LV_USE_ANIMIMG 0
#define LV_USE_BAR 0
#define LV_USE_BTN 0
#define LV_USE_BTNMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 1
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_FLEX 0
#define LV_USE_GRID 0
#define LV_LABEL_TEXT_SELECTION 0
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0
#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#endif
