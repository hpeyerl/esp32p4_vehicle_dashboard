// SPDX-FileCopyrightText: 2026 Herb Peyerl
// SPDX-License-Identifier: BSD-3-Clause

// =============================================================
//  esp_err.h — minimal Linux shim
//
//  Some reused headers that sit next to the shared .cpp files
//  (e.g. src/wifi_manager.h) get picked up by quote-include before
//  our compat shims and pull in esp_err.h. This provides just
//  enough for them to compile; the actual function impls the UI
//  calls are supplied by linux/src/stubs.c.
// =============================================================
#pragma once
#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK    0
#define ESP_FAIL  -1

static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "ESP_ERR"; }
