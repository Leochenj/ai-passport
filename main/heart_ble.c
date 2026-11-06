#include "heart_ble.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heart_store.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include <string.h>

static const char *TAG = "heart_ble";

#define HR_SERVICE_UUID16 0x180D
#define HR_MEASUREMENT_UUID16 0x2A37
#define CCCD_UUID16 0x2902
#define SCAN_DURATION_MS 12000
#define SCAN_RESTART_MS 1500
#define SELECT_DELAY_MS 1200
#define DISCOVERY_TIMEOUT_MS 12000
#define CONNECT_TIMEOUT_MS 10000

static ble_uuid16_t s_hrs_uuid = BLE_UUID16_INIT(HR_SERVICE_UUID16);
static ble_uuid16_t s_hr_measurement_uuid = BLE_UUID16_INIT(HR_MEASUREMENT_UUID16);

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static heart_ble_status_t s_status;
static heart_config_t s_config;
static bool s_started;
static volatile bool s_running;
static volatile bool s_scanning;
static volatile bool s_connecting;
static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start;
static uint16_t s_svc_end;
static uint16_t s_chr_val;
static bool s_cccd_found;
static bool s_subscribed;
static bool s_peer_saved;
static uint8_t s_peer_addr[6];
static uint8_t s_peer_addr_type;

static esp_timer_handle_t s_select_timer;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_discovery_timer;
static TaskHandle_t s_host_task;

typedef struct {
    bool valid;
    ble_addr_t addr;
    int8_t rssi;
    char name[24];
} candidate_t;

static candidate_t s_best;

static int gap_event(struct ble_gap_event *event, void *arg);

static void set_state(heart_ble_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    portEXIT_CRITICAL(&s_lock);
}

static void set_error(int error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.last_error = error;
    portEXIT_CRITICAL(&s_lock);
}

static void store_name(char *destination, const uint8_t *name, uint8_t length)
{
    size_t count = length;
    if (count > 23) count = 23;
    for (size_t i = 0; i < count; i++) {
        const uint8_t value = name[i];
        destination[i] = (value >= 0x20 && value < 0x7F) ? (char)value : '?';
    }
    destination[count] = '\0';
}

static void set_device(const ble_addr_t *addr, int8_t rssi, const uint8_t *name,
                       uint8_t name_length)
{
    portENTER_CRITICAL(&s_lock);
    memcpy(s_status.device_addr, addr->val, 6);
    s_status.device_addr_type = addr->type;
    s_status.rssi = rssi;
    if (name != NULL && name_length > 0) {
        store_name(s_status.device_name, name, name_length);
    }
    portEXIT_CRITICAL(&s_lock);
}

static bool peer_is_saved(const ble_addr_t *addr)
{
    return s_config.has_peer && memcmp(s_config.peer_addr, addr->val, 6) == 0;
}

static void schedule_retry(uint32_t delay_ms)
{
    if (!s_running || s_retry_timer == NULL) return;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000u);
}

static void start_scan(void);

static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (!s_running || s_connecting || s_conn_handle != BLE_HS_CONN_HANDLE_NONE) return;
    start_scan();
}

static void select_timer_cb(void *arg)
{
    (void)arg;
    if (!s_running || s_scanning == false ||
        s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    ble_addr_t addr;
    bool valid;
    int8_t rssi;
    char name[24];
    portENTER_CRITICAL(&s_lock);
    valid = s_best.valid;
    addr = s_best.addr;
    rssi = s_best.rssi;
    memcpy(name, s_best.name, sizeof(name));
    portEXIT_CRITICAL(&s_lock);
    if (!valid) return;

    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "停止扫描失败: %d", rc);
        schedule_retry(SCAN_RESTART_MS);
        return;
    }
    s_scanning = false;

    struct ble_gap_conn_params params = {
        .scan_itvl = BLE_GAP_SCAN_ITVL_MS(60),
        .scan_window = BLE_GAP_SCAN_WIN_MS(30),
        .itvl_min = BLE_GAP_CONN_ITVL_MS(100),
        .itvl_max = BLE_GAP_CONN_ITVL_MS(200),
        .latency = 0,
        .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(5000),
        .min_ce_len = 0,
        .max_ce_len = 0,
    };

    set_device(&addr, rssi, (const uint8_t *)name, (uint8_t)strlen(name));
    set_state(HEART_BLE_CONNECTING);
    portENTER_CRITICAL(&s_lock);
    s_status.connect_count++;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "连接心率设备 %02X:%02X:%02X:%02X:%02X:%02X (RSSI %d)",
             addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1],
             addr.val[0], rssi);

    s_connecting = true;
    rc = ble_gap_connect(s_own_addr_type, &addr, CONNECT_TIMEOUT_MS, &params,
                         gap_event, NULL);
    if (rc != 0) {
        s_connecting = false;
        ESP_LOGW(TAG, "发起连接失败: %d", rc);
        set_error(rc);
        set_state(HEART_BLE_RETRY_WAIT);
        schedule_retry(SCAN_RESTART_MS);
    }
}

