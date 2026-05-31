// =============================================================
//  ota_server.c — Web OTA server + HTTP server init
// =============================================================

#include "ota_server.h"
#include "wifi_manager.h"
#include "wifi_config.h"
#include "vss_web_handlers.h"
#include "mjpeg_stream.h"
#include "settings_page.h"
#include "status_page.h"
#include "dashboard_ui.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ota";

#ifndef OTA_VALID_TIMEOUT_MS
  #define OTA_VALID_TIMEOUT_MS  30000
#endif
#define OTA_WIFI_MODE_STA 1
#define OTA_WIFI_MODE_AP  2
#ifndef OTA_WIFI_MODE
  #define OTA_WIFI_MODE OTA_WIFI_MODE_STA
#endif

#if DISPLAY_STUB
static esp_err_t prv_view_handler(httpd_req_t *req)
{
    // Parse optional ?nav=home|settings|status query param
    char nav[16] = "home";
    char query[64] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16] = {};
        if (httpd_query_key_value(query, "nav", val, sizeof(val)) == ESP_OK)
            snprintf(nav, sizeof(nav), "%s", val);
    }

    char html[1024];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>EV Dashboard</title>"
        "<style>"
        "body{margin:0;background:#07090E;color:#eee;font-family:sans-serif}"
        ".nav{display:flex;background:#0B0F18;border-bottom:1px solid #161C26;padding:8px;gap:8px}"
        ".nav a{flex:1;text-align:center;padding:8px;background:#161C26;color:#4af;"
        "text-decoration:none;border-radius:4px;font-size:.85em}"
        ".nav a.active{background:#4af;color:#000}"
        ".view{width:100%%;height:calc(100vh - 52px)}"
        "img{width:100%%;height:100%%;object-fit:contain}"
        "</style></head><body>"
        "<div class='nav'>"
        "<a href='/view?nav=home' class='%s'>🏠 Home</a>"
        "<a href='/settings' target='_blank' class='%s'>⚙️ Settings</a>"
        "<a href='/status-page' target='_blank' class='%s'>📊 Status</a>"
        "<a href='/ota' target='_blank' style='background:#1a2a3a'>🔧 OTA</a>"
        "</div>"
        "<div class='view'><img src='http://ev-dashboard.local:81/stream' alt='EV Dashboard'></div>"
        "</body></html>",
        strcmp(nav,"home")==0     ? "active" : "",
        strcmp(nav,"settings")==0 ? "active" : "",
        strcmp(nav,"status")==0   ? "active" : ""
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}
#endif

