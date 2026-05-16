// units.h — compile-time unit selection
// Set -DUNITS_METRIC=1 for km/km/h, omit or =0 for mi/mph

#pragma once

#if UNITS_METRIC

  #define UNITS_SPEED_LABEL   "km/h"
  #define UNITS_DIST_LABEL    "km"
  #define UNITS_EFF_LABEL     "km/kWh"

  // Conversion from internal SI values
  // Internal: speed in MPH, distance in miles (from CAN)
  // We convert at display time
  #define SPEED_TO_DISPLAY(mph)   ((mph) * 1.60934f)
  #define DIST_TO_DISPLAY(mi)     ((mi)  * 1.60934f)
  #define EFF_TO_DISPLAY(mi_kwh)  ((mi_kwh) * 1.60934f)

#else

  #define UNITS_SPEED_LABEL   "MPH"
  #define UNITS_DIST_LABEL    "mi"
  #define UNITS_EFF_LABEL     "mi/kWh"

  #define SPEED_TO_DISPLAY(mph)   (mph)
  #define DIST_TO_DISPLAY(mi)     (mi)
  #define EFF_TO_DISPLAY(mi_kwh)  (mi_kwh)

#endif
