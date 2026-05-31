// =============================================================
//  wifi_manager.h — WiFi STA/AP init and event handling
// =============================================================
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_start_sta(void);
esp_err_t wifi_manager_start_ap(void);

#ifdef __cplusplus
}
#endif
