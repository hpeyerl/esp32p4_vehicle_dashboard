// =============================================================
//  http_api.h — tiny HTTP control/status API for the Linux dashboard
// =============================================================
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Start a background HTTP server thread on the given TCP port.
//   GET /api/status          -> live g_dash values (JSON)
//   GET /api/nav?screen=NAME  -> home|settings|status|vcu|bms
void http_api_start(int port);

#ifdef __cplusplus
}
#endif