static void discovery_timeout_cb(void *arg)
{
    (void)arg;
    if (!s_running || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_subscribed) return;
    ESP_LOGW(TAG, "订阅心率通知超时，断开重试");
    set_error(0);
    set_state(HEART_BLE_RETRY_WAIT);
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void start_scan(void)
{
    if (!s_running || s_connecting ||
        s_conn_handle != BLE_HS_CONN_HANDLE_NONE || s_scanning) return;

    portENTER_CRITICAL(&s_lock);
    s_best.valid = false;
    portEXIT_CRITICAL(&s_lock);

    struct ble_gap_disc_params params = {
        .itvl = BLE_GAP_SCAN_ITVL_MS(80),
        .window = BLE_GAP_SCAN_WIN_MS(60),
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        // Keep duplicates enabled: some broadcasters put 0x180D only in the
        // scan response, and controller duplicate filtering can hide it.
        .filter_duplicates = 0,
    };

    const int rc = ble_gap_disc(s_own_addr_type, SCAN_DURATION_MS, &params,
                                gap_event, NULL);
    if (rc == 0) {
        s_scanning = true;
        portENTER_CRITICAL(&s_lock);
        s_status.scan_count++;
        portEXIT_CRITICAL(&s_lock);
        set_state(HEART_BLE_SCANNING);
        ESP_LOGI(TAG, "开始扫描标准心率服务 (0x180D)");
    } else if (rc == BLE_HS_EALREADY) {
        s_scanning = true;
    } else {
        ESP_LOGW(TAG, "启动扫描失败: %d", rc);
        set_error(rc);
        set_state(HEART_BLE_RETRY_WAIT);
        schedule_retry(SCAN_RESTART_MS);
    }
}

static void fail_discovery(int reason, const char *message)
{
    ESP_LOGW(TAG, "%s (%d)", message, reason);
    set_error(reason);
    set_state(HEART_BLE_RETRY_WAIT);
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    } else {
        schedule_retry(SCAN_RESTART_MS);
    }
}

static int cccd_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error->status != 0) {
        fail_discovery(error->status, "写入 CCCD 失败");
        return 0;
    }

    s_subscribed = true;
    esp_timer_stop(s_discovery_timer);
    set_state(HEART_BLE_SUBSCRIBED);
    if (!s_peer_saved) {
        s_peer_saved = true;
        heart_store_save_peer(s_peer_addr, s_peer_addr_type);
        heart_store_get(&s_config);
    }
    ESP_LOGI(TAG, "已订阅心率通知");
    return 0;
}

static int dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                  void *arg)
{
    (void)chr_val_handle;
    (void)arg;

    if (error->status == 0 && dsc != NULL) {
        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(CCCD_UUID16)) == 0) {
            s_cccd_found = true;
            const uint8_t value[2] = {0x01, 0x00};  // notifications enabled
            const int rc = ble_gattc_write_flat(conn_handle, dsc->handle, value,
                                                sizeof(value), cccd_write_cb, NULL);
            if (rc != 0) fail_discovery(rc, "发起 CCCD 写入失败");
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        // EDONE also arrives after the CCCD write was issued; only a missing
        // descriptor is a real failure.
        if (!s_cccd_found) {
            fail_discovery(error->status, "心率特征缺少 CCCD");
        }
        return 0;
    }

    fail_discovery(error->status, "发现描述符失败");
    return 0;
}

static int chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (error->status == 0 && chr != NULL) {
        if ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) == 0) {
            fail_discovery(0, "心率特征不支持通知");
            return 0;
        }
        s_chr_val = chr->val_handle;
        const int rc = ble_gattc_disc_all_dscs(conn_handle, s_chr_val, s_svc_end,
                                               dsc_cb, NULL);
        if (rc != 0) fail_discovery(rc, "发起描述符发现失败");
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        if (s_chr_val == 0) fail_discovery(error->status, "找不到心率测量特征 0x2A37");
        return 0;
    }

    fail_discovery(error->status, "发现心率特征失败");
    return 0;
}

static int svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;

    if (error->status == 0 && service != NULL) {
        s_svc_start = service->start_handle;
        s_svc_end = service->end_handle;
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        if (s_svc_end == 0) {
            fail_discovery(error->status, "找不到心率服务 0x180D");
            return 0;
        }
        const int rc = ble_gattc_disc_chrs_by_uuid(conn_handle, s_svc_start,
                                                   s_svc_end,
                                                   &s_hr_measurement_uuid.u,
                                                   chr_cb, NULL);
        if (rc != 0) fail_discovery(rc, "发起心率特征发现失败");
        return 0;
    }

    fail_discovery(error->status, "发现心率服务失败");
    return 0;
}

