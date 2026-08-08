#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Application-level compile-time constants: GPIO pins, buffer sizes, periods, etc.
   Add #define macros here; every source file that uses them includes "app_config.h". */

// I2C pin mapping (ESP32-C3)
#define APP_I2C_SDA_GPIO          5
#define APP_I2C_SCL_GPIO          4
#define APP_I2C_FREQ_HZ           400000

// MPU6050
#define APP_MPU6050_I2C_ADDR       0x68
#define APP_MPU6050_REINIT_ERRORS  50

// Sampling
#define APP_SAMPLE_RATE_HZ         100
#define APP_SAMPLE_INTERVAL_US     10000
#define APP_SAMPLE_QUEUE_LEN       10

// BLE
#define APP_BLE_DEVICE_NAME        "ESP32C3_MPU6050"
#define APP_BLE_SERVICE_UUID       "19B10000-E8F2-537E-4F6C-D104768A1214"
#define APP_BLE_CHAR_UUID          "19B10001-E8F2-537E-4F6C-D104768A1214"

// Advertised connection parameter hint (units of 1.25ms). Use as defaults only.
#define APP_BLE_CONN_ITVL_MIN      12   // 15ms
#define APP_BLE_CONN_ITVL_MAX      24   // 30ms

#endif /* APP_CONFIG_H */
