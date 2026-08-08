#include <stdbool.h>

#include "i2c_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "logger.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus;
static bool s_inited;
static SemaphoreHandle_t s_mutex;

static void i2c_bus_lock_init_once(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

esp_err_t i2c_bus_get(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_bus = s_bus;
    return ESP_OK;
}

esp_err_t i2c_bus_init(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_bus_lock_init_once();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_inited) {
        *out_bus = s_bus;
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = APP_I2C_SDA_GPIO,
        .scl_io_num = APP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_mutex);
        return err;
    }

    s_inited = true;
    *out_bus = s_bus;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "I2C bus init OK (SDA=%d, SCL=%d, freq=%d Hz)",
             APP_I2C_SDA_GPIO, APP_I2C_SCL_GPIO, APP_I2C_FREQ_HZ);

    return ESP_OK;
}
