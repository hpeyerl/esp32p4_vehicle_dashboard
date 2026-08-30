// SPDX-FileCopyrightText: 2026 Herb Peyerl
// SPDX-License-Identifier: BSD-3-Clause

// =============================================================
//  gvret_server.c — GVRET/SavvyCAN network CAN sniffer
//
//  Protocol reference: GVRET v2 as implemented by EVTV GVRET
//  firmware and consumed by SavvyCAN.
//
//  Packet structure (device → host, 0xF1 0x01 frame):
//    [0]    0xF1  start byte
//    [1]    0x01  command: CAN frame
//    [2..5] timestamp µs, uint32 LE
//    [6..7] CAN ID low 16 bits, uint16 LE  (standard frame)
//    [8..9] CAN ID high bits + flags, uint16 LE
//             bit 15: extended frame
//             bits 11–0: ID bits 28–16 (extended only)
//    [10]   bus number (0 = first bus)
//    [11]   DLC (0–8)
//    [12..19] data bytes (always 8 bytes, zero-padded)
//
//  Host → device commands handled:
//    0xF1 0x00  time sync
//    0xF1 0x01  TX frame injection
//    0xF1 0x06  bus enable/disable
//    0xF1 0x0A  set baud rate (ack only)
//    0xF1 0x0B  request version (replies with version string)
// =============================================================

#include "gvret_server.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "driver/twai.h"
#include "esp_timer.h"

#include <string.h>
#include <errno.h>

static const char *TAG = "gvret";

#define GVRET_PORT        23
#define GVRET_BUF_SZ      256
#define GVRET_STACK_SZ    4096
#define GVRET_PRIORITY    4          // below can_rx (10), below ui (5)
#define GVRET_START_BYTE  0xF1

// ── Commands ──────────────────────────────────────────────────
#define CMD_TIME_SYNC     0x00
#define CMD_CAN_FRAME     0x01
#define CMD_TX_FRAME      0x01       // same opcode, direction determines role
#define CMD_BUS_ENABLE    0x06
#define CMD_SET_BAUD      0x0A
#define CMD_VERSION       0x0B

// ── Version string sent in response to 0xF1 0x0B ─────────────
// Format: "GVRET-V2\r\n" — SavvyCAN looks for "GVRET" prefix.
#define GVRET_VERSION_STR "GVRET-V2\r\n"

// ── Shared client socket ──────────────────────────────────────
// Only one SavvyCAN client at a time; protected by mutex.
static SemaphoreHandle_t s_client_mutex = NULL;
static int               s_client_fd    = -1;

// ── Write helpers ─────────────────────────────────────────────
static inline void prv_put_u16le(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v      );
    buf[1] = (uint8_t)(v >>  8);
}

static inline void prv_put_u32le(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v      );
    buf[1] = (uint8_t)(v >>  8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}

// ── Send raw bytes to client (must hold s_client_mutex) ───────
static void prv_send_locked(const uint8_t *data, size_t len)
{
    if (s_client_fd < 0) return;
    int sent = send(s_client_fd, data, len, MSG_DONTWAIT);
    if (sent < 0 && errno != EAGAIN) {
        ESP_LOGW(TAG, "send error %d — dropping client", errno);
        close(s_client_fd);
        s_client_fd = -1;
    }
}

// ── Build & send time-sync reply (0xF1 0x00) ─────────────────
static void prv_send_time_sync(void)
{
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    uint8_t pkt[7];
    pkt[0] = GVRET_START_BYTE;
    pkt[1] = CMD_TIME_SYNC;
    prv_put_u32le(&pkt[2], now_us);
    pkt[6] = 0x00;   // bus 0
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    prv_send_locked(pkt, sizeof(pkt));
    xSemaphoreGive(s_client_mutex);
}

// ── Build & send version reply (0xF1 0x0B) ───────────────────
static void prv_send_version(void)
{
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    prv_send_locked((const uint8_t *)GVRET_VERSION_STR,
                    strlen(GVRET_VERSION_STR));
    xSemaphoreGive(s_client_mutex);
}

