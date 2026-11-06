// Small NVS-backed settings store for the heartbeat application.
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define HEART_DEFAULT_AGE 30
#define HEART_MIN_AGE 20
#define HEART_MAX_AGE 70

typedef struct {
    uint8_t age;
    bool has_peer;
    uint8_t peer_addr[6];
    uint8_t peer_type;
} heart_config_t;

// Initializes NVS once and loads defaults. Safe to call repeatedly.
esp_err_t heart_store_init(void);

void heart_store_load(heart_config_t *config);
void heart_store_get(heart_config_t *config);
esp_err_t heart_store_save_age(uint8_t age);
esp_err_t heart_store_save_peer(const uint8_t addr[6], uint8_t addr_type);
esp_err_t heart_store_clear_peer(void);