// ── HTML UI ───────────────────────────────────────────────────────────────
static const char s_html[] =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>EV Dashboard OTA</title>"
"<style>"
"body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px;background:#111;color:#eee}"
"h1{color:#4af;margin-bottom:4px}p{color:#aaa;font-size:.9em;margin-top:0}"
"#drop{border:2px dashed #4af;border-radius:8px;padding:40px;text-align:center;"
"cursor:pointer;transition:background .2s}#drop.over{background:#1a2a3a}"
"input[type=file]{display:none}"
"button{background:#4af;color:#000;border:none;padding:10px 24px;"
"border-radius:4px;font-size:1em;cursor:pointer;margin-top:16px;width:100%}"
"button:disabled{background:#444;color:#888;cursor:not-allowed}"
"#status{margin-top:16px;padding:12px;border-radius:4px;display:none}"
".ok{background:#1a3a1a;color:#4f4}.err{background:#3a1a1a;color:#f44}"
"#progress{width:100%;height:8px;background:#222;border-radius:4px;margin-top:8px;display:none}"
"#bar{height:100%;width:0;background:#4af;border-radius:4px;transition:width .3s}"
"</style></head><body>"
"<div class='nav'><a href='/view'>🏠 Home</a><a href='/settings'>⚙️ Settings</a>"
"<a href='/status-page'>📊 Status</a><a href='/ota' class='active'>🔧 OTA</a></div>"
"<style>.nav{display:flex;background:#0B0F18;border-bottom:1px solid #333;padding:8px;gap:8px;margin:-40px -16px 16px}"
".nav a{flex:1;text-align:center;padding:8px;background:#1a2a3a;color:#4af;text-decoration:none;border-radius:4px;font-size:.85em}"
".nav a.active{background:#4af;color:#000}</style>"
"<h1>EV Dashboard OTA</h1>"
"<p id='ver'>Loading...</p>"
"<div style='display:flex;gap:8px;margin-bottom:16px'>"
"<a href='/view' style='flex:1;text-align:center;padding:10px;background:#1a2a3a;color:#4af;"
"text-decoration:none;border-radius:4px;font-size:.9em'>📺 Live View</a>"
"<a href='/view?nav=settings' style='flex:1;text-align:center;padding:10px;background:#1a2a3a;color:#4af;"
"text-decoration:none;border-radius:4px;font-size:.9em'>⚙️ Settings</a>"
"<a href='/view?nav=status' style='flex:1;text-align:center;padding:10px;background:#1a2a3a;color:#4af;"
"text-decoration:none;border-radius:4px;font-size:.9em'>📊 Status</a>"
"</div>"
"<div id='drop' onclick='document.getElementById(\"file\").click()'>"
"  Drop firmware.bin here<br><small>or click to browse</small>"
"  <input type='file' id='file' accept='.bin'>"
"</div>"
"<div id='progress'><div id='bar'></div></div>"
"<button id='btn' disabled>Flash Firmware</button>"
"<div id='status'></div>"
"<script>"
"var file=null;"
"fetch('/status').then(r=>r.json()).then(d=>{"
"  document.getElementById('ver').textContent="
"    'Running: '+d.version+' | Partition: '+d.partition+' | IP: '+d.ip;"
"});"
"var drop=document.getElementById('drop');"
"drop.addEventListener('dragover',function(e){e.preventDefault();drop.classList.add('over');});"
"drop.addEventListener('dragleave',function(){drop.classList.remove('over');});"
"drop.addEventListener('drop',function(e){"
"  e.preventDefault();drop.classList.remove('over');"
"  setFile(e.dataTransfer.files[0]);});"
"document.getElementById('file').addEventListener('change',function(e){"
"  setFile(e.target.files[0]);});"
"function setFile(f){"
"  file=f;"
"  drop.textContent=f.name+' ('+Math.round(f.size/1024)+' KB)';"
"  document.getElementById('btn').disabled=false;}"
"document.getElementById('btn').addEventListener('click',function(){"
"  if(!file)return;"
"  var btn=document.getElementById('btn');"
"  var prog=document.getElementById('progress');"
"  var bar=document.getElementById('bar');"
"  var st=document.getElementById('status');"
"  btn.disabled=true;btn.textContent='Flashing...';"
"  prog.style.display='block';"
"  var xhr=new XMLHttpRequest();"
"  xhr.open('POST','/update');"
"  xhr.setRequestHeader('Content-Type','application/octet-stream');"
"  xhr.upload.addEventListener('progress',function(e){"
"    if(e.lengthComputable){"
"      var pct=Math.round(e.loaded/e.total*100);"
"      bar.style.width=pct+'%';"
"      btn.textContent='Flashing... '+pct+'%';}});"
"  xhr.addEventListener('load',function(){"
"    st.style.display='block';"
"    if(xhr.status===200){"
"      st.className='ok';st.textContent='Success! Device rebooting...';"
"      btn.textContent='Done \u2014 reconnect in ~5s';"
"    }else{"
"      st.className='err';st.textContent='Error: '+xhr.responseText;"
"      btn.textContent='Flash Firmware';btn.disabled=false;}});"
"  xhr.addEventListener('error',function(){"
"    st.style.display='block';st.className='err';"
"    st.textContent='Upload failed \u2014 check connection';"
"    btn.textContent='Flash Firmware';btn.disabled=false;});"
"  xhr.send(file);});"
"</script></body></html>";

// ── HTTP handlers ─────────────────────────────────────────────────────────
static esp_err_t prv_get_ota(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t prv_get_root(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/view");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t prv_get_status(httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    char ip_str[16] = "0.0.0.0";
#if OTA_WIFI_MODE == OTA_WIFI_MODE_STA
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
#else
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
#endif
    if (netif) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(netif, &info) == ESP_OK)
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&info.ip));
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"version\":\"%s\",\"partition\":\"%s\",\"ip\":\"%s\"}",
        desc ? desc->version : "unknown",
        running ? running->label : "unknown",
        ip_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t prv_post_update(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update started — content_len=%d", req->content_len);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Writing to partition: %s", update_part->label);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES,
                                  &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA begin failed");
        return err;
    }

    // Use static buffer — keeps it off the httpd task stack
    static char buf[4096];
    int received  = 0;
    int remaining = req->content_len;

    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int len = httpd_req_recv(req, buf, to_read);
        if (len < 0) {
            if (len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "recv error %d", len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Receive error");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota_handle, buf, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA write failed");
            return err;
        }
        received  += len;
        remaining -= len;
        if (received % (128 * 1024) == 0)
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes", received,
                     req->content_len);
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA end failed");
        return err;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Set boot partition failed");
        return err;
    }

    ESP_LOGI(TAG, "OTA complete — %d bytes written — rebooting", received);
    httpd_resp_sendstr(req, "OK");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ── mDNS ─────────────────────────────────────────────────────────────────
