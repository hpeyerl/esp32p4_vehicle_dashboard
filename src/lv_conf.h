/**
 * lv_conf.h
 * LVGL v9 configuration for EV Dashboard
 * Place in: include/lv_conf.h
 *
 * PlatformIO's lvgl library looks for this file via
 * -DLVGL_CONF_INCLUDE_SIMPLE in build_flags, which makes
 * LVGL search the include path for "lv_conf.h" directly.
 */

#if 1  /* Set to 1 to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* =============================================================
 *  Colour depth
 *  16 = RGB565 (most DSI panels default; saves PSRAM bandwidth)
 *  32 = ARGB8888 (richer gradients, costs 2× RAM)
 * ============================================================= */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color — set to 1 if colours look wrong */
#define LV_COLOR_16_SWAP 0

/* =============================================================
 *  Memory
 * ============================================================= */
/* Use external PSRAM for LVGL heap (ESP32-P4 has 32 MB PSRAM) */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
    #define LV_MEM_CUSTOM_INCLUDE   <esp_heap_caps.h>
    #define LV_MEM_CUSTOM_ALLOC(s)  heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    #define LV_MEM_CUSTOM_FREE      heap_caps_free
    #define LV_MEM_CUSTOM_REALLOC   heap_caps_realloc
#endif

/* =============================================================
 *  Hardware timers
 * ============================================================= */
/* lv_tick_inc() called from our esp_timer ISR — LVGL does not
   need to provide its own tick source */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE  <esp_timer.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)(esp_timer_get_time() / 1000ULL))
#endif

/* =============================================================
 *  Display
 * ============================================================= */
#define LV_HOR_RES_MAX  1280  /* Tab5: 1280x720 */
#define LV_VER_RES_MAX   800

/* =============================================================
 *  Fonts
 *  Enable the sizes used in dashboard_ui.h
 * ============================================================= */
#define LV_FONT_MONTSERRAT_10  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_40  1
#define LV_FONT_MONTSERRAT_48  1
#define LV_FONT_MONTSERRAT_72  1

/* Default font for labels that don't set their own */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* =============================================================
 *  Widgets — enable everything we use
 * ============================================================= */
#define LV_USE_ARC      1
#define LV_USE_BAR      1
#define LV_USE_LABEL    1
#define LV_USE_OBJ      1
#define LV_USE_BTNMATRIX 0   /* not used */
#define LV_USE_CHART    0   /* not used */
#define LV_USE_TABLE    0   /* not used */

/* =============================================================
 *  Logging
 * ============================================================= */
#define LV_USE_LOG 1
#if LV_USE_LOG
    /* LV_LOG_LEVEL_TRACE/INFO/WARN/ERROR/USER/NONE */
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    /* Route LVGL logs through ESP_LOG */
    #define LV_LOG_PRINTF 0
    #define LV_LOG_CUSTOM_HANDLER lv_esp_log_handler
#endif

/* =============================================================
 *  Performance
 * ============================================================= */
/* Shadow + blur are expensive — disable for 30 fps on DSI */
#define LV_DRAW_SW_SHADOW_CACHE_SIZE  0
#define LV_USE_DRAW_SW_COMPLEX        0

/* =============================================================
 *  Feature flags (keep minimal for firmware size)
 * ============================================================= */
#define LV_USE_ANIMATION  1
#define LV_USE_FLEX       1
#define LV_USE_GRID       0
#define LV_USE_FS_STDIO   0
#define LV_USE_PNG        0
#define LV_USE_BMP        0
#define LV_USE_SJPG       0
#define LV_USE_GIF        0
#define LV_USE_QRCODE     0
#define LV_USE_MONKEY     0
#define LV_USE_DEMO_WIDGETS 0

#endif /* LV_CONF_H */
#endif /* Content enable */
