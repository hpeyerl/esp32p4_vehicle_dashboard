// =============================================================
//  bms_data.h — BMW i3 BMS live snapshot (from HTTP /api/data)
//
//  Populated on Linux by bms_http_poll() (linux/src/bms_http.c),
//  read by the BMS screen in dashboard_ui.cpp. Single-threaded:
//  the poll runs in the same loop as dashboard_ui_update(), so no
//  locking is needed.
// =============================================================
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BMS_MAX_MODULES 16
#define BMS_MAX_CELLS   12

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t addr;
    float   voltage;      // module voltage, V
    int16_t t1, t2;       // module temps, °C
    bool    faulted;
    uint8_t num_cells;
    float   cells[BMS_MAX_CELLS];   // per-cell voltage, V
} BmsModule;

typedef struct {
    bool     valid;           // true after the first successful fetch
    uint32_t last_update_ms;  // millis of last successful fetch (staleness)
    float    packV;           // pack voltage, V
    float    soc;             // %
    float    lowCell;         // V
    float    highCell;        // V
    float    avgTemp;         // °C
    float    currentA;        // A
    uint8_t  numModules;
    bool     faulted;
    bool     chargerActive;
    BmsModule modules[BMS_MAX_MODULES];
} BmsData;

// Global instance (defined in src/bms_data.c).
extern BmsData g_bms;

#ifdef __cplusplus
}
#endif
