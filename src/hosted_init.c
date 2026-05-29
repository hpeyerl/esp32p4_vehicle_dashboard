// =============================================================
//  hosted_init.c — ESP-Hosted initialization wrapper
//
//  The IDF CMake build (with IDF_CMAKE_BUILD=1) provides the
//  real implementation. The SCons build provides a WEAK stub
//  so the linker always prefers the IDF CMake strong definition.
// =============================================================

#ifdef IDF_CMAKE_BUILD
// ── Real implementation (IDF CMake only) ─────────────────────
#include "esp_hosted.h"
#include "esp_log.h"

static const char *TAG = "hosted";

esp_err_t hosted_transport_init(void)
{
    ESP_LOGI(TAG, "initializing ESP-Hosted SDIO transport to C6");
    return esp_hosted_init();
}

#else
// ── Weak stub (SCons only) ────────────────────────────────────
// __attribute__((weak)) ensures the IDF CMake strong definition
// wins at link time when both objects are present.
#include "esp_err.h"
__attribute__((weak)) esp_err_t hosted_transport_init(void)
{
    return ESP_OK;
}
#endif