static void start_discovery(uint16_t conn_handle)
{
    s_svc_start = 0;
    s_svc_end = 0;
    s_chr_val = 0;
    s_cccd_found = false;
    s_subscribed = false;

    esp_timer_stop(s_discovery_timer);
    esp_timer_start_once(s_discovery_timer, (uint64_t)DISCOVERY_TIMEOUT_MS * 1000u);

    const int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &s_hrs_uuid.u,
                                              svc_cb, NULL);
    if (rc != 0) fail_discovery(rc, "发起心率服务发现失败");
}

static void handle_disc(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return;

    bool offered = false;
    for (uint8_t i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == HR_SERVICE_UUID16) {
            offered = true;
            break;
        }
    }
    if (!offered) return;

    if (peer_is_saved(&disc->addr)) {
        ESP_LOGI(TAG, "发现上次使用的心率设备，直接连接");
        esp_timer_stop(s_select_timer);
        ble_gap_disc_cancel();
        s_scanning = false;

        set_device(&disc->addr, disc->rssi, fields.name, fields.name_len);
        set_state(HEART_BLE_CONNECTING);
        s_connecting = true;

        struct ble_gap_conn_params params = {
            .scan_itvl = BLE_GAP_SCAN_ITVL_MS(60),
            .scan_window = BLE_GAP_SCAN_WIN_MS(30),
            .itvl_min = BLE_GAP_CONN_ITVL_MS(100),
            .itvl_max = BLE_GAP_CONN_ITVL_MS(200),
            .latency = 0,
            .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(5000),
            .min_ce_len = 0,
            .max_ce_len = 0,
        };
        const int rc = ble_gap_connect(s_own_addr_type, &disc->addr,
                                       CONNECT_TIMEOUT_MS, &params, gap_event, NULL);
        if (rc != 0) {
            s_connecting = false;
            ESP_LOGW(TAG, "连接已保存设备失败: %d", rc);
            set_error(rc);
            set_state(HEART_BLE_RETRY_WAIT);
            schedule_retry(SCAN_RESTART_MS);
        }
        return;
    }

    portENTER_CRITICAL(&s_lock);
    if (!s_best.valid) {
        s_best.valid = true;
        s_best.addr = disc->addr;
        s_best.rssi = disc->rssi;
        if (fields.name != NULL) store_name(s_best.name, fields.name, fields.name_len);
        else s_best.name[0] = '\0';
        portEXIT_CRITICAL(&s_lock);
        esp_timer_start_once(s_select_timer, (uint64_t)SELECT_DELAY_MS * 1000u);
        return;
    }
    if (disc->rssi > s_best.rssi) {
        s_best.addr = disc->addr;
        s_best.rssi = disc->rssi;
        if (fields.name != NULL) store_name(s_best.name, fields.name, fields.name_len);
    }
    portEXIT_CRITICAL(&s_lock);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_disc(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        s_connecting = false;
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "连接失败: %d", event->connect.status);
            set_error(event->connect.status);
            set_state(HEART_BLE_RETRY_WAIT);
            schedule_retry(SCAN_RESTART_MS);
            return 0;
        }

        s_conn_handle = event->connect.conn_handle;
        s_scanning = false;
        s_peer_saved = false;
        portENTER_CRITICAL(&s_lock);
        s_status.conn_handle = s_conn_handle;
        memcpy(s_peer_addr, s_status.device_addr, 6);
        s_peer_addr_type = s_status.device_addr_type;
        portEXIT_CRITICAL(&s_lock);

        set_state(HEART_BLE_DISCOVERING);
        ESP_LOGI(TAG, "已连接 (handle %u)，开始发现心率服务", s_conn_handle);
        start_discovery(s_conn_handle);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "心率设备断开 (reason %d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_scanning = false;
        s_connecting = false;
        s_subscribed = false;
        s_peer_saved = false;
        esp_timer_stop(s_discovery_timer);
        portENTER_CRITICAL(&s_lock);
        s_status.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        portEXIT_CRITICAL(&s_lock);
        set_state(HEART_BLE_RETRY_WAIT);
        schedule_retry(SCAN_RESTART_MS);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = false;
        if (!s_connecting && s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            set_state(HEART_BLE_RETRY_WAIT);
            schedule_retry(SCAN_RESTART_MS);
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_chr_val &&
            event->subscribe.cur_notify) {
            s_subscribed = true;
            esp_timer_stop(s_discovery_timer);
            set_state(HEART_BLE_SUBSCRIBED);
            if (!s_peer_saved) {
                s_peer_saved = true;
                heart_store_save_peer(s_peer_addr, s_peer_addr_type);
                heart_store_get(&s_config);
            }
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.attr_handle != s_chr_val ||
            event->notify_rx.om == NULL) {
            return 0;
        }

        uint8_t data[32];
        const uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        const uint16_t copy = length > sizeof(data) ? sizeof(data) : length;
        if (os_mbuf_copydata(event->notify_rx.om, 0, copy, data) != 0) return 0;

        heart_packet_t packet;
        if (!heart_parse_measurement(data, copy, &packet)) {
            if (!heart_parse_measurement_compat(data, copy, &packet)) {
                ESP_LOGW(TAG, "收到无法解析的心率帧 (%u 字节: %02X %02X)",
                         copy, copy > 0 ? data[0] : 0,
                         copy > 1 ? data[1] : 0);
                return 0;
            }
            ESP_LOGW(TAG, "兼容非标准心率帧 (%u 字节: %02X %02X -> %u bpm)",
                     copy, copy > 0 ? data[0] : 0,
                     copy > 1 ? data[1] : 0, packet.bpm);
        }

        portENTER_CRITICAL(&s_lock);
        s_status.packet = packet;
        s_status.has_packet = true;
        s_status.packets++;
        s_status.last_packet_us = esp_timer_get_time();
        s_status.state = HEART_BLE_SUBSCRIBED;
        portEXIT_CRITICAL(&s_lock);
        return 0;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU 更新: %u", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: %d", reason);
    s_connecting = false;
    s_scanning = false;
    set_error(reason);
    set_state(HEART_BLE_RETRY_WAIT);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        set_error(0);
        set_state(HEART_BLE_UNAVAILABLE);
        return;
    }
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        set_error(0);
        set_state(HEART_BLE_UNAVAILABLE);
        return;
    }
    start_scan();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    set_state(HEART_BLE_UNAVAILABLE);
    vTaskDelete(NULL);
}

