// =============================================================
//  status_page.h — ZombieVerter spot values status web page
// =============================================================
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call after httpd_start()
esp_err_t status_page_register(httpd_handle_t server);

// Update a spot value (call from CAN RX or SDO callback)
// name: param name string, value: float value, unit: unit string
void status_page_update(const char *name, float value, const char *unit);

#ifdef __cplusplus
}
#endif
