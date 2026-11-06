// FoloToy AI Passport — 心跳 (heartbeat monitor).
//
// The device is a BLE central for the standard Heart Rate Service (0x180D).
// An iPhone app such as HeartCast can rebroadcast Apple Watch / HealthKit heart
// rate into that service; the display is this application's own UI, not the
// baseline hardware-test menu.
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "heart_app.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "FoloToy AI Passport 心跳应用启动");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，应用无法继续");
        return;
    }
    bsp_display_backlight(85);

    const esp_err_t err = heart_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "心跳应用启动失败: %s", esp_err_to_name(err));
    }
}
