#ifndef ACCEL_STREAM_H
#define ACCEL_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
    float x;
    float y;
    float z;
} AccelData;

_Static_assert(sizeof(AccelData) == 12, "AccelData must be 12 bytes");

/**
 * @brief Create the sample queue.
 */
QueueHandle_t accel_stream_create_queue(void);

/**
 * @brief Get the queue handle.
 */
QueueHandle_t accel_stream_get_queue(void);

#ifdef __cplusplus
}
#endif

#endif // ACCEL_STREAM_H
