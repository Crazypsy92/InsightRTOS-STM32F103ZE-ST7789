#include "mock_kernel.h"
#include <string.h>

static uint32_t s_mock_tick = 0U;
static uint32_t s_mock_ms = 0U;

void mock_kernel_reset(void)
{
    s_mock_tick = 0U;
    s_mock_ms = 0U;
}

void mock_kernel_set_tick(uint32_t tick)
{
    s_mock_tick = tick;
}

void mock_kernel_advance_ms(uint32_t ms)
{
    s_mock_ms += ms;
    s_mock_tick += ms / 10U; /* 100 Hz tick = 10 ms per tick */
}

uint32_t rtos_tick_get(void)
{
    return s_mock_tick;
}

uint32_t GetTick_ms(void)
{
    return s_mock_ms;
}

rtos_irq_state_t rtos_critical_enter(void)
{
    return 0U;
}

void rtos_critical_exit(rtos_irq_state_t state)
{
    (void)state;
}

rtos_status_t rtos_queue_init(rtos_queue_t *queue, void *buffer,
                             size_t buffer_bytes, size_t item_size, uint32_t capacity)
{
    if (queue == NULL || buffer == NULL || item_size == 0U || capacity == 0U) {
        return RTOS_ERR_ARGUMENT;
    }
    if (buffer_bytes < item_size * capacity) {
        return RTOS_ERR_ARGUMENT;
    }
    queue->buffer = (uint8_t *)buffer;
    queue->buffer_bytes = buffer_bytes;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->count = 0U;
    queue->head = 0U;
    queue->tail = 0U;
    queue->initialized = true;
    queue->send_wait_head = NULL;
    queue->receive_wait_head = NULL;
    return RTOS_OK;
}

rtos_status_t rtos_queue_send(rtos_queue_t *queue, const void *item, uint32_t timeout_ticks)
{
    (void)timeout_ticks;
    if (queue == NULL || !queue->initialized || item == NULL) {
        return RTOS_ERR_ARGUMENT;
    }
    if (queue->count >= queue->capacity) {
        return RTOS_ERR_FULL;
    }
    uint8_t *slot = queue->buffer + (queue->tail * queue->item_size);
    memcpy(slot, item, queue->item_size);
    queue->tail = (queue->tail + 1U) % queue->capacity;
    queue->count++;
    return RTOS_OK;
}

rtos_status_t rtos_queue_send_from_isr(rtos_queue_t *queue, const void *item)
{
    return rtos_queue_send(queue, item, 0U);
}

rtos_status_t rtos_queue_receive(rtos_queue_t *queue, void *item, uint32_t timeout_ticks)
{
    (void)timeout_ticks;
    if (queue == NULL || !queue->initialized || item == NULL) {
        return RTOS_ERR_ARGUMENT;
    }
    if (queue->count == 0U) {
        return RTOS_ERR_UNAVAILABLE;
    }
    const uint8_t *slot = queue->buffer + (queue->head * queue->item_size);
    memcpy(item, slot, queue->item_size);
    queue->head = (queue->head + 1U) % queue->capacity;
    queue->count--;
    return RTOS_OK;
}
