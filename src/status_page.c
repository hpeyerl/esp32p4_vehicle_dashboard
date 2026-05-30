// =============================================================
//  status_page.c — ZombieVerter spot values status web page
//
//  GET /status-page  — HTML status page (auto-refreshes)
//  GET /api/status   — JSON: current spot values
//
//  Values are populated from:
//    1. g_dash struct (speed, SOC, power, temps etc.)
//    2. status_page_update() calls from CAN RX / SDO callbacks
//
//  The page auto-polls /api/status every 2 seconds.
// =============================================================

#include "status_page.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "can_signals.h"
// DashData is defined in dashboard_ui.h (C++) — use the getter
// to avoid mixing C/C++ in this C file.
extern float dash_get_speed(void);
extern float dash_get_soc(void);
extern float dash_get_power_kw(void);
extern float dash_get_pack_volts(void);
extern float dash_get_pack_amps(void);
extern float dash_get_inverter_temp(void);
extern float dash_get_motor_temp(void);
extern float dash_get_batt_temp(void);
extern float dash_get_aux_volts(void);
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "status";

// ── Spot value store ──────────────────────────────────────────
#define MAX_SPOT_VALUES  96
#define SPOT_NAME_LEN    24
#define SPOT_UNIT_LEN     8

typedef struct {
    char  name[SPOT_NAME_LEN];
    char  unit[SPOT_UNIT_LEN];
    float value;
    bool  valid;
} spot_value_t;

static spot_value_t     *s_spots  = NULL;
static uint8_t           s_count  = 0;
static SemaphoreHandle_t s_mutex  = NULL;

// ── Predefined spot value list (from ZombieVerter spot values) ─
static const struct { const char *name; const char *unit; } k_spot_defs[] = {
    // Drive
    {"opmode",      ""},
    {"speed",       "rpm"},
    {"Veh_Speed",   "kph"},
    {"torque",      "dig"},
    {"potnom",      "%"},
    {"dir",         ""},
    {"cruisespeed", "rpm"},
    {"cruisestt",   ""},
    // Power
    {"udc",         "V"},
    {"idc",         "A"},
    {"power",       "kW"},
    {"KWh",         "kWh"},
    {"AMPh",        "Ah"},
    // SOC / BMS
    {"SOC",         "%"},
    {"BMS_Vmin",    "V"},
    {"BMS_Vmax",    "V"},
    {"BMS_Tavg",    "°C"},
    {"BMS_Tmin",    "°C"},
    {"BMS_Tmax",    "°C"},
    {"BMS_ChargeLim","A"},
    {"BMS_MaxInput","kW"},
    {"BMS_MaxOutput","kW"},
    // Temps
    {"tmphs",       "°C"},
    {"tmpm",        "°C"},
    {"tmpaux",      "°C"},
    {"tmpheater",   "°C"},
    // Aux / 12V
    {"uaux",        "V"},
    {"U12V",        "V"},
    {"I12V",        "A"},
    // Charger
    {"hvChg",       ""},
    {"AC_Volts",    "V"},
    {"AC_Amps",     "A"},
    {"CCS_I",       "A"},
    {"CCS_V",       "V"},
    {"CCS_State",   "s"},
    // Status
    {"status",      ""},
    {"lasterr",     ""},
    {"chgtyp",      ""},
    {"CanAct",      ""},
    {"TorqDerate",  ""},
    // Digital inputs
    {"din_cruise",  ""},
    {"din_start",   ""},
    {"din_brake",   ""},
    {"din_forward", ""},
    {"din_reverse", ""},
    // System
    {"cpuload",     "%"},
    {"canctr",      "dig"},
    {"version",     ""},
    {NULL, NULL}
};

static spot_value_t *prv_find_or_create(const char *name)
{
    // Find existing
    for (uint8_t i = 0; i < s_count; i++) {
        if (strcmp(s_spots[i].name, name) == 0)
            return &s_spots[i];
    }
    // Create new
    if (s_count >= MAX_SPOT_VALUES) return NULL;
    spot_value_t *s = &s_spots[s_count++];
    snprintf(s->name, SPOT_NAME_LEN, "%s", name);
    s->unit[0] = '\0';
    s->value   = 0.0f;
    s->valid   = false;
    return s;
}

