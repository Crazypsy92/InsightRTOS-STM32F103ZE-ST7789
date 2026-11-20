#include <stddef.h>
#include <stdint.h>

#include "delay_queue.h"
#include "port.h"
#include "rtos_assert.h"

static rtos_task_t *delay_head;

void rtos_delay_queue_insert(rtos_task_t *task, uint32_t now)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT(task->id != RTOS_IDLE_TASK_ID);
    RTOS_ASSERT(task->state == RTOS_TASK_BLOCKED);
    RTOS_ASSERT(!task->ready_queued);
    RTOS_ASSERT(!task->delay_queued);
    RTOS_ASSERT(task->next == NULL);
    RTOS_ASSERT(task->delay_next == NULL);
    if (task->wait_reason == RTOS_WAIT_SEMAPHORE || task->wait_reason == RTOS_WAIT_MUTEX ||
            task->wait_reason == RTOS_WAIT_QUEUE_SEND || task->wait_reason == RTOS_WAIT_QUEUE_RECEIVE) {
        RTOS_ASSERT(task->wait_object != NULL);
        RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_PENDING);
    } else {
        RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
        RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
        RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_NONE);
    }

    const uint32_t distance = task->wake_tick - now;
    RTOS_ASSERT((distance > 0U) && (distance <= RTOS_DELAY_MAX_TICKS));
    rtos_task_t **link = &delay_head;
    while (*link != NULL) {
        const rtos_task_t *const queued = *link;
        RTOS_ASSERT(queued->state == RTOS_TASK_BLOCKED);
        RTOS_ASSERT(queued->delay_queued && !queued->ready_queued);
        const uint32_t queued_distance = queued->wake_tick - now;
        RTOS_ASSERT((queued_distance > 0U) &&
                    (queued_distance <= RTOS_DELAY_MAX_TICKS));
        if (queued_distance > distance) {
            break;
        }
        /* Equal deadlines are skipped, retaining insertion FIFO order. */
        link = &(*link)->delay_next;
    }
    task->delay_next = *link;
    task->delay_queued = true;
    *link = task;
}

rtos_task_t *rtos_delay_queue_peek_due(uint32_t now)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    rtos_task_t *const task = delay_head;
    if ((task == NULL) || !rtos_tick_deadline_due(now, task->wake_tick)) {
        return NULL;
    }
    RTOS_ASSERT(task->state == RTOS_TASK_BLOCKED);
    RTOS_ASSERT(task->delay_queued && !task->ready_queued);
    return task;
}

rtos_task_t *rtos_delay_queue_pop_due(uint32_t now)
{
    rtos_task_t *const task = rtos_delay_queue_peek_due(now);
    if (task == NULL) return NULL;
    delay_head = task->delay_next;
    task->delay_next = NULL;
    task->delay_queued = false;
    return task;
}

void rtos_delay_queue_remove(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(task != NULL && task->delay_queued && !task->ready_queued);
    RTOS_ASSERT(task->state == RTOS_TASK_BLOCKED);
    rtos_task_t **link = &delay_head;
    while (*link != task) {
        RTOS_ASSERT(*link != NULL);
        link = &(*link)->delay_next;
    }
    *link = task->delay_next;
    task->delay_next = NULL;
    task->delay_queued = false;
}

bool rtos_delay_queue_empty(void)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    return delay_head == NULL;
}

#if RTOS_TESTING
rtos_task_t *rtos_delay_queue_test_head(void)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    return delay_head;
}
#endif
