#include "kernel_internal.h"
#include "memory.h"
#include "port.h"
#include "queue_internal.h"
#include "rtos.h"
#include "wait_internal.h"
#include "delay_queue.h"

static rtos_status_t queue_context(bool running)
{
    if (rtos_port_in_handler() || rtos_port_in_unmaskable_handler() ||
        rtos_port_interrupts_masked()) return RTOS_ERR_CONTEXT;
    if (running || rtos_sched_started()) {
        const rtos_task_t *const task = rtos_task_current();
        if (!rtos_sched_started() || task == NULL || task->id == RTOS_IDLE_TASK_ID ||
            task->state != RTOS_TASK_RUNNING) return RTOS_ERR_STATE;
    }
    return RTOS_OK;
}

/* Half-open address ranges, only after representability checks. This ARM/host
 * port uses flat uintptr_t addressing; callers still guarantee mapped storage. */
static bool range_valid(const void *pointer, size_t bytes)
{
    return pointer != NULL && bytes <= UINTPTR_MAX - (uintptr_t)pointer;
}
static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    return (uintptr_t)a < (uintptr_t)b + bn && (uintptr_t)b < (uintptr_t)a + an;
}
static void queue_check(const rtos_queue_t *queue)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    RTOS_ASSERT(queue->initialized && queue->buffer != NULL);
    RTOS_ASSERT(queue->item_size > 0U && queue->item_size <= RTOS_QUEUE_MAX_ITEM_SIZE);
    RTOS_ASSERT(queue->send_wait_head == NULL || queue->count == queue->capacity);
    RTOS_ASSERT(queue->receive_wait_head == NULL || queue->count == 0U);
    RTOS_ASSERT(queue->send_wait_head == NULL || queue->receive_wait_head == NULL);
    RTOS_ASSERT(queue->capacity > 0U && queue->count <= queue->capacity);
    RTOS_ASSERT(queue->head < queue->capacity && queue->tail < queue->capacity);
    RTOS_ASSERT(queue->item_size <= SIZE_MAX / queue->capacity);
    RTOS_ASSERT(queue->buffer_bytes >= queue->item_size * queue->capacity);
    const uint32_t distance = queue->tail >= queue->head ? queue->tail - queue->head :
        queue->capacity - (queue->head - queue->tail);
    RTOS_ASSERT(distance == (queue->count == queue->capacity ? 0U : queue->count));
}
rtos_status_t rtos_queue_init(rtos_queue_t *queue, void *buffer,
    size_t buffer_bytes, size_t item_size, uint32_t capacity)
{
    const rtos_status_t context = queue_context(false);
    if (context != RTOS_OK) return context;
    if (!range_valid(queue, sizeof(*queue)) || !range_valid(buffer, buffer_bytes) ||
        item_size == 0U || capacity == 0U || item_size > SIZE_MAX / capacity ||
        item_size > RTOS_QUEUE_MAX_ITEM_SIZE || buffer_bytes < item_size * capacity ||
        overlap(queue, sizeof(*queue), buffer, buffer_bytes)) return RTOS_ERR_ARGUMENT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (queue->initialized) {
        rtos_critical_exit(irq);
        return RTOS_ERR_STATE;
    }
    queue->buffer = buffer;
    queue->buffer_bytes = buffer_bytes;
    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->count = 0U;
    queue->head = 0U;
    queue->tail = 0U;
    queue->send_wait_head = NULL;
    queue->receive_wait_head = NULL;
    queue->initialized = true;
    rtos_critical_exit(irq);
    return RTOS_OK;
}

rtos_task_t **rtos_queue_wait_head_locked(rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && task != NULL);
    rtos_queue_t *const queue = task->wait_object;
    RTOS_ASSERT(queue != NULL && queue->initialized);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_QUEUE_SEND || task->wait_reason == RTOS_WAIT_QUEUE_RECEIVE);
    return task->wait_reason == RTOS_WAIT_QUEUE_SEND ? &queue->send_wait_head : &queue->receive_wait_head;
}

static bool pending_overlap(const rtos_queue_t *queue, const void *item)
{
    const rtos_task_t *heads[2] = {queue->send_wait_head, queue->receive_wait_head};
    for (size_t direction = 0U; direction < 2U; ++direction) {
        for (const rtos_task_t *t = heads[direction]; t != NULL; t = t->wait_next) {
            const void *const buffer = direction == 0U ? t->wait_buffer.send : t->wait_buffer.receive;
            RTOS_ASSERT(buffer != NULL && t->state == RTOS_TASK_BLOCKED && t->wait_result == RTOS_WAIT_RESULT_PENDING);
            if (overlap(item, queue->item_size, buffer, queue->item_size)) return true;
        }
    }
    return false;
}

static void queue_block(rtos_queue_t *queue, const void *input, void *output, bool send, uint32_t timeout)
{
    rtos_task_t *const task = rtos_task_current();
    RTOS_ASSERT(task->state == RTOS_TASK_RUNNING && task->wait_result == RTOS_WAIT_RESULT_NONE);
    RTOS_ASSERT(task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL && task->wait_buffer.receive == NULL);
    RTOS_ASSERT(!task->ready_queued && !task->delay_queued && task->next == NULL && task->delay_next == NULL);
    task->wait_reason = send ? RTOS_WAIT_QUEUE_SEND : RTOS_WAIT_QUEUE_RECEIVE;
    task->wait_object = queue;
    if (send) task->wait_buffer.send = input;
    else task->wait_buffer.receive = output;
    task->wait_result = RTOS_WAIT_RESULT_PENDING;
    rtos_task_t **link = rtos_queue_wait_head_locked(task);
    while (*link != NULL && (*link)->effective_priority >= task->effective_priority) {
        RTOS_ASSERT(*link != task);
        link = &(*link)->wait_next;
    }
    task->wait_next = *link;
    *link = task;
    task->slice_remaining = RTOS_TIME_SLICE_TICKS;
    task->state = RTOS_TASK_BLOCKED;
    if (timeout != RTOS_WAIT_FOREVER) {
        const uint32_t now = rtos_tick_get();
        task->wake_tick = now + timeout;
        rtos_delay_queue_insert(task, now);
    }
    rtos_sched_request(RTOS_SCHED_REASON_BLOCK);
}

