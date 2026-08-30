// =============================================================
//  sdo_manager.c — CANopen SDO client for ZombieVerter VCU
//
//  Ported from m5dial SDOManager.cpp / CANData.cpp.
//  Replaces Arduino APIs with IDF equivalents:
//    millis()       → esp_timer_get_time() / 1000
//    Serial.printf  → ESP_LOGI/D
//    String         → heap char buffer
// =============================================================

#include "sdo_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "sdo";

#define NOW_MS()  ((uint32_t)(esp_timer_get_time() / 1000ULL))

// ── State machine ─────────────────────────────────────────────
typedef enum {
    SDO_IDLE = 0,
    SDO_SEND_REQUEST,
    SDO_WAIT_RESPONSE,
    SDO_PROCESS_RESPONSE,
    SDO_RETRY,
    SDO_FAIL,
    SDO_SEG_INIT,        // segmented upload: send init request
    SDO_SEG_WAIT_INIT,   // segmented upload: wait for init response
    SDO_SEG_REQUEST,     // segmented upload: send segment request
    SDO_SEG_WAIT,        // segmented upload: wait for segment
    SDO_SEG_DONE,        // segmented upload: complete
} sdo_state_t;

// ── Module state ──────────────────────────────────────────────
static QueueHandle_t    s_req_q      = NULL;
static QueueHandle_t    s_rx_q       = NULL;
static SemaphoreHandle_t s_stats_mtx = NULL;
static TaskHandle_t     s_task       = NULL;

static sdo_result_cb_t  s_result_cb  = NULL;
static void            *s_result_ctx = NULL;

static sdo_seg_upload_cb_t s_seg_cb  = NULL;
static void               *s_seg_ctx = NULL;

static volatile uint32_t s_success = 0;
static volatile uint32_t s_failure = 0;
static volatile uint32_t s_timeout = 0;

// ── Helpers ───────────────────────────────────────────────────
static bool prv_send_frame(uint8_t cmd, uint16_t param_id, int32_t value)
{
    twai_message_t tx = {};
    tx.identifier       = SDO_TX_ID;
    tx.data_length_code = 8;

    uint16_t index    = SDO_BASE_INDEX | (param_id >> 8);
    uint8_t  subindex = param_id & 0xFF;

    tx.data[0] = cmd;
    tx.data[1] = (uint8_t)(index & 0xFF);
    tx.data[2] = (uint8_t)(index >> 8);
    tx.data[3] = subindex;
    tx.data[4] = (uint8_t)( value        & 0xFF);
    tx.data[5] = (uint8_t)((value >>  8) & 0xFF);
    tx.data[6] = (uint8_t)((value >> 16) & 0xFF);
    tx.data[7] = (uint8_t)((value >> 24) & 0xFF);

    esp_err_t rc = twai_transmit(&tx, pdMS_TO_TICKS(10));
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "TX fail param %d: %s", param_id, esp_err_to_name(rc));
        return false;
    }
    ESP_LOGD(TAG, "TX [%02X %02X %02X %02X %02X %02X %02X %02X]",
             tx.data[0], tx.data[1], tx.data[2], tx.data[3],
             tx.data[4], tx.data[5], tx.data[6], tx.data[7]);
    return true;
}

static bool prv_send_raw(uint8_t d[8])
{
    twai_message_t tx = {};
    tx.identifier       = SDO_TX_ID;
    tx.data_length_code = 8;
    memcpy(tx.data, d, 8);
    esp_err_t rc = twai_transmit(&tx, pdMS_TO_TICKS(10));
    return rc == ESP_OK;
}

static void prv_deliver(bool ok, uint16_t param_id, int32_t raw_val,
                         bool is_write, uint32_t abort_code)
{
    if (!s_result_cb) return;
    sdo_result_t r = {
        .param_id   = param_id,
        .value      = raw_val,
        .value_f    = raw_val / 32.0f,
        .success    = ok,
        .is_write   = is_write,
        .abort_code = abort_code,
    };
    s_result_cb(&r, s_result_ctx);
}

static const char *prv_abort_str(uint32_t code)
{
    switch (code) {
        case 0x05030000: return "Toggle bit not alternated";
        case 0x05040000: return "SDO protocol timed out";
        case 0x05040001: return "Invalid command";
        case 0x06090011: return "Object does not exist";
        case 0x06090030: return "Value out of range";
        case 0x08000000: return "General error";
        default:         return "Unknown";
    }
}

