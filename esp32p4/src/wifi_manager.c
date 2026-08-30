// =============================================================
//  wifi_manager.c — WiFi STA/AP init and event handling
// =============================================================

#include "wifi_manager.h"
#include "wifi_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#ifdef IDF_CMAKE_BUILD
extern esp_err_t esp_hosted_init(void);
#endif

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_eg;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry = 0;
#define WIFI_MAX_RETRY 10

// ── WiFi event handler ────────────────────────────────────────
static void prv_wifi_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGD(TAG, "WiFi STA started — connecting");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_eg, WIFI_CONNECTED_BIT);
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
        ESP_LOGI(TAG, "mDNS:    http://%s.local/", OTA_MDNS_HOSTNAME);
        s_retry = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "AP: client connected  MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                 e->mac[0], e->mac[1], e->mac[2],
                 e->mac[3], e->mac[4], e->mac[5]);
    }
}

// ── Common init ───────────────────────────────────────────────
static esp_err_t prv_wifi_common_init(void)
{
#ifdef IDF_CMAKE_BUILD
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init failed");
#endif
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return err;
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────
esp_err_t wifi_manager_start_sta(void)
{
    s_wifi_eg = xEventGroupCreate();
    ESP_RETURN_ON_ERROR(prv_wifi_common_init(), TAG, "wifi common init failed");
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
    ESP_LOGI(TAG, "WiFi STA connecting in background...");
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    ESP_RETURN_ON_ERROR(prv_wifi_common_init(), TAG, "wifi common init failed");
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(err);

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
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    if (!s_wifi_eg) return false;
    return (xEventGroupGetBits(s_wifi_eg) & WIFI_CONNECTED_BIT) != 0;
}
