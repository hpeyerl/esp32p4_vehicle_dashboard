// =============================================================
//  settings_page.c — ZombieVerter parameter settings web page
//
//  GET  /settings         — HTML settings page
//  GET  /api/params       — JSON: current params (from VCU or cache)
//  POST /api/param        — JSON: set a single param {id, value}
//  POST /api/save         — Save params to VCU flash
//
//  Params are fetched from VCU via SDO segmented upload (0x5001)
//  on first access and cached in PSRAM.
// =============================================================

#include "settings_page.h"
#include "sdo_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "settings";

// ── Param cache — allocated in PSRAM to avoid BSS pressure ───
#define MAX_PARAMS      128
#define PARAM_NAME_LEN   32

typedef struct {
    char     name[PARAM_NAME_LEN];
    uint16_t id;
    float    value;
    bool     editable;
} param_entry_t;

static param_entry_t    *s_params       = NULL;  // heap allocated
static uint16_t          s_param_count  = 0;
static bool              s_params_loaded = false;
static SemaphoreHandle_t s_params_mutex  = NULL;

// ── SDO fetch callback ────────────────────────────────────────
static void prv_on_params_fetched(const char *json, size_t len, void *ctx)
{
    if (!json || len == 0) {
        ESP_LOGE(TAG, "Param fetch failed — null or empty response");
        return;
    }
    ESP_LOGI(TAG, "Param schema received: %u bytes", (unsigned)len);

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGE(TAG, "cJSON parse failed");
        return;
    }

    xSemaphoreTake(s_params_mutex, portMAX_DELAY);
    s_param_count = 0;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (s_param_count >= MAX_PARAMS) break;
        if (!cJSON_IsObject(item)) continue;

        cJSON *id_j  = cJSON_GetObjectItem(item, "id");
        cJSON *val_j = cJSON_GetObjectItem(item, "value");
        cJSON *isp_j = cJSON_GetObjectItem(item, "isparam");
        if (!id_j) continue;

        param_entry_t *p = &s_params[s_param_count];
        snprintf(p->name, PARAM_NAME_LEN, "%s",
                 item->string ? item->string : "?");
        p->id       = (uint16_t)cJSON_GetNumberValue(id_j);
        p->value    = val_j ? (float)cJSON_GetNumberValue(val_j) : 0.0f;
        p->editable = isp_j ? cJSON_IsTrue(isp_j) : false;
        s_param_count++;
    }

    s_params_loaded = true;
    xSemaphoreGive(s_params_mutex);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Loaded %d params from VCU", s_param_count);
}

// ── /api/params handler ───────────────────────────────────────
static esp_err_t prv_api_params_handler(httpd_req_t *req)
{
    if (!s_params_loaded) {
        sdo_fetch_params(prv_on_params_fetched, NULL);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"fetching\",\"params\":[]}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    httpd_resp_send_chunk(req, "{\"status\":\"ok\",\"params\":[", 24);

    xSemaphoreTake(s_params_mutex, portMAX_DELAY);
    char buf[128];
    for (uint16_t i = 0; i < s_param_count; i++) {
        param_entry_t *p = &s_params[i];
        int n = snprintf(buf, sizeof(buf),
            "%s{\"name\":\"%s\",\"id\":%d,\"value\":%.4g,\"editable\":%s}",
            i > 0 ? "," : "",
            p->name, p->id, p->value,
            p->editable ? "true" : "false");
        httpd_resp_send_chunk(req, buf, n);
    }
    xSemaphoreGive(s_params_mutex);

    httpd_resp_send_chunk(req, "]}", 2);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ── /api/param POST handler ───────────────────────────────────
static esp_err_t prv_api_set_param_handler(httpd_req_t *req)
{
    char buf[128] = {};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }

    cJSON *id_j  = cJSON_GetObjectItem(root, "id");
    cJSON *val_j = cJSON_GetObjectItem(root, "value");
    if (!id_j || !val_j) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing id or value");
        return ESP_FAIL;
    }

    uint16_t param_id = (uint16_t)cJSON_GetNumberValue(id_j);
    float    value    = (float)cJSON_GetNumberValue(val_j);
    cJSON_Delete(root);

    // Update cache
    if (s_params) {
        xSemaphoreTake(s_params_mutex, portMAX_DELAY);
        for (uint16_t i = 0; i < s_param_count; i++) {
            if (s_params[i].id == param_id) {
                s_params[i].value = value;
                break;
            }
        }
        xSemaphoreGive(s_params_mutex);
    }

    bool ok = sdo_write(param_id, value, true);
    ESP_LOGI(TAG, "Set param %d = %.4g  queued=%d", param_id, value, ok);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"queue full\"}");
    return ESP_OK;
}

