#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a singleton I2C master bus.
 *
 * Safe to call multiple times; the bus will be created only once.
 */
esp_err_t i2c_bus_init(i2c_master_bus_handle_t *out_bus);

/**
 * @brief Get the existing I2C master bus handle.
 *
 * @return ESP_OK and sets *out_bus if initialized, else ESP_ERR_INVALID_STATE.
 */
esp_err_t i2c_bus_get(i2c_master_bus_handle_t *out_bus);

#ifdef __cplusplus
}
#endif

#endif // I2C_BUS_H
