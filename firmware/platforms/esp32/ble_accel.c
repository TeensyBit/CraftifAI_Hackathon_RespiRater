#include "ble_accel.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "app_config.h"
#include "logger.h"

static const char *TAG = "ble_accel";

static uint8_t s_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_char_val_handle;
static bool s_notify_enabled;

static ble_accel_on_conn_state_cb_t s_on_conn_cb;
static ble_accel_on_sub_state_cb_t s_on_sub_cb;

static int ble_accel_gap_event(struct ble_gap_event *event, void *arg);
static void ble_accel_advertise(void);

// UUIDs from app_config.h strings (Arduino-format) expanded into NimBLE byte order.
// NOTE: BLE_UUID128_INIT expects bytes in little-endian order.
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x14, 0x12, 0x8A, 0x76, 0x04, 0xD1, 0x04, 0x6C,
                     0x6C, 0x4F, 0x7E, 0x53, 0xF2, 0xE8, 0x00, 0x00);

static const ble_uuid128_t s_chr_uuid =
    BLE_UUID128_INIT(0x14, 0x12, 0x8A, 0x76, 0x04, 0xD1, 0x04, 0x6C,
                     0x6C, 0x4F, 0x7E, 0x53, 0xF2, 0xE8, 0x01, 0x00);

// Advertised UUID list (NimBLE expects an array of ble_uuid128_t)
static const ble_uuid128_t s_adv_uuids128[] = {
    BLE_UUID128_INIT(0x14, 0x12, 0x8A, 0x76, 0x04, 0xD1, 0x04, 0x6C,
                     0x6C, 0x4F, 0x7E, 0x53, 0xF2, 0xE8, 0x00, 0x00),
};

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;

    // Read is allowed by flag, but we don't maintain a shadow value here.
    // Returning empty is OK; clients typically use notifications.
    return 0;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_chr_uuid.u,
                .access_cb = gatt_access_cb,
                .val_handle = &s_char_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

static void ble_accel_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset; reason=%d", reason);
}

static void ble_accel_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: rc=%d", rc);
        return;
    }
    ble_accel_advertise();
}

static void ble_accel_advertise(void)
{
    // Keep advertising payload small:
    //  - ADV: flags + 128-bit service UUID
    //  - SCAN_RSP: device name
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields scan_fields;

    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Advertise our 128-bit service UUID
    adv_fields.uuids128 = (ble_uuid128_t *)s_adv_uuids128;
    adv_fields.num_uuids128 = (uint8_t)(sizeof(s_adv_uuids128) / sizeof(s_adv_uuids128[0]));
    adv_fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc);
        return;
    }

    // Put the name in scan response (reduces ADV size)
    memset(&scan_fields, 0, sizeof(scan_fields));
    const char *name = APP_BLE_DEVICE_NAME;
    scan_fields.name = (const uint8_t *)name;
    scan_fields.name_len = (uint8_t)strlen(name);
    scan_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scan_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_scan_rsp rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_accel_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising started");
}

static int ble_accel_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_notify_enabled = false;
            ESP_LOGI(TAG, "Connected; conn_handle=%d", s_conn_handle);
            if (s_on_conn_cb) {
                s_on_conn_cb(true);
            }
        } else {
            ESP_LOGW(TAG, "Connect failed; status=%d", event->connect.status);
            ble_accel_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        if (s_on_conn_cb) {
            s_on_conn_cb(false);
        }
        ble_accel_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Adv complete");
        ble_accel_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_char_val_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Subscribe: notify=%d", (int)s_notify_enabled);
            if (s_on_sub_cb) {
                s_on_sub_cb(s_notify_enabled);
            }
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated; mtu=%d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_accel_init(ble_accel_on_conn_state_cb_t on_conn, ble_accel_on_sub_state_cb_t on_sub)
{
    s_on_conn_cb = on_conn;
    s_on_sub_cb = on_sub;

    // NVS needed for BLE PHY calibration data
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.reset_cb = ble_accel_on_reset;
    ble_hs_cfg.sync_cb = ble_accel_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(APP_BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set rc=%d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);

    return ESP_OK;
}

bool ble_accel_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool ble_accel_is_notify_enabled(void)
{
    return s_notify_enabled;
}

esp_err_t ble_accel_notify(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_notify_enabled) {
        return ESP_OK;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_char_val_handle, om);
    if (rc != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}
