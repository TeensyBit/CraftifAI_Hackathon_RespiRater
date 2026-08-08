#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "app_config.h"
#include "logger.h"

#include "accel_stream.h"
#include "ble_accel.h"

static const char *TAG = "ble_notify";

static void ble_notify_task(void *arg)
{
    (void)arg;

    QueueHandle_t q = accel_stream_get_queue();
    if (q == NULL) {
        ESP_LOGE(TAG, "Queue not created");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "BLE notify task started");

    while (1) {
        AccelData d;
        if (xQueueReceive(q, &d, portMAX_DELAY) == pdTRUE) {
            if (ble_accel_is_connected() && ble_accel_is_notify_enabled()) {
                esp_err_t err = ble_accel_notify((const uint8_t *)&d, sizeof(d));
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "notify failed: %s", esp_err_to_name(err));
                }
            }
        }
    }
}

void ble_notify_task_start(void)
{
    xTaskCreate(ble_notify_task, "ble_notify", 4096, NULL, 5, NULL);
}