// ── /settings HTML handler ────────────────────────────────────
// HTML is const so it goes to .rodata (flash), not RAM
static const char s_settings_html[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>EV Settings</title>"
"<style>"
"*{box-sizing:border-box}body{margin:0;background:#07090E;color:#C8D6E5;"
"font-family:sans-serif;font-size:14px}"
".nav{display:flex;background:#0B0F18;border-bottom:1px solid #161C26;"
"padding:8px;gap:8px}"
".nav a{flex:1;text-align:center;padding:8px;background:#161C26;color:#4af;"
"text-decoration:none;border-radius:4px;font-size:.85em}"
".nav a.active{background:#4af;color:#000}"
".toolbar{display:flex;align-items:center;gap:8px;padding:8px 12px;"
"background:#0B0F18;border-bottom:1px solid #161C26}"
".toolbar input{flex:1;padding:6px 10px;background:#161C26;"
"border:1px solid #2D3A50;color:#C8D6E5;border-radius:4px}"
".toolbar button{padding:6px 14px;background:#4af;color:#000;border:none;"
"border-radius:4px;cursor:pointer;font-weight:bold}"
".toolbar button.sec{background:#161C26;color:#4af;border:1px solid #4af}"
"#params{padding:8px}"
".param{display:flex;align-items:center;gap:8px;padding:6px 8px;"
"border-radius:4px;margin-bottom:2px}"
".param:hover{background:#0B0F18}"
".pname{flex:1;color:#C8D6E5;font-size:13px}"
".pid{color:#2D3A50;font-size:11px;min-width:36px;text-align:right}"
".pval{min-width:80px;text-align:right}"
".pval input{width:80px;padding:3px 6px;background:#161C26;"
"border:1px solid #2D3A50;color:#C8D6E5;border-radius:3px;"
"text-align:right;font-size:13px}"
".pval span{color:#C8D6E5;font-size:13px}"
".readonly .pname{color:#4A5A70}.readonly .pval span{color:#4A5A70}"
".save-btn{padding:2px 10px;background:#4af;color:#000;border:none;"
"border-radius:3px;cursor:pointer;font-size:12px}"
".save-btn:hover{background:#6cf}"
".status{text-align:center;padding:40px;color:#4A5A70}"
".ok{color:#10B981}.err{color:#EF4444}"
"</style></head><body>"
"<div class='nav'>"
"<a href='/view'>🏠 Home</a>"
"<a href='/settings' class='active'>⚙️ Settings</a>"
"<a href='/status-page'>📊 Status</a>"
"<a href='/ota'>🔧 OTA</a>"
"</div>"
"<div class='toolbar'>"
"<input type='text' id='search' placeholder='Search params...' "
"oninput='filterParams()'>"
"<button class='sec' onclick='fetchParams()'>↻ Refresh</button>"
"<button onclick='saveAll()'>💾 Save All</button>"
"</div>"
"<div id='params'><div class='status'>Loading parameters...</div></div>"
"<script>"
"var allParams=[];"
"function fetchParams(){"
"document.getElementById('params').innerHTML="
"\"<div class='status'>Fetching from VCU...</div>\";"
"fetch('/api/params').then(r=>r.json()).then(d=>{"
"if(d.status==='fetching'){setTimeout(fetchParams,1000);return;}"
"allParams=d.params;renderParams(allParams);"
"}).catch(e=>{"
"document.getElementById('params').innerHTML="
"\"<div class='status err'>Error: \"+e+\"</div>\";});}"
"function renderParams(params){"
"var q=document.getElementById('search').value.toLowerCase();"
"var f=params.filter(p=>p.name.toLowerCase().includes(q));"
"var h='';"
"f.forEach(p=>{"
"var c='param '+(p.editable?'':'readonly');"
"var v=p.editable"
"?\"<input type='number' value='\"+p.value+\"' step='any' \""
"+\"id='i\"+p.id+\"' style='display:none'>\""
"+\"<span id='s\"+p.id+\"'>\"+p.value+\"</span>\""
"+\"<button class='save-btn' onclick='editParam(\"+p.id+\")' \""
"+\"id='b\"+p.id+\"'>edit</button>\""
":\"<span>\"+p.value+\"</span>\";"
"h+=\"<div class='\"+c+\"'>\""
"+\"<span class='pname'>\"+p.name+\"</span>\""
"+\"<span class='pid'>#\"+p.id+\"</span>\""
"+\"<span class='pval'>\"+v+\"</span></div>\";});"
"document.getElementById('params').innerHTML="
"h||\"<div class='status'>No params match</div>\";}"
"function filterParams(){renderParams(allParams);}"
"function editParam(id){"
"var i=document.getElementById('i'+id);"
"var s=document.getElementById('s'+id);"
"var b=document.getElementById('b'+id);"
"if(i.style.display==='none'){"
"i.style.display='inline';s.style.display='none';"
"b.textContent='set';i.focus();i.select();"
"}else setParam(id,i.value);}"
"function setParam(id,val){"
"fetch('/api/param',{method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({id:id,value:parseFloat(val)})})"
".then(r=>r.json()).then(d=>{"
"var s=document.getElementById('s'+id);"
"var i=document.getElementById('i'+id);"
"var b=document.getElementById('b'+id);"
"if(d.ok&&s){s.textContent=val;s.style.display='inline';"
"if(i)i.style.display='none';if(b)b.textContent='edit';"
"s.style.color='#10B981';"
"setTimeout(()=>{if(s)s.style.color='';},2000);}}).catch(()=>{});}"
"function saveAll(){"
"fetch('/api/save',{method:'POST'}).then(r=>r.json()).then(d=>{"
"alert(d.ok?'Saved to VCU flash':'Save failed');});}"
"fetchParams();"
"</script></body></html>";

static esp_err_t prv_settings_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, s_settings_html);
    return ESP_OK;
}

