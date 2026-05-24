// =============================================================
//  ota_server.c — Web-based OTA firmware update server
//
//  Provides:
//    GET  /          — browser UI (drag-and-drop or file picker)
//    POST /update    — raw binary upload endpoint (curl-friendly)
//    GET  /status    — JSON: current fw version, partition, IP
//
//  Rollback safety:
//    New firmware must call ota_server_mark_valid() (which calls
//    esp_ota_mark_app_valid_cancel_rollback) or the bootloader
//    reverts to the previous image on the next reboot.
//    A watchdog task enforces this — if mark_valid() hasn't been
//    called within OTA_VALID_TIMEOUT_MS of boot, it reboots.
// =============================================================

#include "ota_server.h"
#include "wifi_config.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ota";

#ifndef OTA_VALID_TIMEOUT_MS
  #define OTA_VALID_TIMEOUT_MS  30000
#endif

// Must define mode constants before using them in #if
#define OTA_WIFI_MODE_STA 1
#define OTA_WIFI_MODE_AP  2
#ifndef OTA_WIFI_MODE
  #define OTA_WIFI_MODE OTA_WIFI_MODE_STA
#endif

// ── WiFi event handling ───────────────────────────────────────────────────
static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry = 0;
#define WIFI_MAX_RETRY 10

static void prv_wifi_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGW(TAG, "WiFi retry %d/%d", s_retry, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected  IP=" IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "OTA UI:  http://" IPSTR "/", IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "OTA URL: http://" IPSTR "/update", IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "mDNS:    http://%s.local/", OTA_MDNS_HOSTNAME);
        s_retry = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        // MACSTR can't be used inside ESP_LOGI string concatenation — expand manually
        ESP_LOGI(TAG, "AP: client connected  MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                 e->mac[0], e->mac[1], e->mac[2],
                 e->mac[3], e->mac[4], e->mac[5]);
    }
}

static esp_err_t prv_wifi_sta_init(void)
{
    s_wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h_any, h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, prv_wifi_event_handler, NULL, &h_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, prv_wifi_event_handler, NULL, &h_ip));

    wifi_config_t wc = {};
    strlcpy((char *)wc.sta.ssid,     OTA_STA_SSID,     sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, OTA_STA_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    ESP_LOGE(TAG, "WiFi STA connect failed");
    return ESP_FAIL;
}

static esp_err_t prv_wifi_ap_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, prv_wifi_event_handler, NULL, NULL));

    wifi_config_t wc = {};
    strlcpy((char *)wc.ap.ssid,     OTA_AP_SSID,     sizeof(wc.ap.ssid));
    strlcpy((char *)wc.ap.password, OTA_AP_PASSWORD, sizeof(wc.ap.password));
    wc.ap.channel        = OTA_AP_CHANNEL;
    wc.ap.max_connection = OTA_AP_MAX_CONN;
    wc.ap.authmode       = strlen(OTA_AP_PASSWORD) ? WIFI_AUTH_WPA2_PSK
                                                    : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started  SSID=%s", OTA_AP_SSID);
    ESP_LOGI(TAG, "OTA UI:  http://192.168.4.1/");
    ESP_LOGI(TAG, "OTA URL: http://192.168.4.1/update");
    return ESP_OK;
}

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
"<h1>EV Dashboard OTA</h1>"
"<p id='ver'>Loading...</p>"
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
static esp_err_t prv_get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
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

    char buf[1024];
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
        ESP_LOGD(TAG, "OTA progress: %d / %d bytes", received,
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
static void prv_httpd_start(void)
{
    httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
    cfg.server_port       = OTA_HTTP_PORT;
    cfg.max_uri_handlers  = 4;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.max_resp_headers  = 8;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t root   = { .uri="/",       .method=HTTP_GET,  .handler=prv_get_root   };
    httpd_uri_t status = { .uri="/status", .method=HTTP_GET,  .handler=prv_get_status };
    httpd_uri_t update = { .uri="/update", .method=HTTP_POST, .handler=prv_post_update,
                           .user_ctx=NULL };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &update));

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
    ESP_ERROR_CHECK(prv_wifi_ap_init());
#else
    ESP_LOGI(TAG, "WiFi mode: STA  SSID=%s", OTA_STA_SSID);
    if (prv_wifi_sta_init() != ESP_OK) {
        ESP_LOGW(TAG, "STA failed — falling back to AP");
        ESP_ERROR_CHECK(prv_wifi_ap_init());
    }
#endif

    prv_mdns_init();
    prv_httpd_start();

    xTaskCreate(prv_rollback_watchdog, "ota_wdog", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "OTA server ready");
    return ESP_OK;
}
