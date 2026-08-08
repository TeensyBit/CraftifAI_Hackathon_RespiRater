#include "app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "app_config.h"
#include "logger.h"

#include "accel_stream.h"
#include "ble_accel.h"
#include "ble_notify_task.h"
#include "sample_task.h"

static const char *TAG = "app";

static void on_conn_state(bool connected)
{
    ESP_LOGI(TAG, "BLE connected=%d", (int)connected);
}

static void on_sub_state(bool notify_enabled)
{
    ESP_LOGI(TAG, "BLE notify_enabled=%d", (int)notify_enabled);
}

void app_start(void)
{
    ESP_LOGI(TAG, "firmware started");

    QueueHandle_t q = accel_stream_create_queue();
    if (q == NULL) {
        ESP_LOGE(TAG, "Failed to create sample queue");
        return;
    }

    esp_err_t err = ble_accel_init(on_conn_state, on_sub_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(err));
        return;
    }

    // Start tasks (sampling at 100Hz + BLE notify consumer)
    sample_task_start();
    ble_notify_task_start();

    ESP_LOGI(TAG, "Init complete. Connect over BLE and enable notifications.");
}