// ── Process one complete host→device command ──────────────────
// cmd_buf[0] = 0xF1, cmd_buf[1] = opcode, rest = payload
static void prv_handle_command(const uint8_t *cmd_buf, size_t len)
{
    if (len < 2) return;
    uint8_t cmd = cmd_buf[1];

    switch (cmd) {
    case CMD_TIME_SYNC:
        ESP_LOGD(TAG, "time sync request");
        prv_send_time_sync();
        break;

    case CMD_VERSION:
        ESP_LOGI(TAG, "version request");
        prv_send_version();
        break;

    case CMD_BUS_ENABLE:
        // Payload: [2] = bus, [3] = 0/1 (disable/enable)
        if (len >= 4)
            ESP_LOGI(TAG, "bus %u enable=%u (no-op)", cmd_buf[2], cmd_buf[3]);
        break;

    case CMD_SET_BAUD:
        // Payload: [2] = bus, [3..6] = baud LE uint32
        if (len >= 7) {
            uint32_t baud = (uint32_t)cmd_buf[3]
                          | ((uint32_t)cmd_buf[4] <<  8)
                          | ((uint32_t)cmd_buf[5] << 16)
                          | ((uint32_t)cmd_buf[6] << 24);
            ESP_LOGI(TAG, "set baud bus=%u baud=%lu (ack only)",
                     cmd_buf[2], (unsigned long)baud);
        }
        break;

    case CMD_TX_FRAME:
        // Inject a CAN frame onto the bus.
        // Payload: same layout as the RX frame packet minus start/cmd bytes.
        // [2..5] = timestamp (ignored), [6..7] = ID lo, [8..9] = ID hi+flags
        // [10] = bus, [11] = DLC, [12..] = data
        if (len >= 20) {
            uint16_t id_lo    = (uint16_t)cmd_buf[6] | ((uint16_t)cmd_buf[7] << 8);
            uint16_t id_hi_f  = (uint16_t)cmd_buf[8] | ((uint16_t)cmd_buf[9] << 8);
            bool is_ext       = (id_hi_f & 0x8000) != 0;
            uint32_t can_id   = is_ext
                                ? (((uint32_t)(id_hi_f & 0x1FFF)) << 16) | id_lo
                                : id_lo;
            uint8_t dlc       = cmd_buf[11] > 8 ? 8 : cmd_buf[11];

            twai_message_t tx = {};
            tx.identifier          = can_id;
            tx.data_length_code    = dlc;
            tx.extd                = is_ext ? 1 : 0;
            memcpy(tx.data, &cmd_buf[12], dlc);

            esp_err_t err = twai_transmit(&tx, pdMS_TO_TICKS(10));
            if (err != ESP_OK)
                ESP_LOGW(TAG, "TX inject failed: 0x%x", err);
            else
                ESP_LOGD(TAG, "TX inject id=0x%lx dlc=%u", (unsigned long)can_id, dlc);
        }
        break;

    default:
        ESP_LOGD(TAG, "unhandled GVRET cmd 0x%02X", cmd);
        break;
    }
}

// ── Per-client RX task ────────────────────────────────────────
// Parses the 0xF1-framed byte stream from SavvyCAN.
static void prv_client_rx_task(void *arg)
{
    int fd = (int)(intptr_t)arg;
    uint8_t rx[GVRET_BUF_SZ];
    uint8_t cmd[64];
    int     cmd_len = 0;
    bool    in_cmd  = false;

    ESP_LOGI(TAG, "client connected fd=%d", fd);

    // Immediately send version so SavvyCAN recognises us as GVRET
    // (some versions of SavvyCAN send 0xF1 0x0B first; others expect us to)
    prv_send_version();

    while (1) {
        int n = recv(fd, rx, sizeof(rx), 0);
        if (n <= 0) {
            ESP_LOGI(TAG, "client disconnected fd=%d (n=%d errno=%d)",
                     fd, n, errno);
            break;
        }

        for (int i = 0; i < n; i++) {
            uint8_t b = rx[i];

            if (!in_cmd) {
                if (b == GVRET_START_BYTE) {
                    cmd[0]  = b;
                    cmd_len = 1;
                    in_cmd  = true;
                }
                // else: skip garbage / telnet negotiation bytes
                continue;
            }

            cmd[cmd_len++] = b;

            if (cmd_len < 2) continue;   // need opcode

            // Determine expected total length from opcode
            uint8_t opcode   = cmd[1];
            int     expected = -1;       // -1 = don't know yet

            switch (opcode) {
            case CMD_TIME_SYNC: expected = 2;  break;
            case CMD_VERSION:   expected = 2;  break;
            case CMD_BUS_ENABLE:expected = 4;  break;
            case CMD_SET_BAUD:  expected = 7;  break;
            case CMD_TX_FRAME:  expected = 20; break;
            default:            expected = 2;  break;
            }

            if (cmd_len >= expected) {
                prv_handle_command(cmd, cmd_len);
                cmd_len = 0;
                in_cmd  = false;
            }

            if (cmd_len >= (int)sizeof(cmd)) {
                // Overflow guard
                cmd_len = 0;
                in_cmd  = false;
            }
        }
    }

    // Clean up shared fd reference
    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (s_client_fd == fd) s_client_fd = -1;
    xSemaphoreGive(s_client_mutex);

    close(fd);
    vTaskDelete(NULL);
}

