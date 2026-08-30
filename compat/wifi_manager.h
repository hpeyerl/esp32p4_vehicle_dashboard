// SPDX-FileCopyrightText: 2026 Herb Peyerl
// SPDX-License-Identifier: BSD-3-Clause

// =============================================================
//  wifi_manager.h — Linux shim
//
//  Shadows the ESP-IDF wifi_manager.h (which pulls in esp_err.h)
//  so the portable UI compiles on Linux. Only the one symbol the
//  UI actually references is declared here.
// =============================================================
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