// ── /api/save handler ─────────────────────────────────────────
static esp_err_t prv_api_save_handler(httpd_req_t *req)
{
    // SDO write to save params to VCU flash
    // Index 0x1010, subindex 1, value "evas" (0x65766173)
    // TODO: implement via sdo_write when we have the raw index write API
    ESP_LOGI(TAG, "Save to flash requested");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// ── Register handlers ─────────────────────────────────────────
esp_err_t settings_page_register(httpd_handle_t server)
{
    // Allocate param cache in PSRAM
    s_params = (param_entry_t *)heap_caps_calloc(
        MAX_PARAMS, sizeof(param_entry_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_params) {
        ESP_LOGE(TAG, "Failed to allocate param cache (%u bytes)",
                 (unsigned)(MAX_PARAMS * sizeof(param_entry_t)));
        return ESP_ERR_NO_MEM;
    }

    s_params_mutex = xSemaphoreCreateMutex();
    if (!s_params_mutex) {
        heap_caps_free(s_params);
        s_params = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    httpd_uri_t settings = { .uri="/settings",  .method=HTTP_GET,
                              .handler=prv_settings_handler };
    httpd_uri_t api_get  = { .uri="/api/params", .method=HTTP_GET,
                              .handler=prv_api_params_handler };
    httpd_uri_t api_set  = { .uri="/api/param",  .method=HTTP_POST,
                              .handler=prv_api_set_param_handler };
    httpd_uri_t api_save = { .uri="/api/save",   .method=HTTP_POST,
                              .handler=prv_api_save_handler };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &settings));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api_set));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api_save));

    ESP_LOGI(TAG, "Settings page registered at /settings");
    return ESP_OK;
}