static void prv_mdns_init(void)
{
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(OTA_MDNS_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("EV Dashboard OTA"));
    mdns_service_add(NULL, "_http", "_tcp", OTA_HTTP_PORT, NULL, 0);
    ESP_LOGI(TAG, "mDNS: http://%s.local/", OTA_MDNS_HOSTNAME);
}

// ── HTTP server ───────────────────────────────────────────────────────────

#if DISPLAY_STUB
static esp_err_t prv_nav_handler(httpd_req_t *req)
{
    char query[32] = {};
    char screen[16] = "home";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "screen", screen, sizeof(screen));

    dash_screen_t scr = DASH_SCREEN_HOME;
    if      (strcmp(screen, "settings") == 0) scr = DASH_SCREEN_SETTINGS;
    else if (strcmp(screen, "status")   == 0) scr = DASH_SCREEN_STATUS;

    dashboard_ui_set_screen(scr);

    httpd_resp_set_type(req, "application/json");
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"screen\":\"%s\"}", screen);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}
#endif

static void prv_httpd_start(void)
{
    httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
    cfg.server_port       = OTA_HTTP_PORT;
    cfg.max_uri_handlers  = 16;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.max_resp_headers  = 8;
    cfg.max_open_sockets  = 7;  // allow MJPEG + OTA simultaneously
    cfg.lru_purge_enable  = true;  // evict oldest connection if needed

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t root   = { .uri="/",       .method=HTTP_GET,  .handler=prv_get_root   };
    httpd_uri_t ota_pg = { .uri="/ota",    .method=HTTP_GET,  .handler=prv_get_ota    };
    httpd_uri_t status = { .uri="/status", .method=HTTP_GET,  .handler=prv_get_status };
    httpd_uri_t update = { .uri="/update", .method=HTTP_POST, .handler=prv_post_update,
                           .user_ctx=NULL };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ota_pg));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &update));
    vss_register_handlers(server);

#if DISPLAY_STUB
    // Stream runs on port 81 (separate server) — started below
    ESP_LOGI(TAG, "registering /view handler...");
    httpd_uri_t view = { .uri="/view", .method=HTTP_GET,
                         .handler=prv_view_handler };
    esp_err_t view_err = httpd_register_uri_handler(server, &view);
    ESP_LOGI(TAG, "/view registration result: 0x%x", view_err);
    ESP_ERROR_CHECK(view_err);
    ESP_LOGI(TAG, "MJPEG viewer: http://ev-dashboard.local/view");
    settings_page_register(server);
    status_page_register(server);
    mjpeg_server_start();  // stream on port 81
    httpd_uri_t nav = { .uri="/nav", .method=HTTP_GET,
                        .handler=prv_nav_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &nav));
#endif

    ESP_LOGI(TAG, "HTTP server started on port %d", OTA_HTTP_PORT);
}

// ── Rollback watchdog ─────────────────────────────────────────────────────
static void prv_rollback_watchdog(void *arg)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

    if (running == factory) {
        ESP_LOGI(TAG, "Running from factory — rollback watchdog inactive");
        vTaskDelete(NULL);
        return;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "OTA pending verify — %d ms to call ota_server_mark_valid()",
                 OTA_VALID_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(OTA_VALID_TIMEOUT_MS));
        if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGE(TAG, "mark_valid() not called — rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
    }
    vTaskDelete(NULL);
}

// ── Public API ────────────────────────────────────────────────────────────
void ota_server_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA image marked valid");
    } else if (err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mark_valid: %s", esp_err_to_name(err));
    }
}


esp_err_t ota_server_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

#if OTA_WIFI_MODE == OTA_WIFI_MODE_AP
    ESP_LOGI(TAG, "WiFi mode: AP  SSID=%s", OTA_AP_SSID);
    ESP_ERROR_CHECK(wifi_manager_start_ap());
#else
    ESP_LOGI(TAG, "WiFi mode: STA  SSID=%s", OTA_STA_SSID);
    ESP_ERROR_CHECK(wifi_manager_start_sta());
#endif

    prv_mdns_init();
    prv_httpd_start();

    xTaskCreate(prv_rollback_watchdog, "ota_wdog", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "OTA server ready");
    return ESP_OK;
}