static void delete_timers(void)
{
    if (s_select_timer) {
        esp_timer_stop(s_select_timer);
        esp_timer_delete(s_select_timer);
        s_select_timer = NULL;
    }
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
        esp_timer_delete(s_retry_timer);
        s_retry_timer = NULL;
    }
    if (s_discovery_timer) {
        esp_timer_stop(s_discovery_timer);
        esp_timer_delete(s_discovery_timer);
        s_discovery_timer = NULL;
    }
}

esp_err_t heart_ble_start(void)
{
    if (s_started) return ESP_OK;

    memset(&s_status, 0, sizeof(s_status));
    s_status.state = HEART_BLE_STARTING;
    s_status.conn_handle = BLE_HS_CONN_HANDLE_NONE;

    esp_err_t err = heart_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 不可用，心率源设置不会持久化: %s", esp_err_to_name(err));
    }
    heart_store_load(&s_config);

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE 初始化失败: %s", esp_err_to_name(err));
        set_state(HEART_BLE_UNAVAILABLE);
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    const esp_timer_create_args_t select_args = {
        .callback = select_timer_cb,
        .name = "heart_select",
    };
    const esp_timer_create_args_t retry_args = {
        .callback = retry_timer_cb,
        .name = "heart_retry",
    };
    const esp_timer_create_args_t discovery_args = {
        .callback = discovery_timeout_cb,
        .name = "heart_disc",
    };

    err = esp_timer_create(&select_args, &s_select_timer);
    if (err == ESP_OK) err = esp_timer_create(&retry_args, &s_retry_timer);
    if (err == ESP_OK) err = esp_timer_create(&discovery_args, &s_discovery_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建定时器失败: %s", esp_err_to_name(err));
        delete_timers();
        nimble_port_deinit();
        set_state(HEART_BLE_UNAVAILABLE);
        return err;
    }

    s_running = true;
    if (xTaskCreate(host_task, "nimble_host", 4096, NULL, 5, &s_host_task) != pdPASS) {
        s_running = false;
        delete_timers();
        nimble_port_deinit();
        set_state(HEART_BLE_UNAVAILABLE);
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

void heart_ble_get_status(heart_ble_status_t *status)
{
    if (status == NULL) return;
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

void heart_ble_rescan(void)
{
    if (!s_started) return;
    esp_timer_stop(s_select_timer);
    portENTER_CRITICAL(&s_lock);
    s_best.valid = false;
    portEXIT_CRITICAL(&s_lock);

    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        set_state(HEART_BLE_RETRY_WAIT);
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    if (s_connecting) {
        ble_gap_conn_cancel();
        s_connecting = false;
    }
    ble_gap_disc_cancel();
    s_scanning = false;
    start_scan();
}

void heart_ble_forget(void)
{
    heart_store_clear_peer();
    heart_store_get(&s_config);
    heart_ble_rescan();
}