// ── Main task ─────────────────────────────────────────────────
static void prv_sdo_task(void *arg)
{
    sdo_state_t  state          = SDO_IDLE;
    sdo_request_t cur_req       = {};
    uint32_t     state_entered  = 0;
    uint8_t      retry_count    = 0;
    uint32_t     last_tx_ms     = 0;

    // Segmented upload state
    char        *seg_buf        = NULL;
    size_t       seg_buf_len    = 0;
    size_t       seg_total      = 0;
    bool         seg_toggle     = false;

    for (;;) {
        uint32_t now = NOW_MS();

        switch (state) {

        case SDO_IDLE: {
            if ((now - last_tx_ms) < SDO_MIN_GAP_MS) {
                vTaskDelay(pdMS_TO_TICKS(1));
                break;
            }
            if (xQueueReceive(s_req_q, &cur_req, pdMS_TO_TICKS(10)) == pdTRUE) {
                retry_count = 0;
                if (cur_req.type == SDO_REQ_SEG_UPLOAD)
                    state = SDO_SEG_INIT;
                else
                    state = SDO_SEND_REQUEST;
            }
            break;
        }

        case SDO_SEND_REQUEST: {
            uint8_t cmd = (cur_req.type == SDO_REQ_WRITE) ?
                          SDO_CMD_WRITE : SDO_CMD_READ;
            int32_t val = (cur_req.type == SDO_REQ_WRITE) ? cur_req.value : 0;
            if (prv_send_frame(cmd, cur_req.param_id, val)) {
                state_entered = NOW_MS();
                state = SDO_WAIT_RESPONSE;
            } else {
                state = SDO_RETRY;
            }
            break;
        }

        case SDO_WAIT_RESPONSE: {
            twai_message_t frame;
            if (xQueueReceive(s_rx_q, &frame, pdMS_TO_TICKS(5)) == pdTRUE) {
                uint8_t  cmd      = frame.data[0];
                uint16_t index    = (uint16_t)(frame.data[1] | (frame.data[2] << 8));
                uint8_t  subindex = frame.data[3];
                uint16_t param_id = (uint16_t)(((index & 0xFF) << 8) | subindex);

                if (cmd == SDO_RESP_READ_4B || cmd == SDO_RESP_READ_2B) {
                    int32_t val = (int32_t)(frame.data[4] | (frame.data[5] << 8) |
                                           (frame.data[6] << 16) | (frame.data[7] << 24));
                    ESP_LOGD(TAG, "RX Read OK param %d = %d (%.2f)", param_id, val, val/32.0f);
                    prv_deliver(true, param_id, val, false, 0);
                    xSemaphoreTake(s_stats_mtx, portMAX_DELAY);
                    s_success++;
                    xSemaphoreGive(s_stats_mtx);
                    last_tx_ms = NOW_MS();
                    state = SDO_IDLE;
                } else if (cmd == SDO_RESP_WRITE) {
                    ESP_LOGD(TAG, "RX Write OK param %d", param_id);
                    prv_deliver(true, param_id, cur_req.value, true, 0);
                    xSemaphoreTake(s_stats_mtx, portMAX_DELAY);
                    s_success++;
                    xSemaphoreGive(s_stats_mtx);
                    last_tx_ms = NOW_MS();
                    state = SDO_IDLE;
                } else if (cmd == SDO_RESP_ABORT) {
                    uint32_t abort_code = (uint32_t)(frame.data[4] | (frame.data[5] << 8) |
                                                     (frame.data[6] << 16) | (frame.data[7] << 24));
                    ESP_LOGW(TAG, "RX Abort param %d code 0x%08X (%s)",
                             param_id, (unsigned)abort_code, prv_abort_str(abort_code));
                    prv_deliver(false, param_id, 0, cur_req.type != SDO_REQ_READ, abort_code);
                    xSemaphoreTake(s_stats_mtx, portMAX_DELAY);
                    s_failure++;
                    xSemaphoreGive(s_stats_mtx);
                    last_tx_ms = NOW_MS();
                    state = SDO_IDLE;
                }
                break;
            }
            if ((NOW_MS() - state_entered) >= SDO_TIMEOUT_MS) {
                ESP_LOGW(TAG, "Timeout param %d (attempt %d)", cur_req.param_id, retry_count+1);
                xSemaphoreTake(s_stats_mtx, portMAX_DELAY);
                s_timeout++;
                xSemaphoreGive(s_stats_mtx);
                state = SDO_RETRY;
            }
            break;
        }

        case SDO_RETRY: {
            retry_count++;
            if (retry_count <= SDO_MAX_RETRIES) {
                ESP_LOGD(TAG, "Retry %d/%d param %d", retry_count, SDO_MAX_RETRIES, cur_req.param_id);
                vTaskDelay(pdMS_TO_TICKS(20));
                state = SDO_SEND_REQUEST;
            } else {
                state = SDO_FAIL;
            }
            break;
        }

        case SDO_FAIL: {
            ESP_LOGE(TAG, "FAILED param %d after %d retries", cur_req.param_id, SDO_MAX_RETRIES);
            prv_deliver(false, cur_req.param_id, 0, cur_req.type != SDO_REQ_READ, 0x05040000);
            xSemaphoreTake(s_stats_mtx, portMAX_DELAY);
            s_failure++;
            xSemaphoreGive(s_stats_mtx);
            last_tx_ms = NOW_MS();
            state = SDO_IDLE;
            break;
        }

        // ── Segmented upload state machine ────────────────────────────
        case SDO_SEG_INIT: {
            // Free any previous buffer
            if (seg_buf) { free(seg_buf); seg_buf = NULL; seg_buf_len = 0; }
            seg_total  = 0;
            seg_toggle = false;

            // Send initiate upload request for index 0x5001 subindex 0x00
            uint8_t req[8] = { SDO_CMD_INIT_UP_REQ,
                                SDO_PARAMS_INDEX & 0xFF,
                                (SDO_PARAMS_INDEX >> 8) & 0xFF,
                                SDO_PARAMS_SUBINDEX,
                                0, 0, 0, 0 };
            ESP_LOGI(TAG, "Sending param schema fetch request (0x5001)");
            if (prv_send_raw(req)) {
                state_entered = NOW_MS();
                state = SDO_SEG_WAIT_INIT;
            } else {
                ESP_LOGE(TAG, "Seg init TX failed");
                if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                state = SDO_IDLE;
            }
            break;
        }

        case SDO_SEG_WAIT_INIT: {
            twai_message_t frame;
            if (xQueueReceive(s_rx_q, &frame, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (frame.data[0] == SDO_RESP_ABORT) {
                    uint32_t abort_code = (uint32_t)(frame.data[4] | (frame.data[5]<<8) |
                                                     (frame.data[6]<<16) | (frame.data[7]<<24));
                    ESP_LOGE(TAG, "Seg init abort: 0x%08X", (unsigned)abort_code);
                    if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                    state = SDO_IDLE;
                    break;
                }
                // Initiate upload response — may include size
                if (frame.data[0] & 0x01) {
                    seg_total = (uint32_t)(frame.data[4] | (frame.data[5]<<8) |
                                           (frame.data[6]<<16) | (frame.data[7]<<24));
                    ESP_LOGI(TAG, "Param schema size: %u bytes", (unsigned)seg_total);
                }
                // Allocate buffer
                size_t alloc = (seg_total > 0 && seg_total < 65536) ? seg_total + 64 : 4096;
                seg_buf = (char *)malloc(alloc);
                if (!seg_buf) {
                    ESP_LOGE(TAG, "Seg buf alloc failed");
                    if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                    state = SDO_IDLE;
                    break;
                }
                seg_buf_len = 0;
                state = SDO_SEG_REQUEST;
            } else if ((NOW_MS() - state_entered) >= 1500) {
                ESP_LOGE(TAG, "Seg init timeout");
                if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                state = SDO_IDLE;
            }
            break;
        }

        case SDO_SEG_REQUEST: {
            uint8_t req[8] = {
                (uint8_t)(SDO_CMD_SEG_UP_REQ | (seg_toggle ? SDO_SEG_TOGGLE : 0)),
                0, 0, 0, 0, 0, 0, 0
            };
            if (prv_send_raw(req)) {
                state_entered = NOW_MS();
                state = SDO_SEG_WAIT;
            } else {
                ESP_LOGE(TAG, "Seg request TX failed");
                if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                free(seg_buf); seg_buf = NULL;
                state = SDO_IDLE;
            }
            break;
        }

        case SDO_SEG_WAIT: {
            twai_message_t frame;
            if (xQueueReceive(s_rx_q, &frame, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (frame.data[0] == SDO_RESP_ABORT) {
                    ESP_LOGE(TAG, "Seg abort during download");
                    if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                    free(seg_buf); seg_buf = NULL;
                    state = SDO_IDLE;
                    break;
                }
                bool is_last     = (frame.data[0] & SDO_SEG_LAST) != 0;
                int  unused      = (frame.data[0] >> 1) & 0x07;
                int  bytes_in_seg = is_last ? (7 - unused) : 7;

                // Grow buffer if needed
                if (seg_buf && seg_buf_len + bytes_in_seg + 1 > (seg_total > 0 ? seg_total + 64 : 4096)) {
                    char *new_buf = (char *)realloc(seg_buf, seg_buf_len + bytes_in_seg + 64);
                    if (new_buf) seg_buf = new_buf;
                }

                if (seg_buf) {
                    memcpy(seg_buf + seg_buf_len, &frame.data[1], bytes_in_seg);
                    seg_buf_len += bytes_in_seg;
                }

                seg_toggle = !seg_toggle;

                if (is_last) {
                    if (seg_buf) seg_buf[seg_buf_len] = '\0';
                    ESP_LOGI(TAG, "Param schema downloaded: %u bytes", (unsigned)seg_buf_len);
                    if (s_seg_cb) s_seg_cb(seg_buf, seg_buf_len, s_seg_ctx);
                    free(seg_buf); seg_buf = NULL;
                    last_tx_ms = NOW_MS();
                    state = SDO_IDLE;
                } else {
                    state = SDO_SEG_REQUEST;
                }
            } else if ((NOW_MS() - state_entered) >= SDO_TIMEOUT_MS) {
                ESP_LOGE(TAG, "Seg timeout at byte %u", (unsigned)seg_buf_len);
                if (s_seg_cb) s_seg_cb(NULL, 0, s_seg_ctx);
                free(seg_buf); seg_buf = NULL;
                state = SDO_IDLE;
            }
            break;
        }

        default:
            state = SDO_IDLE;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ── Public API ────────────────────────────────────────────────
esp_err_t sdo_manager_init(sdo_result_cb_t result_cb, void *result_ctx)
{
    s_result_cb  = result_cb;
    s_result_ctx = result_ctx;

    s_req_q    = xQueueCreate(SDO_QUEUE_DEPTH, sizeof(sdo_request_t));
    s_rx_q     = xQueueCreate(32, sizeof(twai_message_t));
    s_stats_mtx = xSemaphoreCreateMutex();

    if (!s_req_q || !s_rx_q || !s_stats_mtx) {
        ESP_LOGE(TAG, "Failed to create FreeRTOS objects");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t rc = xTaskCreatePinnedToCore(
        prv_sdo_task, "sdo", 4096, NULL, 5, &s_task, 0);

    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SDO manager ready  TX=0x%03X  RX=0x%03X", SDO_TX_ID, SDO_RX_ID);
    return ESP_OK;
}

bool sdo_read(uint16_t param_id, bool high_pri)
{
    sdo_request_t req = { .type = SDO_REQ_READ, .param_id = param_id,
                          .value = 0, .high_pri = high_pri };
    if (high_pri)
        return xQueueSendToFront(s_req_q, &req, 0) == pdTRUE;
    return xQueueSend(s_req_q, &req, 0) == pdTRUE;
}

bool sdo_write(uint16_t param_id, float value, bool high_pri)
{
    int32_t raw = (int32_t)(value * 32.0f);
    sdo_request_t req = { .type = SDO_REQ_WRITE, .param_id = param_id,
                          .value = raw, .high_pri = high_pri };
    if (high_pri)
        return xQueueSendToFront(s_req_q, &req, 0) == pdTRUE;
    return xQueueSend(s_req_q, &req, 0) == pdTRUE;
}

bool sdo_fetch_params(sdo_seg_upload_cb_t cb, void *ctx)
{
    s_seg_cb  = cb;
    s_seg_ctx = ctx;
    sdo_request_t req = { .type = SDO_REQ_SEG_UPLOAD, .param_id = 0,
                          .value = 0, .high_pri = true };
    return xQueueSendToFront(s_req_q, &req, 0) == pdTRUE;
}

void sdo_process_frame(const twai_message_t *msg)
{
    if (msg->identifier == SDO_RX_ID && s_rx_q)
        xQueueSend(s_rx_q, msg, 0);
}

uint32_t sdo_get_success_count(void) { return s_success; }
uint32_t sdo_get_failure_count(void) { return s_failure; }
uint32_t sdo_get_timeout_count(void) { return s_timeout; }
