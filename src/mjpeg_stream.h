// =============================================================
//  mjpeg_stream.h — MJPEG HTTP stream handler
//
//  Register with httpd:
//    httpd_uri_t stream_uri = {
//        .uri      = "/stream",
//        .method   = HTTP_GET,
//        .handler  = mjpeg_stream_handler,
//    };
//    httpd_register_uri_handler(server, &stream_uri);
// =============================================================

#pragma once
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mjpeg_stream_handler(httpd_req_t *req);
esp_err_t mjpeg_server_start(void);

#ifdef __cplusplus
}
#endif
