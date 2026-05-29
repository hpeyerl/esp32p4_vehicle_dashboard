// =============================================================
//  sdo_manager.h — CANopen SDO client for ZombieVerter VCU
//
//  Supports:
//    - Expedited read/write (normal params, ×32 fixed-point)
//    - Segmented upload (index 0x5001 = full param schema JSON)
//
//  ZombieVerter specifics:
//    TX CAN ID: 0x603  (P4 → VCU)
//    RX CAN ID: 0x583  (VCU → P4)
//    Node ID:   3
//    Values:    fixed-point ×32 (divide by 32.0f for display)
//    Param index: 0x2100 | (paramId >> 8), subindex = paramId & 0xFF
// =============================================================

#pragma once
#include "esp_err.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Protocol constants ────────────────────────────────────────
#define SDO_TX_ID           0x603
#define SDO_RX_ID           0x583
#define SDO_TIMEOUT_MS      500
#define SDO_MAX_RETRIES     2
#define SDO_MIN_GAP_MS      10
#define SDO_QUEUE_DEPTH     32
#define SDO_BASE_INDEX      0x2100

// SDO command bytes
#define SDO_CMD_READ        0x40
#define SDO_CMD_WRITE       0x23
#define SDO_CMD_SEG_UP_REQ  0x60  // segment upload request
#define SDO_CMD_INIT_UP_REQ 0x40  // initiate upload request

// SDO response bytes
#define SDO_RESP_READ_4B    0x43
#define SDO_RESP_READ_2B    0x4B
#define SDO_RESP_WRITE      0x60
#define SDO_RESP_ABORT      0x80
#define SDO_RESP_INIT_UP    0x41  // initiate upload response (segmented)
#define SDO_RESP_SEG_UP     0x00  // segment upload response (masked with 0x0E)
#define SDO_SEG_LAST        0x01  // last segment bit
#define SDO_SEG_TOGGLE      0x10  // toggle bit

// Index for full param schema JSON
#define SDO_PARAMS_INDEX    0x5001
#define SDO_PARAMS_SUBINDEX 0x00

// ── Request/result types ──────────────────────────────────────
typedef enum {
    SDO_REQ_READ = 0,
    SDO_REQ_WRITE,
    SDO_REQ_SEG_UPLOAD,   // segmented upload (for param fetch)
} sdo_req_type_t;

typedef struct {
    sdo_req_type_t  type;
    uint16_t        param_id;   // VCU numeric param ID
    int32_t         value;      // for writes (already ×32 scaled)
    bool            high_pri;
} sdo_request_t;

typedef struct {
    uint16_t    param_id;
    int32_t     value;          // raw ×32 value from VCU
    float       value_f;        // divided by 32.0f
    bool        success;
    bool        is_write;
    uint32_t    abort_code;
} sdo_result_t;

typedef void (*sdo_result_cb_t)(const sdo_result_t *result, void *ctx);

// ── Segmented upload callback ─────────────────────────────────
// Called when full param schema JSON has been downloaded.
// json is null-terminated, caller must NOT free it (valid until next fetch).
typedef void (*sdo_seg_upload_cb_t)(const char *json, size_t len, void *ctx);

// ── Public API ────────────────────────────────────────────────
esp_err_t sdo_manager_init(sdo_result_cb_t result_cb, void *result_ctx);

// Queue an expedited read — result delivered via result_cb
bool sdo_read(uint16_t param_id, bool high_pri);

// Queue an expedited write (value in human units — we scale ×32)
bool sdo_write(uint16_t param_id, float value, bool high_pri);

// Trigger segmented upload of param schema — cb called when complete
bool sdo_fetch_params(sdo_seg_upload_cb_t cb, void *ctx);

// Feed incoming CAN frame to SDO manager (call from can_rx_task)
void sdo_process_frame(const twai_message_t *msg);

// Stats
uint32_t sdo_get_success_count(void);
uint32_t sdo_get_failure_count(void);
uint32_t sdo_get_timeout_count(void);

#ifdef __cplusplus
}
#endif
