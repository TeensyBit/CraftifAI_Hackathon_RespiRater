#ifndef MPU6050_DRV_H
#define MPU6050_DRV_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    bool inited;
} mpu6050_t;

/**
 * @brief Initialize MPU6050 on an existing I2C bus.
 *
 * Configures:
 *  - wakes device
 *  - accel range ±2g
 *  - DLPF ~21Hz
 */
esp_err_t mpu6050_init(mpu6050_t *m, i2c_master_bus_handle_t bus, uint8_t i2c_addr);

/**
 * @brief Read acceleration in m/s^2 as float.
 */
esp_err_t mpu6050_read_accel_ms2(mpu6050_t *m, float *x_ms2, float *y_ms2, float *z_ms2);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_DRV_H
