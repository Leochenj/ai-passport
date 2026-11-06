#include "heart_app.h"

#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heart_ble.h"
#include "heart_store.h"
#include "heart_ui.h"
#include <stdint.h>

static const char *TAG = "heart_app";

#define APP_PERIOD_MS 100
#define STALE_MS 5000

static void app_task(void *arg)
{
    (void)arg;
    uint16_t last_bpm = UINT16_MAX;

    for (;;) {
        heart_ble_status_t status;
        heart_ble_get_status(&status);

        const int64_t now_us = esp_timer_get_time();
        const bool fresh = status.has_packet &&
                           (now_us - status.last_packet_us) <=
                               (int64_t)STALE_MS * 1000;
        const uint16_t bpm = fresh ? status.packet.bpm : 0;

        if (bpm != last_bpm) {
            last_bpm = bpm;
            if (bsp_lvgl_lock(50)) {
                heart_ui_set_bpm(bpm);
                bsp_lvgl_unlock();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_PERIOD_MS));
    }
}

esp_err_t heart_app_start(void)
{
    const esp_err_t store_err = heart_store_init();
    if (store_err != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable; the saved sensor will not persist");
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Failed to acquire the LVGL lock");
        return ESP_ERR_TIMEOUT;
    }
    heart_ui_create();
    bsp_lvgl_unlock();

    const esp_err_t ble_err = heart_ble_start();
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE start failed: %s", esp_err_to_name(ble_err));
    }

    if (xTaskCreate(app_task, "heart_app", 3072, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Heartbeat display started");
    return ESP_OK;
}
