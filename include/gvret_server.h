// =============================================================
//  gvret_server.h — GVRET/SavvyCAN network CAN sniffer
//
//  Listens on TCP port 23 (telnet).  SavvyCAN connects via
//  "Network Connection" → set host to the board's IP, port 23.
//
//  Implements the binary GVRET v2 protocol:
//    0xF1 0x00  — Time sync request / reply
//    0xF1 0x01  — Single CAN frame (receive path)
//    0xF1 0x02  — TX frame injection (optional)
//    0xF1 0x06  — Enable/disable bus
//    0xF1 0x09  — Single wire CAN (ignored)
//    0xF1 0x0A  — Set baud rate (accepted, not acted on)
//    0xF1 0x0B  — GVRET version / capabilities reply
//
//  All multi-byte fields are little-endian.
//
//  Usage:
//    Call gvret_server_start() once after WiFi connects.
//    Hook gvret_forward_frame() into can_rx_task().
// =============================================================
#pragma once
#include <stdint.h>
#include "driver/twai.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the TCP listener task (port 23, single client).
// Safe to call before WiFi connects — it will just accept no connections.
void gvret_server_start(void);

// Forward a received CAN frame to any connected GVRET client.
// Call from can_rx_task() after twai_receive() succeeds.
// timestamp_us: esp_timer_get_time() value at frame receipt.
void gvret_forward_frame(const twai_message_t *msg, int64_t timestamp_us);

#ifdef __cplusplus
}
#endif
