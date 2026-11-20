#ifndef INSIGHT_QUEUE_INTERNAL_H
#define INSIGHT_QUEUE_INTERNAL_H
#include "rtos_queue.h"
/* Caller masked; returns the matching event head without changing state. */
rtos_task_t **rtos_queue_wait_head_locked(rtos_task_t *task);
/* Ring relation without overflow or a power-of-two assumption. */
static inline uint32_t rtos_queue_advance(uint32_t index, uint32_t capacity)
{
    return index == capacity - 1U ? 0U : index + 1U;
}
#endif
