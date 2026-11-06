#include "heart_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "heart_store";
static const char *NAMESPACE = "heart";
static const char *KEY_AGE = "age";
static const char *KEY_PEER = "peer";

static bool s_initialized;
static heart_config_t s_config = {
    .age = HEART_DEFAULT_AGE,
};

static uint8_t clamp_age(uint8_t age)
{
    if (age < HEART_MIN_AGE) return HEART_MIN_AGE;
    if (age > HEART_MAX_AGE) return HEART_MAX_AGE;
    return age;
}

esp_err_t heart_store_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要重建(%s)，将清除整个 NVS 分区",
                 esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    heart_store_load(&s_config);
    return ESP_OK;
}

void heart_store_load(heart_config_t *config)
{
    if (config == NULL) return;

    heart_config_t loaded = {
        .age = HEART_DEFAULT_AGE,
    };

    nvs_handle_t handle;
    if (s_initialized && nvs_open(NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t age = HEART_DEFAULT_AGE;
        if (nvs_get_u8(handle, KEY_AGE, &age) == ESP_OK) {
            loaded.age = clamp_age(age);
        }

        uint8_t peer[7];
        size_t length = sizeof(peer);
        if (nvs_get_blob(handle, KEY_PEER, peer, &length) == ESP_OK &&
            length == sizeof(peer)) {
            loaded.has_peer = true;
            memcpy(loaded.peer_addr, peer, 6);
            loaded.peer_type = peer[6];
        }
        nvs_close(handle);
    }

    s_config = loaded;
    *config = loaded;
}

static esp_err_t open_rw(nvs_handle_t *handle)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return nvs_open(NAMESPACE, NVS_READWRITE, handle);
}

esp_err_t heart_store_save_age(uint8_t age)
{
    age = clamp_age(age);
    s_config.age = age;

    nvs_handle_t handle;
    esp_err_t err = open_rw(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, KEY_AGE, age);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "保存年龄失败: %s", esp_err_to_name(err));
    return err;
}

esp_err_t heart_store_save_peer(const uint8_t addr[6], uint8_t addr_type)
{
    if (addr == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t peer[7];
    memcpy(peer, addr, 6);
    peer[6] = addr_type;

    nvs_handle_t handle;
    esp_err_t err = open_rw(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, KEY_PEER, peer, sizeof(peer));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        s_config.has_peer = true;
        memcpy(s_config.peer_addr, addr, 6);
        s_config.peer_type = addr_type;
    } else {
        ESP_LOGW(TAG, "保存心率源失败: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t heart_store_clear_peer(void)
{
    s_config.has_peer = false;

    nvs_handle_t handle;
    esp_err_t err = open_rw(&handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, KEY_PEER);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

void heart_store_get(heart_config_t *config)
{
    if (config != NULL) *config = s_config;
}
