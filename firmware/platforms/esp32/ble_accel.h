#ifndef BLE_ACCEL_H
#define BLE_ACCEL_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_accel_on_conn_state_cb_t)(bool connected);
typedef void (*ble_accel_on_sub_state_cb_t)(bool notify_enabled);

/**
 * @brief Initialize NimBLE host, GATT service/characteristic, and start advertising.
 */
esp_err_t ble_accel_init(ble_accel_on_conn_state_cb_t on_conn, ble_accel_on_sub_state_cb_t on_sub);

/**
 * @brief True if a client is connected (GAP connection established).
 */
bool ble_accel_is_connected(void);

/**
 * @brief True if the client has enabled notifications (CCCD subscribe state).
 */
bool ble_accel_is_notify_enabled(void);

/**
 * @brief Notify a 12-byte payload to the connected client.
 */
esp_err_t ble_accel_notify(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // BLE_ACCEL_H