static rtos_status_t queue_transfer(rtos_queue_t *queue, void *output,
    const void *input, uint32_t timeout, bool send)
{
    const void *const item = send ? input : output;
    if (!range_valid(queue, sizeof(*queue)) || item == NULL ||
        (timeout > RTOS_DELAY_MAX_TICKS && timeout != RTOS_WAIT_FOREVER)) return RTOS_ERR_ARGUMENT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    if (!queue->initialized) {
        rtos_critical_exit(irq);
        return RTOS_ERR_STATE;
    }
    queue_check(queue);
    if (!range_valid(item, queue->item_size) ||
        overlap(item, queue->item_size, queue->buffer, queue->buffer_bytes) ||
        overlap(item, queue->item_size, queue, sizeof(*queue)) || pending_overlap(queue, item)) {
        rtos_critical_exit(irq);
        return RTOS_ERR_ARGUMENT;
    }
    if (send && queue->receive_wait_head != NULL) {
        rtos_task_t *const receiver = queue->receive_wait_head;
        RTOS_ASSERT(queue->count == 0U);
        (void)memcpy(receiver->wait_buffer.receive, input, queue->item_size);
        RTOS_ASSERT(rtos_wait_complete_locked(receiver, RTOS_WAIT_RESULT_GRANTED));
    } else if (send ? queue->count == queue->capacity : queue->count == 0U) {
        if (timeout == RTOS_NO_WAIT) {
            rtos_critical_exit(irq);
            return send ? RTOS_ERR_FULL : RTOS_ERR_UNAVAILABLE;
        }
        rtos_task_t *const task = rtos_task_current();
        if (!rtos_wait_block_allowed_locked(task)) {
            rtos_critical_exit(irq); return RTOS_ERR_CANCELLED;
        }
        queue_block(queue, input, output, send, timeout);
        rtos_critical_exit(irq);
        /* Restore this exact call; permission/data was already committed.
         * Never retry count or retain a reference beyond completion. */
        const rtos_irq_state_t resumed = rtos_critical_enter();
        RTOS_ASSERT(rtos_task_current() == task && task->state == RTOS_TASK_RUNNING);
        RTOS_ASSERT(task->wait_result == RTOS_WAIT_RESULT_GRANTED || task->wait_result == RTOS_WAIT_RESULT_TIMEOUT || task->wait_result == RTOS_WAIT_RESULT_CANCELLED);
        RTOS_ASSERT(task->wait_object == NULL && task->wait_next == NULL && task->wait_reason == RTOS_WAIT_NONE);
        RTOS_ASSERT(task->wait_buffer.receive == NULL && !task->ready_queued && !task->delay_queued);
        const rtos_status_t result = task->wait_result == RTOS_WAIT_RESULT_GRANTED ? RTOS_OK :
            task->wait_result == RTOS_WAIT_RESULT_CANCELLED ? RTOS_ERR_CANCELLED : RTOS_ERR_TIMEOUT;
        task->wait_result = RTOS_WAIT_RESULT_NONE;
        rtos_critical_exit(resumed);
        return result;
    } else if (send) {
        (void)memcpy(queue->buffer + (size_t)queue->tail * queue->item_size, input, queue->item_size);
        queue->tail = rtos_queue_advance(queue->tail, queue->capacity);
        ++queue->count;
    } else {
        (void)memcpy(output, queue->buffer + (size_t)queue->head * queue->item_size, queue->item_size);
        queue->head = rtos_queue_advance(queue->head, queue->capacity);
        --queue->count;
        if (queue->send_wait_head != NULL) {
            rtos_task_t *const sender = queue->send_wait_head;
            (void)memcpy(queue->buffer + (size_t)queue->tail * queue->item_size,
                         sender->wait_buffer.send, queue->item_size);
            queue->tail = rtos_queue_advance(queue->tail, queue->capacity);
            ++queue->count;
            RTOS_ASSERT(rtos_wait_complete_locked(sender, RTOS_WAIT_RESULT_GRANTED));
        }
    }
    rtos_critical_exit(irq);
    return RTOS_OK;
}
rtos_status_t rtos_queue_send(rtos_queue_t *queue, const void *item, uint32_t timeout_ticks)
{
    const rtos_status_t context = queue_context(true);
    return context == RTOS_OK ? queue_transfer(queue, NULL, item, timeout_ticks, true) : context;
}
rtos_status_t rtos_queue_receive(rtos_queue_t *queue, void *item, uint32_t timeout_ticks)
{
    const rtos_status_t context = queue_context(true);
    return context == RTOS_OK ? queue_transfer(queue, item, NULL, timeout_ticks, false) : context;
}

rtos_status_t rtos_queue_send_from_isr(rtos_queue_t *queue, const void *item)
{
    if (!rtos_port_in_kernel_isr()) return RTOS_ERR_CONTEXT;
    /* Like Semaphore ISR give: current can be Idle, BLOCKED or early READY.
     * NO_WAIT makes the shared transfer core incapable of registering a wait. */
    if (!rtos_sched_started() || rtos_task_current() == NULL) return RTOS_ERR_STATE;
    return queue_transfer(queue, NULL, item, RTOS_NO_WAIT, true);
}
