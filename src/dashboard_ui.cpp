// dashboard_ui.cpp
// Pulls in the dashboard_ui.h implementation.
// The header uses #ifdef DASHBOARD_UI_IMPL to gate the function bodies
// so they only get compiled once, here, rather than in every TU that
// includes the header.

#define DASHBOARD_UI_IMPL
#include "dashboard_ui.h"
