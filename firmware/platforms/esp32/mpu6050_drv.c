#include "mpu6050_drv.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "logger.h"

static const char *TAG = "mpu6050";

// MPU6050 registers
#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_WHO_AM_I     0x75
#define MPU6050_REG_PWR_MGMT_1   0x6B

#define MPU6050_WHO_AM_I_EXPECT1 0x68
#define MPU6050_WHO_AM_I_EXPECT2 0x72  // some variants

// CONFIG register DLPF_CFG values
//  4 => ~21Hz accel bandwidth (per datasheet typical table)
#define MPU6050_DLPF_CFG_21HZ    0x04

// ACCEL_CONFIG AFS_SEL values
//  0 => ±2g
#define MPU6050_AFS_SEL_2G       0x00

// Sensitivity for ±2g
#define MPU6050_LSB_PER_G_2G     16384.0f
#define MPU6050_GRAVITY_MS2      9.80665f

static esp_err_t mpu6050_reg_read(mpu6050_t *m, uint8_t reg, uint8_t *data, size_t len)
{
    if (!m || !m->inited) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(m->dev, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t mpu6050_reg_write8(mpu6050_t *m, uint8_t reg, uint8_t val)
{
    if (!m || !m->inited) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(m->dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

esp_err_t mpu6050_init(mpu6050_t *m, i2c_master_bus_handle_t bus, uint8_t i2c_addr)
{
    if (!m || !bus) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(m, 0, sizeof(*m));
    m->bus = bus;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &m->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }
    m->inited = true;

    // WHO_AM_I check
    uint8_t who = 0;
    err = mpu6050_reg_read(m, MPU6050_REG_WHO_AM_I, &who, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (who != MPU6050_WHO_AM_I_EXPECT1 && who != MPU6050_WHO_AM_I_EXPECT2) {
        ESP_LOGW(TAG, "Unexpected WHO_AM_I=0x%02X (continuing)", who);
    } else {
        ESP_LOGI(TAG, "WHO_AM_I=0x%02X", who);
    }

    // Wake up: clear sleep bit; select internal 8MHz oscillator (CLKSEL=0)
    err = mpu6050_reg_write8(m, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PWR_MGMT_1 write failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // Accel range ±2g
    err = mpu6050_reg_write8(m, MPU6050_REG_ACCEL_CONFIG, (MPU6050_AFS_SEL_2G << 3));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ACCEL_CONFIG write failed: %s", esp_err_to_name(err));
        return err;
    }

    // DLPF ~21 Hz
    err = mpu6050_reg_write8(m, MPU6050_REG_CONFIG, MPU6050_DLPF_CFG_21HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CONFIG (DLPF) write failed: %s", esp_err_to_name(err));
        return err;
    }

    // Sample rate divider: sample_rate = gyro_output_rate / (1 + SMPLRT_DIV)
    // With DLPF enabled, gyro output rate is 1kHz. We don't rely on this for timing,
    // but set it to a reasonable value anyway.
    // For ~100Hz: 1000/(1+9)=100
    (void) mpu6050_reg_write8(m, MPU6050_REG_SMPLRT_DIV, 9);

    ESP_LOGI(TAG, "MPU6050 init OK (addr=0x%02X)", i2c_addr);
    return ESP_OK;
}

esp_err_t mpu6050_read_accel_ms2(mpu6050_t *m, float *x_ms2, float *y_ms2, float *z_ms2)
{
    if (!m || !m->inited || !x_ms2 || !y_ms2 || !z_ms2) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[6] = {0};
    esp_err_t err = mpu6050_reg_read(m, MPU6050_REG_ACCEL_XOUT_H, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }

    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);

    const float x_g = ((float)ax) / MPU6050_LSB_PER_G_2G;
    const float y_g = ((float)ay) / MPU6050_LSB_PER_G_2G;
    const float z_g = ((float)az) / MPU6050_LSB_PER_G_2G;

    *x_ms2 = x_g * MPU6050_GRAVITY_MS2;
    *y_ms2 = y_g * MPU6050_GRAVITY_MS2;
    *z_ms2 = z_g * MPU6050_GRAVITY_MS2;

    return ESP_OK;
}
