#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "app_config.h"
#include "logger.h"

#include "accel_stream.h"
#include "i2c_bus.h"
#include "mpu6050_drv.h"

static const char *TAG = "sample_task";

static void sample_task(void *arg)
{
    (void)arg;

    i2c_master_bus_handle_t bus;
    mpu6050_t mpu;

    int consecutive_failures = 0;

    while (1) {
        esp_err_t err = i2c_bus_init(&bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        err = mpu6050_init(&mpu, bus, APP_MPU6050_I2C_ADDR);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MPU6050 init failed: %s (retrying)", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        QueueHandle_t q = accel_stream_get_queue();
        if (q == NULL) {
            ESP_LOGE(TAG, "Sample queue not created");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int64_t next_us = esp_timer_get_time();
        const int64_t interval_us = APP_SAMPLE_INTERVAL_US;

        ESP_LOGI(TAG, "Sampling started @ %d Hz", APP_SAMPLE_RATE_HZ);

        while (1) {
            next_us += interval_us;

            float x = 0, y = 0, z = 0;
            err = mpu6050_read_accel_ms2(&mpu, &x, &y, &z);
            if (err == ESP_OK) {
                consecutive_failures = 0;

                AccelData d = {
                    .x = x,
                    .y = y,
                    .z = z,
                };

                // Non-blocking enqueue. If full, drop newest.
                (void)xQueueSend(q, &d, 0);
            } else {
                consecutive_failures++;
                if (consecutive_failures == 1 || (consecutive_failures % 10) == 0) {
                    ESP_LOGW(TAG, "MPU read failed (%d): %s", consecutive_failures, esp_err_to_name(err));
                }
                if (consecutive_failures >= APP_MPU6050_REINIT_ERRORS) {
                    ESP_LOGE(TAG, "Too many read errors; reinitializing MPU");
                    break;
                }
            }

            // Cadence control using esp_timer_get_time(); keep drift low.
            int64_t now = esp_timer_get_time();
            int64_t sleep_us = next_us - now;
            if (sleep_us > 0) {
                // FreeRTOS delay has 1ms granularity; do coarse delay, then spin-yield.
                if (sleep_us > 2000) {
                    vTaskDelay(pdMS_TO_TICKS((sleep_us - 1000) / 1000));
                }
                while (esp_timer_get_time() < next_us) {
                    taskYIELD();
                }
            } else {
                // We're late; resync to prevent runaway.
                next_us = now;
            }
        }

        // Loop back for re-init
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void sample_task_start(void)
{
    xTaskCreate(sample_task, "sample_task", 4096, NULL, 6, NULL);
}