// ── Listener / accept loop ────────────────────────────────────
static void prv_gvret_server_task(void *arg)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(GVRET_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, 1) < 0) {
        ESP_LOGE(TAG, "listen() failed: %d", errno);
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "GVRET listening on TCP port %d", GVRET_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            ESP_LOGW(TAG, "accept() failed: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Kick any existing client
        xSemaphoreTake(s_client_mutex, portMAX_DELAY);
        if (s_client_fd >= 0) {
            ESP_LOGW(TAG, "new client — closing old fd=%d", s_client_fd);
            close(s_client_fd);
        }
        s_client_fd = client_fd;
        xSemaphoreGive(s_client_mutex);

        // Spin a short-lived task to handle RX from this client
        xTaskCreate(prv_client_rx_task, "gvret_rx", GVRET_STACK_SZ,
                    (void *)(intptr_t)client_fd, GVRET_PRIORITY, NULL);
    }
}

// ── Public API ────────────────────────────────────────────────
void gvret_server_start(void)
{
    s_client_mutex = xSemaphoreCreateMutex();
    configASSERT(s_client_mutex);

    xTaskCreate(prv_gvret_server_task, "gvret_srv", GVRET_STACK_SZ,
                NULL, GVRET_PRIORITY, NULL);
    ESP_LOGI(TAG, "GVRET server task started (port %d)", GVRET_PORT);
}

void gvret_forward_frame(const twai_message_t *msg, int64_t timestamp_us)
{
    if (!s_client_mutex) return;

    xSemaphoreTake(s_client_mutex, portMAX_DELAY);
    if (s_client_fd < 0) {
        xSemaphoreGive(s_client_mutex);
        return;
    }

    // Build 0xF1 0x01 packet (20 bytes)
    uint8_t pkt[20];
    uint32_t ts = (uint32_t)timestamp_us;

    bool is_ext = msg->extd;
    uint32_t id = msg->identifier;

    // ID encoding: lo word = bits 15–0, hi word bits 15 = ext flag,
    // bits 12–0 = id bits 28–16 (extended only).
    uint16_t id_lo  = (uint16_t)(id & 0xFFFF);
    uint16_t id_hi  = is_ext
                      ? (0x8000 | (uint16_t)((id >> 16) & 0x1FFF))
                      : 0x0000;

    uint8_t dlc = msg->data_length_code < 8 ? msg->data_length_code : 8;

    pkt[0] = GVRET_START_BYTE;
    pkt[1] = CMD_CAN_FRAME;
    prv_put_u32le(&pkt[2], ts);
    prv_put_u16le(&pkt[6], id_lo);
    prv_put_u16le(&pkt[8], id_hi);
    pkt[10] = 0x00;   // bus 0
    pkt[11] = dlc;
    memcpy(&pkt[12], msg->data, dlc);
    memset(&pkt[12 + dlc], 0, 8 - dlc);   // zero-pad to 8

    prv_send_locked(pkt, sizeof(pkt));
    xSemaphoreGive(s_client_mutex);
}
