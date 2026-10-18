#ifndef APP_CAPTURE_H
#define APP_CAPTURE_H

#include <stdint.h>
#include "tx_api.h"
#include "stm32n6xx_hal.h"

#define APP_CAPTURE_NONE 0xffu
typedef struct {
    TX_SEMAPHORE ready;
    uint8_t *buffers[3];
    uint32_t ticks[3];
    uint32_t sequences[3];
    uint8_t active;
    uint8_t pending;
    uint8_t processing;
    volatile uint32_t frames;
    volatile uint32_t replaced;
} AppCapture;

static void app_capture_init(AppCapture *q, uint8_t **buffers)
{
    for (uint32_t i = 0; i < 3; i++) q->buffers[i] = buffers[i];
    q->active = 0;
    q->pending = APP_CAPTURE_NONE;
    q->processing = APP_CAPTURE_NONE;
    q->frames = q->replaced = 0;
    tx_semaphore_create(&q->ready, NULL, 0);
}

static void app_capture_complete(AppCapture *q, uint32_t tick, void (*set_address)(uint8_t *))
{
    uint32_t mask = __get_PRIMASK();
    uint8_t completed;
    uint8_t next;
    uint8_t had_pending;
    __disable_irq();
    completed = q->active;
    next = q->pending;
    had_pending = q->pending != APP_CAPTURE_NONE;
    if (had_pending)
    {
        q->replaced++;
    }
    else
    {
        for (next = 0; next < 3; next++)
            if (next != completed && next != q->processing) break;
    }
    /* Three buffers cover capture, a completed image and one active inference. */
    set_address(q->buffers[next]);
    q->active = next;
    q->frames++;
    q->ticks[completed] = tick;
    q->sequences[completed] = q->frames;
    q->pending = completed;
    __DMB();
    if (!had_pending) tx_semaphore_put(&q->ready);
    __set_PRIMASK(mask);
}

static uint8_t *app_capture_get(AppCapture *q, uint32_t *tick, uint32_t *sequence)
{
    for (;;)
    {
        tx_semaphore_get(&q->ready, TX_WAIT_FOREVER);
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        if (q->pending != APP_CAPTURE_NONE)
        {
            uint8_t index = q->pending;
            q->pending = APP_CAPTURE_NONE;
            q->processing = index;
            *tick = q->ticks[index];
            *sequence = q->sequences[index];
            __DMB();
            __set_PRIMASK(mask);
            return q->buffers[index];
        }
        __set_PRIMASK(mask);
    }
}

static void app_capture_release(AppCapture *q)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    q->processing = APP_CAPTURE_NONE;
    __DMB();
    __set_PRIMASK(mask);
}

#endif
