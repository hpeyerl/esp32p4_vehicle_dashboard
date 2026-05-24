// vss_web_handlers.h
#pragma once
#include "esp_http_server.h"
#ifdef __cplusplus
extern "C" {
#endif
void vss_register_handlers(httpd_handle_t server);
#ifdef __cplusplus
}
#endif