void status_page_update(const char *name, float value, const char *unit)
{
    if (!s_spots || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    spot_value_t *s = prv_find_or_create(name);
    if (s) {
        s->value = value;
        s->valid = true;
        if (unit) snprintf(s->unit, SPOT_UNIT_LEN, "%s", unit);
    }
    xSemaphoreGive(s_mutex);
}

// Populate from g_dash struct
static void prv_sync_from_dash(void)
{
    status_page_update("Veh_Speed",  dash_get_speed(),        "kph");
    status_page_update("SOC",        dash_get_soc(),          "%");
    status_page_update("power",      dash_get_power_kw(),     "kW");
    status_page_update("udc",        dash_get_pack_volts(),   "V");
    status_page_update("idc",        dash_get_pack_amps(),    "A");
    status_page_update("tmphs",      dash_get_inverter_temp(),"°C");
    status_page_update("tmpm",       dash_get_motor_temp(),   "°C");
    status_page_update("tmpaux",     dash_get_batt_temp(),    "°C");
    status_page_update("uaux",       dash_get_aux_volts(),    "V");
}

// ── /api/status handler ───────────────────────────────────────
static esp_err_t prv_api_status_handler(httpd_req_t *req)
{
    prv_sync_from_dash();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    httpd_resp_send_chunk(req, "[", 1);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    char buf[96];
    for (uint8_t i = 0; i < s_count; i++) {
        spot_value_t *s = &s_spots[i];
        int n = snprintf(buf, sizeof(buf),
            "%s{\"name\":\"%s\",\"value\":%.4g,\"unit\":\"%s\"}",
            i > 0 ? "," : "",
            s->name, s->value, s->unit);
        httpd_resp_send_chunk(req, buf, n);
    }
    xSemaphoreGive(s_mutex);

    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ── /status-page HTML ─────────────────────────────────────────
static const char s_status_html[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>EV Status</title>"
"<style>"
"*{box-sizing:border-box}body{margin:0;background:#07090E;color:#C8D6E5;"
"font-family:sans-serif;font-size:13px}"
".nav{display:flex;background:#0B0F18;border-bottom:1px solid #161C26;"
"padding:8px;gap:8px}"
".nav a{flex:1;text-align:center;padding:8px;background:#161C26;color:#4af;"
"text-decoration:none;border-radius:4px;font-size:.85em}"
".nav a.active{background:#4af;color:#000}"
".toolbar{display:flex;align-items:center;justify-content:space-between;"
"padding:8px 12px;background:#0B0F18;border-bottom:1px solid #161C26;"
"color:#4A5A70;font-size:12px}"
"#vals{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));"
"gap:4px;padding:8px}"
".row{display:flex;justify-content:space-between;align-items:center;"
"padding:5px 8px;background:#0B0F18;border-radius:4px}"
".rname{color:#7A8A9A;font-size:12px}"
".rval{font-weight:bold;color:#C8D6E5}"
".runit{color:#4A5A70;font-size:11px;margin-left:4px}"
".zero .rval{color:#2D3A50}"
"</style></head><body>"
"<div class='nav'>"
"<a href='/view'>🏠 Home</a>"
"<a href='/settings' target='_blank'>⚙️ Settings</a>"
"<a href='/status-page' class='active'>📊 Status</a>"
"<a href='/ota' target='_blank'>🔧 OTA</a>"
"</div>"
"<div class='toolbar'>"
"<span id='ts'>–</span>"
"<span>auto-refresh 2s</span>"
"</div>"
"<div id='vals'><div style='padding:40px;text-align:center;color:#4A5A70'>"
"Loading...</div></div>"
"<script>"
"function refresh(){"
"fetch('/api/status').then(r=>r.json()).then(d=>{"
"var h='';"
"d.forEach(s=>{"
"var z=s.value===0?\" zero\":'';"
"h+=\"<div class='row\"+z+\"'>\""
"+\"<span class='rname'>\"+s.name+\"</span>\""
"+\"<span><span class='rval'>\"+s.value+\"</span>\""
"+\"<span class='runit'>\"+s.unit+\"</span></span>\""
"+\"</div>\";});"
"document.getElementById('vals').innerHTML=h;"
"document.getElementById('ts').textContent="
"new Date().toLocaleTimeString();"
"}).catch(()=>{});}"
"refresh();"
"setInterval(refresh,2000);"
"</script></body></html>";

static esp_err_t prv_status_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, s_status_html);
    return ESP_OK;
}

// ── Register ──────────────────────────────────────────────────
esp_err_t status_page_register(httpd_handle_t server)
{
    s_spots = (spot_value_t *)heap_caps_calloc(
        MAX_SPOT_VALUES, sizeof(spot_value_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_spots) {
        ESP_LOGE(TAG, "Failed to allocate spot values cache");
        return ESP_ERR_NO_MEM;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        heap_caps_free(s_spots);
        s_spots = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Pre-populate with known spot value names
    for (int i = 0; k_spot_defs[i].name != NULL; i++) {
        spot_value_t *s = prv_find_or_create(k_spot_defs[i].name);
        if (s) snprintf(s->unit, SPOT_UNIT_LEN, "%s", k_spot_defs[i].unit);
    }

    httpd_uri_t pg  = { .uri="/status-page", .method=HTTP_GET,
                        .handler=prv_status_page_handler };
    httpd_uri_t api = { .uri="/api/status",  .method=HTTP_GET,
                        .handler=prv_api_status_handler };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &pg));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api));

    ESP_LOGI(TAG, "Status page registered at /status-page");
    return ESP_OK;
}
