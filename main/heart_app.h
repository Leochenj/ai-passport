#pragma once

#include "esp_err.h"

// Creates the heartbeat screen, starts the BLE central and the application
// task. Call after display and LVGL initialization.
esp_err_t heart_app_start(void);
