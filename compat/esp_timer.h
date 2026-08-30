// SPDX-FileCopyrightText: 2026 Herb Peyerl
// SPDX-License-Identifier: BSD-3-Clause

// =============================================================
//  esp_timer.h — Linux shim for the ESP-IDF timer API
//
//  Lets the portable UI code (dashboard_ui.cpp) call
//  esp_timer_get_time() unchanged on Linux. Returns microseconds
//  from a monotonic clock, matching ESP-IDF semantics.
// =============================================================
#pragma once
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

#ifdef __cplusplus
}
#endif
