// =============================================================
//  bms_http.h — poll the BMW i3 BMS web API (Linux only)
// =============================================================
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fetch http://$BMS_HOST/api/data and fill g_bms. Rate-limited to ~2s
// internally; only actually fetches when `active` is true (i.e. the BMS
// screen is on). Blocking, but bounded (~500ms timeout). Host defaults to
// "i3bms.beer.org", override with the BMS_HOST env var.
void bms_http_poll(uint32_t now_ms, bool active);

#ifdef __cplusplus
}
#endif
