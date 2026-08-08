# ESP32-C3 MPU6050 → BLE Notifications (100 Hz)

This firmware runs on an **ESP32-C3** and streams **3-axis acceleration** from an **MPU6050** over **BLE notifications** at **100 samples/sec (100 Hz)**.

It is implemented in **ESP-IDF (not Arduino)** using the **NimBLE** host stack and the **ESP-IDF v5.x I2C master driver** (`driver/i2c_master.h`).

## Features

- **I2C (custom pins)**: SDA = GPIO **5**, SCL = GPIO **4**
- **MPU6050 config**:
  - Accelerometer range: **±2g**
  - Digital Low Pass Filter (DLPF): **~21 Hz** (CONFIG register `0x1A` = `0x04`)
  - Sample cadence: **100 Hz** (10,000 µs interval)
- **BLE**:
  - Device name: `ESP32C3_MPU6050`
  - Service UUID: `19B10000-E8F2-537E-4F6C-D104768A1214`
  - Characteristic UUID: `19B10001-E8F2-537E-4F6C-D104768A1214`
  - Properties: **READ + NOTIFY**
  - Advertising resumes automatically on disconnect
  - Notifications are sent only when the client enables CCCD (subscribe)

## Hardware Wiring

| ESP32-C3 | MPU6050 |
|---:|:---|
| 3V3 | VCC |
| GND | GND |
| GPIO5 | SDA |
| GPIO4 | SCL |

> Note: Internal pull-ups are enabled in the I2C bus config, but for best reliability (especially with longer wires), add external pull-ups (e.g., 4.7k–10k) from SDA/SCL to 3V3.

## Data Stream Format (BLE Notifications)

Each BLE notification is **exactly 12 bytes**:

- 3 × **32-bit float** (IEEE-754), **little-endian**
- Order: **x, y, z**
- Units: **m/s²**

### Byte layout

- bytes `0..3`  : `float x_ms2`
- bytes `4..7`  : `float y_ms2`
- bytes `8..11` : `float z_ms2`

### Client decode examples

Python:
```python
import struct
x, y, z = struct.unpack('<fff', payload_bytes)
```

C struct:
```c
#pragma pack(push, 1)
typedef struct {
  float x;
  float y;
  float z;
} AccelData;
#pragma pack(pop)
```

## Timing & Throughput

- Sampling cadence: **100 Hz** using `esp_timer_get_time()`.
- Notifications: one notification per sample when connected + subscribed.

## Project Layout (Firmgen standard)

- `firmware/app/app.c`
  - Orchestrates startup: creates the sample queue, initializes BLE, starts tasks.
- `firmware/platforms/esp32/`
  - `i2c_bus.c/h`: singleton I2C bus init (new `i2c_master` API)
  - `mpu6050_drv.c/h`: minimal MPU6050 driver (init + accel reads)
  - `ble_accel.c/h`: NimBLE GATT server + advertising + notification API
- `firmware/services/`
  - `sample_task.c/h`: 100 Hz sampling task → pushes `AccelData` into queue
  - `ble_notify_task.c/h`: consumes queue → notifies BLE characteristic
  - `accel_stream.c/h`: queue creation + shared type (`AccelData`)
- `firmware/configs/app_config.h`
  - All pins, UUIDs, and constants (recommended place to edit)

## Configuration

Edit these in:

- `firmware/configs/app_config.h`

Key macros:
- `APP_I2C_SDA_GPIO`, `APP_I2C_SCL_GPIO`, `APP_I2C_FREQ_HZ`
- `APP_MPU6050_I2C_ADDR`
- `APP_SAMPLE_RATE_HZ`, `APP_SAMPLE_INTERVAL_US`
- `APP_BLE_DEVICE_NAME`, `APP_BLE_SERVICE_UUID`, `APP_BLE_CHAR_UUID`

## Build

From the project directory:

```bash
idf.py build
```

## Flash & Monitor

```bash
idf.py -p <PORT> flash monitor
```

## Verifying with a BLE Client

1. Install **nRF Connect** (Android/iOS) or similar.
2. Scan and connect to `ESP32C3_MPU6050`.
3. Find service `19B10000-E8F2-537E-4F6C-D104768A1214`.
4. Enable notifications on characteristic `19B10001-E8F2-537E-4F6C-D104768A1214`.
5. You should receive **12-byte notifications** at ~100 Hz.

## Prompts / Requirements Captured From You

You explicitly asked for the firmware to implement the following (captured verbatim as requirements):

1. **Board / target**: ESP32-C3.
2. **MPU6050 over I2C**:
   - Read **3-axis acceleration**
   - I2C pins: **SDA on GPIO 5**, **SCL on GPIO 4**
   - Sampling rate: **100 Hz** ("100 sps")
   - Configure MPU6050:
     - **±2g range**
     - **~21 Hz low-pass filter bandwidth**
3. **BLE streaming**:
   - Device name: `ESP32C3_MPU6050`
   - Service UUID: `19B10000-E8F2-537E-4F6C-D104768A1214`
   - Characteristic UUID: `19B10001-E8F2-537E-4F6C-D104768A1214`
   - Characteristic properties: **READ | NOTIFY**
   - Support notifications via CCCD (subscribe/enable notifications on the client)
   - On disconnect: automatically restart advertising
4. **Binary packed payload**:
   - Exactly **12 bytes** per notification
   - You iterated through payload formats during development:
     - float32 in **g**
     - **raw** accel counts
     - final requirement: float32 in **m/s²** ("4 bytes per axis - 32 Bit float")

This firmware currently implements the FINAL payload requirement: **m/s² float32 x,y,z**.

## Notes / Known Differences vs Arduino Sketches

- This project uses **ESP-IDF NimBLE**, not Arduino BLE APIs (`BLEDevice.h`, `BLE2902`, etc.).
  Client behavior (subscribe/notify) is the same; CCCD exists as part of the notify characteristic.
- MPU6050 scaling to m/s² is done by direct-register read + constants (`16384 LSB/g` at ±2g, `9.80665 m/s² per g`).
