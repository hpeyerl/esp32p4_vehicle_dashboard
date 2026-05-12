/**
 * lv_conf.h  —  LVGL v9.5 configuration for EV Dashboard
 * ESP32-P4 + Waveshare 10.1" DSI-Touch-A
 *
 * Place this file at:  src/lv_conf.h
 */

#if 1  /* Set to 1 to enable */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ===========================================================
 *  Colour depth
 *  16 = RGB565  (DSI panels, saves PSRAM bandwidth)
 *  32 = ARGB8888
 * =========================================================== */
#define LV_COLOR_DEPTH 16

/* ===========================================================
 *  Memory  —  allocate from PSRAM via ESP-IDF heap caps
 * =========================================================== */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
    #define LV_MEM_CUSTOM_INCLUDE  <esp_heap_caps.h>
    #define LV_MEM_CUSTOM_ALLOC(s) heap_caps_malloc((s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    #define LV_MEM_CUSTOM_FREE     heap_caps_free
    #define LV_MEM_CUSTOM_REALLOC  heap_caps_realloc
#endif

/* ===========================================================
 *  Tick  —  use esp_timer, no separate ISR needed
 * =========================================================== */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE           <esp_timer.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR     ((uint32_t)(esp_timer_get_time() / 1000ULL))
#endif

/* ===========================================================
 *  Operating system  —  FreeRTOS
 * =========================================================== */
#define LV_USE_OS   LV_OS_NONE

/* ===========================================================
 *  Software renderer
 * =========================================================== */
#define LV_USE_DRAW_SW  1
#if LV_USE_DRAW_SW
    /* CRITICAL: disable ARM-specific ASM backends.
     * ESP32-P4 is RISC-V — Helium/NEON will not assemble. */
    #define LV_DRAW_SW_ASM  LV_DRAW_SW_ASM_NONE

    /* Disable complex rendering to save CPU (no rounded shadow etc.) */
    #define LV_DRAW_SW_COMPLEX  0

    /* Shadow cache — 0 = disabled */
    #define LV_DRAW_SW_SHADOW_CACHE_SIZE  0
#endif

/* ===========================================================
 *  Fonts  —  Montserrat sizes used in dashboard_ui.h
 * =========================================================== */
#define LV_FONT_MONTSERRAT_10  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_40  1
#define LV_FONT_MONTSERRAT_48  1

#define LV_FONT_DEFAULT  &lv_font_montserrat_14

/* ===========================================================
 *  Widgets
 * =========================================================== */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_OBJ        1   /* base object, always needed */
#define LV_USE_BTNMATRIX  0
#define LV_USE_CHART      0
#define LV_USE_TABLE      0
#define LV_USE_CANVAS     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   1
#define LV_USE_ROLLER     0
#define LV_USE_DROPDOWN   1
#define LV_USE_SLIDER     0

/* ===========================================================
 *  Layouts
 * =========================================================== */
#define LV_USE_FLEX  1
#define LV_USE_GRID  0

/* ===========================================================
 *  Logging
 * =========================================================== */
#define LV_USE_LOG  1
#if LV_USE_LOG
    #define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0   /* use custom handler below */
    /* Route through ESP_LOG — implement lv_esp_log_handler() in main.cpp */
    #define LV_LOG_CUSTOM_HANDLER  lv_esp_log_handler
#endif

/* ===========================================================
 *  File system, image decoders  —  all off
 * =========================================================== */
#define LV_USE_FS_STDIO   0
#define LV_USE_FS_POSIX   0
#define LV_USE_PNG        0
#define LV_USE_BMP        0
#define LV_USE_GIF        0
#define LV_USE_QRCODE     0

/* ===========================================================
 *  Animation
 * =========================================================== */
#define LV_USE_ANIM  1

/* ===========================================================
 *  Demos / examples  —  off
 * =========================================================== */
#define LV_USE_DEMO_WIDGETS      0
#define LV_USE_DEMO_BENCHMARK    0
#define LV_USE_DEMO_STRESS       0
#define LV_BUILD_EXAMPLES        0

#endif  /* LV_CONF_H */
#endif  /* Content enable */
