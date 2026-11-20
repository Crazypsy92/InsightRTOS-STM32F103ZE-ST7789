#include <stddef.h>

#include "ready_queue.h"
#include "rtos_assert.h"
#include "port.h"

/* Static initialization gives each priority an empty FIFO at reset. */
static rtos_task_t *ready_heads[RTOS_PRIORITY_LEVELS];
static rtos_task_t *ready_tails[RTOS_PRIORITY_LEVELS];

void rtos_ready_queue_push(rtos_task_t *task)
{
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT(task->effective_priority <= RTOS_MAX_PRIORITY);
    RTOS_ASSERT(task->state == RTOS_TASK_READY);
    RTOS_ASSERT(task->next == NULL);
    RTOS_ASSERT(!task->ready_queued);
    RTOS_ASSERT(!task->delay_queued);
    RTOS_ASSERT(task->delay_next == NULL);
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_result != RTOS_WAIT_RESULT_PENDING);

    const unsigned int priority = task->effective_priority;
    RTOS_ASSERT(ready_tails[priority] != task);
    if (ready_tails[priority] == NULL) {
        RTOS_ASSERT(ready_heads[priority] == NULL);
        ready_heads[priority] = task;
    } else {
        ready_tails[priority]->next = task;
    }
    ready_tails[priority] = task;
    task->ready_queued = true;
}

void rtos_ready_queue_push_front(rtos_task_t *task)
{
    RTOS_ASSERT(task != NULL);
    RTOS_ASSERT(task->effective_priority <= RTOS_MAX_PRIORITY);
    RTOS_ASSERT(task->state == RTOS_TASK_READY);
    RTOS_ASSERT(task->next == NULL);
    RTOS_ASSERT(!task->ready_queued);
    RTOS_ASSERT(!task->delay_queued);
    RTOS_ASSERT(task->delay_next == NULL);
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_result != RTOS_WAIT_RESULT_PENDING);

    const unsigned int priority = task->effective_priority;
    RTOS_ASSERT(ready_tails[priority] != task);
    task->next = ready_heads[priority];
    ready_heads[priority] = task;
    if (ready_tails[priority] == NULL) {
        ready_tails[priority] = task;
    }
    task->ready_queued = true;
}

bool rtos_ready_queue_has_priority(uint8_t priority)
{
    RTOS_ASSERT(priority <= RTOS_MAX_PRIORITY);
    return ready_heads[priority] != NULL;
}

rtos_task_t *rtos_ready_queue_pop(void)
{
    for (unsigned int level = RTOS_PRIORITY_LEVELS; level > 0U; --level) {
        const unsigned int priority = level - 1U;
        rtos_task_t *task = ready_heads[priority];
        if (task != NULL) {
            RTOS_ASSERT(task->state == RTOS_TASK_READY);
            RTOS_ASSERT(task->ready_queued);
            RTOS_ASSERT(!task->delay_queued);
            RTOS_ASSERT(task->delay_next == NULL);
            RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL);
            RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
            RTOS_ASSERT(task->wait_result != RTOS_WAIT_RESULT_PENDING);
            ready_heads[priority] = task->next;
            if (ready_heads[priority] == NULL) {
                ready_tails[priority] = NULL;
            }
            task->next = NULL;
            task->ready_queued = false;
            return task;
        }
    }
    return NULL;
}

#if RTOS_TESTING
void rtos_ready_queue_test_roots(rtos_task_t **heads, rtos_task_t **tails)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    for (size_t i = 0U; i < RTOS_PRIORITY_LEVELS; ++i) {
        heads[i] = ready_heads[i];
        tails[i] = ready_tails[i];
    }
}
#endif

void rtos_ready_queue_remove(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(task != NULL && task->state == RTOS_TASK_READY && task->ready_queued);
    const unsigned int priority = task->effective_priority;
    RTOS_ASSERT(priority <= RTOS_MAX_PRIORITY);
    rtos_task_t **link = &ready_heads[priority];
    rtos_task_t *previous = NULL;
    while (*link != task) {
        RTOS_ASSERT(*link != NULL);
        previous = *link; link = &(*link)->next;
    }
    *link = task->next;
    if (ready_tails[priority] == task) ready_tails[priority] = previous;
    task->next = NULL; task->ready_queued = false;
}
int rtos_ready_queue_highest_priority(void)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    for (unsigned int p = RTOS_PRIORITY_LEVELS; p > 0U; --p) {
        if (ready_heads[p - 1U] != NULL) return (int)(p - 1U);
    }
    return -1;
}
