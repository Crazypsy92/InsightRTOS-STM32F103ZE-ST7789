#include <stddef.h>
#include "priority_internal.h"
#include "kernel_internal.h"
#include "port.h"
#include "queue_internal.h"
#include "ready_queue.h"
#include "rtos.h"

void rtos_task_set_effective_locked(rtos_task_t *task, uint8_t priority)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && task != NULL && task->id != RTOS_IDLE_TASK_ID);
    RTOS_ASSERT(priority >= task->base_priority && priority <= RTOS_MAX_PRIORITY);
    if (task->effective_priority == priority) return;
    const bool ready = task->state == RTOS_TASK_READY;
    RTOS_ASSERT(ready || task->state == RTOS_TASK_RUNNING || task->state == RTOS_TASK_BLOCKED);
    rtos_task_t **event_head = NULL;
    if (ready) rtos_ready_queue_remove(task);
    if (task->state == RTOS_TASK_BLOCKED && task->wait_reason != RTOS_WAIT_NONE) {
        if (task->wait_reason == RTOS_WAIT_SEMAPHORE) {
            rtos_semaphore_t *const sem = task->wait_object; event_head = &sem->wait_head;
        } else if (task->wait_reason == RTOS_WAIT_QUEUE_SEND || task->wait_reason == RTOS_WAIT_QUEUE_RECEIVE) {
            event_head = rtos_queue_wait_head_locked(task);
        } else {
            RTOS_ASSERT(task->wait_reason == RTOS_WAIT_MUTEX);
            rtos_mutex_t *const mutex = task->wait_object; event_head = &mutex->wait_head;
        }
        RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_PENDING);
        rtos_task_t **link = event_head;
        while (*link != task) { RTOS_ASSERT(*link != NULL); link = &(*link)->wait_next; }
        *link = task->wait_next; task->wait_next = NULL;
    }
    RTOS_TRACE(RTOS_TRACE_PRIORITY_CHANGE, task, (uint32_t)task->effective_priority | ((uint32_t)priority << 8), task->base_priority); task->effective_priority = priority;
    if (ready) rtos_ready_queue_push(task);
    if (event_head != NULL) {
        rtos_task_t **link = event_head;
        while (*link != NULL && (*link)->effective_priority >= priority) link = &(*link)->wait_next;
        task->wait_next = *link; *link = task;
    }
}
