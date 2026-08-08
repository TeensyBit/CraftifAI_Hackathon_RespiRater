# MPU6050 based Respiration Rate tracker for sleep monitoring

Vidlink: https://drive.google.com/drive/folders/1QNU3sl9Tb0qz4BG5r3MqL0wRlBGx8VSz

Overview
This project presents a non-invasive telemetry and digital signal processing framework for real-time human respiratory rate estimation using a single chest-worn three-axis inertial measurement unit coupled with an ESP32 micro-controller streaming raw accelerometer data over Bluetooth Low Energy at a continuous sampling frequency of one hundred hertz. The processing pipeline directly addresses the challenge of isolating subtle, low-amplitude respiration-induced chest wall displacements from high-amplitude gravitational posture vectors, body movement artifacts, and electronic sensor jitter. Raw acceleration signals are first smoothed using a local time-domain Savitzky-Golay polynomial filter to suppress high-frequency quantization noise while strictly preserving asymmetric waveform morphology and peak amplitude integrity. Baseline drift and low-frequency motion sway are subsequently eliminated through zero-phase forward-backward second-order high-pass Butterworth digital filtering with a half-hertz cutoff threshold, preventing phase distortion and temporal peak displacement.

Respiratory frequencies are continuously estimated across sliding five-second observation windows using a dual-engine analytical framework operating in parallel. In the frequency domain, normalized single-sided real Fast Fourier Transforms isolate dominant movement energy within a bounded spectrum, while the time-domain engine executes local maxima peak prominence detection to evaluate mean inter-peak intervals. To account for multi-harmonic chest wall mechanics and sensor mounting dynamics that generate higher-order cyclic sub-movements during individual breath cycles, an empirical sub-harmonic one-to-four frequency scaling algorithm converts raw acceleration event spikes into true physiological breaths per minute. The overall architecture is embedded within an asynchronous non-blocking Python streaming engine that decouples high-frequency data ingestion, analytics execution, multi-subplot canvas rendering, and structured metadata telemetry logging.

Algo Documentation
## Configuration Parameters Reference

The system's operational thresholds and signal processing parameters are defined at the top of the streaming application. Adjust these parameters to match different sensor orientations or physical mounting constraints.

| Parameter | Default Value | Description |
| :--- | :--- | :--- |
| `FS` | `100 Hz` | IMU sampling frequency received over BLE notifications. |
| `MAX_PLOT_POINTS` | `200` | Number of raw data points rendered per subplot window ($\approx 2 \text{s}$). |
| `WINDOW_SIZE` | `500` | Rolling sample length for real-time DSP calculations ($5.0 \text{s}$). |
| `SG_WINDOW` | `31` | Window length for the Savitzky-Golay smoothing filter ($\approx 0.31 \text{s}$). |
| `SG_POLY` | `3` | Polynomial order used for local Savitzky-Golay curve fitting. |
| `HP_CUTOFF` | `0.5 Hz` | High-pass Butterworth cutoff frequency for motion drift suppression. |
| `MIN_BAND_HZ` | `0.5 Hz` | Lower boundary for valid spectral movement searches ($30 \text{ raw CPM}$). |
| `MAX_BAND_HZ` | `4.0 Hz` | Upper boundary for valid spectral movement searches ($240 \text{ raw CPM}$). |

---

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
