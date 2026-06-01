// =============================================================
//  gear_shifter.c — CAN PRND gear shift transmitter
// =============================================================

#include "gear_shifter.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define TAG             "shifter"
#define CAN_ID_GEAR_TX  0x312
#define TX_INTERVAL_MS  20

static volatile int8_t s_gear = -1;

static void prv_shifter_task(void *arg)
{
    uint8_t cnt = 0;
    TickType_t last_tx = xTaskGetTickCount();

    for (;;) {
        // Bus-off / stopped recovery (mirrors M5Dial canTask)
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            if      (st.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
            else if (st.state == TWAI_STATE_STOPPED) twai_start();
        }

        int8_t gear = s_gear;
        if (gear >= 0 && (xTaskGetTickCount() - last_tx >= pdMS_TO_TICKS(TX_INTERVAL_MS))) {
            last_tx = xTaskGetTickCount();

            uint8_t data[8] = {0};
            data[3] = ((uint8_t)gear << 4) & 0xF0;
            data[7] = cnt & 0x0F;
            cnt = (cnt + 1) & 0x0F;

            twai_message_t tx;
            memset(&tx, 0, sizeof(tx));
            tx.identifier       = CAN_ID_GEAR_TX;
            tx.data_length_code = 8;
            memcpy(tx.data, data, 8);
            twai_transmit(&tx, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void gear_shifter_start(void)
{
    xTaskCreatePinnedToCore(prv_shifter_task, "gear_tx", 2048, NULL, 9, NULL, 0);
    ESP_LOGI(TAG, "gear shifter task started");
}

void gear_shifter_request(int8_t gear)
{
    if (gear < -1 || gear > 3) return;
    s_gear = gear;
    if (gear >= 0) {
        const char *names[] = {"P", "R", "N", "D"};
        ESP_LOGI(TAG, "gear -> %s", names[(int)gear]);
    }
}

int8_t gear_shifter_current(void)
{
    return s_gear;
}
