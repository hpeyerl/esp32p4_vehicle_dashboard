// =============================================================
//  dashboard_layout.h — Layout selector
//  Include this in dashboard_ui.cpp instead of hardcoded values.
//
//  DISPLAY_TARGET is set in platformio.ini build_flags:
//    -DDISPLAY_TARGET=DISPLAY_TARGET_WAVESHARE  (2)
//    -DDISPLAY_TARGET=DISPLAY_TARGET_TAB5       (1)
//    -DDISPLAY_TARGET=DISPLAY_TARGET_STUB       (0, default)
// =============================================================
#pragma once
#include "display_driver.h"   // for DISPLAY_TARGET_* constants

#if DISPLAY_TARGET == DISPLAY_TARGET_TAB5
  #include "layout_tab5.h"
#elif DISPLAY_TARGET == DISPLAY_TARGET_WAVESHARE
  #include "layout_waveshare.h"
#else
  #include "layout_stub.h"
#endif
