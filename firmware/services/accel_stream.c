#include "accel_stream.h"

#include "app_config.h"

static QueueHandle_t s_q;

QueueHandle_t accel_stream_create_queue(void)
{
    if (s_q == NULL) {
        s_q = xQueueCreate(APP_SAMPLE_QUEUE_LEN, sizeof(AccelData));
    }
    return s_q;
}

QueueHandle_t accel_stream_get_queue(void)
{
    return s_q;
}
