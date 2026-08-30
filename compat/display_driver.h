// SPDX-FileCopyrightText: 2026 Herb Peyerl
// SPDX-License-Identifier: BSD-3-Clause

// =============================================================
//  display_driver.h — Linux shim
//
//  Shadows the ESP-IDF display_driver.h (which pulls in esp_err.h
//  and esp_lcd_*). On Linux the display is a DRM device set up in
//  main_linux.c, so all the portable code needs from here is the
//  DISPLAY_TARGET_* constants that dashboard_layout.h switches on.
//  DISPLAY_TARGET itself is passed by CMake (-DDISPLAY_TARGET=2).
// =============================================================
#pragma once

#ifndef DISPLAY_STUB
  #define DISPLAY_STUB 0
#endif

#define DISPLAY_TARGET_TAB5       1
#define DISPLAY_TARGET_WAVESHARE  2
