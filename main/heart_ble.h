// NimBLE central that connects to the standard Bluetooth SIG Heart Rate
// Service (0x180D) and subscribes to the Heart Rate Measurement
// characteristic (0x2A37).
#pragma once

#include "esp_err.h"
#include "heart_model.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEART_BLE_STARTING = 0,
    HEART_BLE_SCANNING,
    HEART_BLE_CONNECTING,
    HEART_BLE_DISCOVERING,
    HEART_BLE_SUBSCRIBED,
    HEART_BLE_RETRY_WAIT,
    HEART_BLE_UNAVAILABLE,
} heart_ble_state_t;

typedef struct {
    heart_ble_state_t state;
    char device_name[24];
    uint8_t device_addr[6];
    uint8_t device_addr_type;
    int8_t rssi;
    uint16_t conn_handle;
    uint32_t packets;
    uint32_t scan_count;
    uint32_t connect_count;
    int last_error;
    int64_t last_packet_us;
    bool has_packet;
    heart_packet_t packet;
} heart_ble_status_t;

// Initializes NVS (when possible), the NimBLE host, and starts scanning.
// The call returns after the host task is created; connection progress is
// reported through heart_ble_get_status().
esp_err_t heart_ble_start(void);

// Copies the latest link state and decoded packet. Thread-safe; call at UI
// refresh rate.
void heart_ble_get_status(heart_ble_status_t *status);

// Disconnects the current sensor (if any) and starts a fresh scan. The saved
// peer is still preferred, which keeps "reconnect" fast.
void heart_ble_rescan(void);

// Forgets the saved sensor address and starts a fresh scan.
void heart_ble_forget(void);
