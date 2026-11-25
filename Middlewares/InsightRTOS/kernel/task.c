#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diag_rt_internal.h"
#include "kernel_internal.h"
#include "port.h"
#include "ready_queue.h"
#include "task_lifecycle_internal.h"
#include "rtos.h"
#include "wait_internal.h"

static rtos_task_t task_pool[RTOS_MAX_TASKS];
static size_t task_count;
/* Identity is separate from the frozen 72-byte ARM TCB. Never wrap. */
static uint32_t task_generation[RTOS_MAX_TASKS];
static bool task_stop[RTOS_MAX_TASKS];

bool rtos_task_reclaimable_locked(const rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    return task != NULL && task != rtos_current_task &&
        task->state == RTOS_TASK_TERMINATED && !task->ready_queued &&
        !task->delay_queued && task->next == NULL && task->delay_next == NULL &&
        task->wait_next == NULL && task->wait_object == NULL &&
        task->wait_reason == RTOS_WAIT_NONE &&
        task->wait_result == RTOS_WAIT_RESULT_NONE &&
        task->owned_mutexes == NULL && task->wait_buffer.receive == NULL &&
        task->effective_priority == task->base_priority;
}

bool rtos_task_ref_capture_locked(const rtos_task_t *task, rtos_task_ref_t *ref)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && ref != NULL);
    for (size_t i = 0U; i < task_count; ++i) {
        if (task == &task_pool[i] && task->state != RTOS_TASK_UNUSED) {
            ref->slot = (uint32_t)i; ref->generation = task_generation[i];
            return true;
        }
    }
    ref->slot = RTOS_MAX_TASKS; ref->generation = 0U;
    return false;
}

rtos_task_t *rtos_task_ref_resolve_locked(rtos_task_ref_t ref)
{
    RTOS_ASSERT(rtos_port_interrupts_masked());
    if (ref.slot >= task_count || ref.generation == 0U ||
        ref.generation != task_generation[ref.slot] ||
        task_pool[ref.slot].state == RTOS_TASK_UNUSED) return NULL;
    return &task_pool[ref.slot];
}

#if RTOS_TESTING
void rtos_task_test_generation_locked(rtos_task_t *task, uint32_t generation)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && generation != 0U);
    for (size_t i = 0U; i < task_count; ++i) {
        if (task == &task_pool[i]) {
            RTOS_ASSERT(rtos_task_reclaimable_locked(task));
            task_generation[i] = generation; return;
        }
    }
    RTOS_ASSERT(false);
}
#endif

static bool stack_range(uint32_t *stack, size_t words,
                        uintptr_t *begin, uintptr_t *end)
{
    if ((stack == NULL) || (words < RTOS_MIN_STACK_WORDS) ||
        (words > (SIZE_MAX / sizeof(*stack)))) {
        return false;
    }

    *begin = (uintptr_t)stack;
    const size_t bytes = words * sizeof(*stack);
    if (((*begin % RTOS_STACK_ALIGNMENT) != 0U) ||
        (bytes > UINTPTR_MAX - *begin)) {
        return false;
    }
    *end = *begin + bytes;
    return true;
}

/* Masked selection only; no mutation before all failure checks succeed. */
static rtos_status_t select_slot(uintptr_t begin, uintptr_t end, size_t *slot)
{
    *slot = RTOS_MAX_TASKS;
    if (rtos_sched_stack_overlaps(begin, end)) return RTOS_ERR_ARGUMENT;
    for (size_t i = 0U; i < task_count; ++i) {
        const rtos_task_t *task = &task_pool[i];
        const uintptr_t other_begin = (uintptr_t)task->stack_base;
        const uintptr_t other_end = other_begin + task->stack_words * sizeof(uint32_t);
        if (begin < other_end && other_begin < end) {
            if (*slot != RTOS_MAX_TASKS || !rtos_task_reclaimable_locked(task) ||
                task_generation[i] == UINT32_MAX) return RTOS_ERR_ARGUMENT;
            *slot = i;
        }
    }
    if (*slot != RTOS_MAX_TASKS) return RTOS_OK;
    if (task_count < RTOS_MAX_TASKS) { *slot = task_count; return RTOS_OK; }
    for (size_t i = 0U; i < task_count; ++i) {
        if (task_generation[i] != UINT32_MAX &&
            rtos_task_reclaimable_locked(&task_pool[i])) {
            *slot = i; return RTOS_OK;
        }
    }
    return RTOS_ERR_CAPACITY;
}

bool rtos_task_stack_valid(const rtos_task_t *task)
{
    if ((task == NULL) || (task->stack_base == NULL) || (task->sp == NULL)) {
        return false;
    }
    const uintptr_t begin = (uintptr_t)task->stack_base;
    const uintptr_t end = begin + task->stack_words * sizeof(uint32_t);
    const uintptr_t saved_sp = (uintptr_t)task->sp;
    return (task->stack_base[0] == RTOS_STACK_GUARD) &&
           (saved_sp >= begin + sizeof(uint32_t)) && (saved_sp < end) &&
           ((saved_sp % RTOS_STACK_ALIGNMENT) == 0U) &&
           rtos_port_context_valid(task);
}

static rtos_status_t task_create_common(rtos_task_t **out, rtos_task_ref_t *ref,
    const char *name, rtos_task_entry_t entry, void *arg,
    uint32_t *stack, size_t words, uint8_t priority)
{
    if (!rtos_port_task_create_context()) {
        return RTOS_ERR_CONTEXT;
    }

    uintptr_t begin = 0U;
    uintptr_t end = 0U;
    if ((name == NULL) || (name[0] == '\0') || (entry == NULL) ||
        (priority > RTOS_MAX_PRIORITY) ||
        !stack_range(stack, words, &begin, &end)) {
        return RTOS_ERR_ARGUMENT;
    }

    const rtos_irq_state_t irq_state = rtos_critical_enter();
    if (rtos_sched_started() && (rtos_current_task == NULL ||
        rtos_current_task->state != RTOS_TASK_RUNNING)) {
        rtos_critical_exit(irq_state);
        return RTOS_ERR_STATE;
    }
    size_t slot;
    const rtos_status_t selected = select_slot(begin, end, &slot);
    if (selected != RTOS_OK) {
        rtos_critical_exit(irq_state);
        return selected;
    }
    rtos_task_t *task = &task_pool[slot];
    if (task->state == RTOS_TASK_TERMINATED) { RTOS_TRACE(RTOS_TRACE_TASK_RECLAIM, task, 0U, 0U); } ++task_generation[slot];
    task_stop[slot] = false;
    /* Explicit member stores keep freestanding builds independent of the
     * compiler's aggregate-zeroing memset/memcpy runtime helpers. */
    task->sp = NULL;
    task->id = (uint32_t)slot + UINT32_C(1);
    task->name = name;
    task->stack_base = stack;
    task->stack_words = words;
    task->base_priority = priority;
    task->effective_priority = priority;
    task->state = RTOS_TASK_UNUSED;
    task->entry = entry;
    task->arg = arg;
    task->next = NULL;
    task->slice_remaining = RTOS_TIME_SLICE_TICKS;
    task->delay_next = NULL;
    task->wake_tick = 0U;
    task->delay_queued = false;
    task->ready_queued = false;
    task->wait_next = NULL;
    task->wait_object = NULL;
    task->wait_reason = RTOS_WAIT_NONE;
    task->wait_result = RTOS_WAIT_RESULT_NONE;
    task->owned_mutexes = NULL;
    task->wait_buffer.receive = NULL;
    /* Reserve ownership before enabling IRQs: another task may create a task
     * while this stack is filled. RESERVED uses UNUSED and is never queued. */
    if (slot == task_count) ++task_count;
    rtos_critical_exit(irq_state);

    uint32_t *const initial_sp = rtos_port_stack_init(stack, words, entry, arg);

    const rtos_irq_state_t publish_state = rtos_critical_enter();
    task->sp = initial_sp;
    RTOS_ASSERT(rtos_task_stack_valid(task));
    /* Commit identity before READY, while both words are protected by PRIMASK. */
    if (out != NULL) *out = task;
    if (ref != NULL) { ref->slot = (uint32_t)slot; ref->generation = task_generation[slot]; }
    rtos_diag_rt_task_init_hook((uint32_t)slot, task_generation[slot]);
    RTOS_TRACE(RTOS_TRACE_TASK_CREATE, task, task->base_priority, 0U); task->state = RTOS_TASK_READY; RTOS_TRACE(RTOS_TRACE_TASK_READY, task, 0U, 0U); rtos_diag_rt_task_ready_hook(task);
    rtos_ready_queue_push(task);
    rtos_sched_task_ready(task);
    rtos_critical_exit(publish_state);
    return RTOS_OK;
}

rtos_status_t rtos_task_create(rtos_task_t **out, const char *name,
    rtos_task_entry_t entry, void *arg, uint32_t *stack, size_t words, uint8_t priority)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    *out = NULL;
    return task_create_common(out, NULL, name, entry, arg, stack, words, priority);
}

rtos_status_t rtos_task_create_with_ref(rtos_task_ref_t *out, const char *name,
    rtos_task_entry_t entry, void *arg, uint32_t *stack, size_t words, uint8_t priority)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    return task_create_common(NULL, out, name, entry, arg, stack, words, priority);
}

static bool task_published(const rtos_task_t *task)
{
    return (task->state == RTOS_TASK_READY || task->state == RTOS_TASK_RUNNING ||
            task->state == RTOS_TASK_BLOCKED || task->state == RTOS_TASK_TERMINATED) &&
        task->base_priority <= RTOS_MAX_PRIORITY &&
        task->effective_priority >= task->base_priority &&
        task->effective_priority <= RTOS_MAX_PRIORITY;
}

rtos_status_t rtos_task_self_ref(rtos_task_ref_t *out)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    const rtos_task_t *task = rtos_current_task;
    if (!rtos_sched_started() || task == NULL || task->id == RTOS_IDLE_TASK_ID ||
        task->id > task_count || task != &task_pool[task->id - 1U] ||
        task->state != RTOS_TASK_RUNNING) {
        rtos_critical_exit(irq); return RTOS_ERR_STATE;
    }
    out->slot = task->id - 1U;
    out->generation = task_generation[out->slot];
    rtos_critical_exit(irq); return RTOS_OK;
}

static rtos_status_t task_ref_validate_locked(rtos_task_ref_t ref)
{
    if (ref.slot >= RTOS_MAX_TASKS || ref.generation == 0U || ref.slot >= task_count)
        return RTOS_ERR_ARGUMENT;
    if (ref.generation != task_generation[ref.slot]) return RTOS_ERR_STALE;
    return task_published(&task_pool[ref.slot]) ? RTOS_OK : RTOS_ERR_STATE;
}

rtos_status_t rtos_task_cancel_wait(rtos_task_ref_t target)
{
    if (rtos_port_in_handler() || rtos_port_in_unmaskable_handler() ||
        rtos_port_interrupts_masked()) return RTOS_ERR_CONTEXT;
    if (!rtos_sched_started() || rtos_current_task == NULL ||
        rtos_current_task->id == RTOS_IDLE_TASK_ID ||
        rtos_current_task->state != RTOS_TASK_RUNNING) return RTOS_ERR_STATE;
    const rtos_irq_state_t irq = rtos_critical_enter();
    rtos_status_t status = task_ref_validate_locked(target);
    if (status == RTOS_OK && !rtos_wait_complete_locked(&task_pool[target.slot],
            RTOS_WAIT_RESULT_CANCELLED)) status = RTOS_ERR_STATE;
    rtos_critical_exit(irq);
    return status;
}

rtos_status_t rtos_task_get_snapshot(rtos_task_ref_t ref, rtos_task_snapshot_t *out)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    const rtos_status_t status = task_ref_validate_locked(ref);
    if (status == RTOS_OK) {
        const rtos_task_t *task = &task_pool[ref.slot];
        out->identity.slot = ref.slot; out->identity.generation = ref.generation;
        out->state = task->state; out->base_priority = task->base_priority;
        out->effective_priority = task->effective_priority;
    }
    rtos_critical_exit(irq); return status;
}

rtos_task_t *rtos_task_current(void)
{
    return rtos_current_task;
}

/* Identity and STOP belong to the same reserved/published slot generation. */
bool rtos_task_stop_pending_locked(const rtos_task_t *task)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && task != NULL);
    RTOS_ASSERT(task->id != RTOS_IDLE_TASK_ID && task->id <= task_count);
    const size_t slot = task->id - 1U;
    RTOS_ASSERT(task == &task_pool[slot]);
    return task_stop[slot];
}

rtos_status_t rtos_task_request_stop(rtos_task_ref_t target)
{
    if (rtos_port_in_handler() || rtos_port_in_unmaskable_handler() ||
        rtos_port_interrupts_masked()) return RTOS_ERR_CONTEXT;
    if (!rtos_sched_started() || rtos_current_task == NULL ||
        rtos_current_task->id == RTOS_IDLE_TASK_ID ||
        rtos_current_task->state != RTOS_TASK_RUNNING) return RTOS_ERR_STATE;
    const rtos_irq_state_t irq = rtos_critical_enter();
    rtos_status_t status = task_ref_validate_locked(target);
    if (status == RTOS_OK) {
        rtos_task_t *const task = &task_pool[target.slot];
        if (task->state == RTOS_TASK_TERMINATED) status = RTOS_ERR_STATE;
        else {
            if (!task_stop[target.slot]) { RTOS_TRACE(RTOS_TRACE_STOP_REQUEST, task, 0U, 0U); } task_stop[target.slot] = true;
            (void)rtos_wait_complete_locked(task, RTOS_WAIT_RESULT_CANCELLED);
        }
    }
    /* STOP and optional completion are committed before the first unmask. */
    rtos_critical_exit(irq);
    return status;
}

rtos_status_t rtos_task_stop_requested(bool *out_requested)
{
    if (out_requested == NULL) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const rtos_irq_state_t irq = rtos_critical_enter();
    const rtos_task_t *const task = rtos_current_task;
    if (!rtos_sched_started() || task == NULL || task->id == RTOS_IDLE_TASK_ID ||
        task->state != RTOS_TASK_RUNNING) {
        rtos_critical_exit(irq); return RTOS_ERR_STATE;
    }
    *out_requested = rtos_task_stop_pending_locked(task);
    rtos_critical_exit(irq);
    return RTOS_OK;
}

_Noreturn void rtos_task_exit(void)
{
    RTOS_ASSERT(!rtos_port_in_handler());
    RTOS_ASSERT(!rtos_port_interrupts_masked());
    RTOS_ASSERT(rtos_sched_started());
    RTOS_ASSERT(rtos_current_task != NULL);
    RTOS_ASSERT(rtos_current_task->id != RTOS_IDLE_TASK_ID);
    RTOS_ASSERT(rtos_current_task->state == RTOS_TASK_RUNNING);

    const rtos_irq_state_t irq_state = rtos_critical_enter();
    RTOS_ASSERT(!rtos_current_task->ready_queued);
    RTOS_ASSERT(!rtos_current_task->delay_queued);
    RTOS_ASSERT(rtos_current_task->wait_object == NULL);
    RTOS_ASSERT(rtos_current_task->wait_next == NULL);
    RTOS_ASSERT(rtos_current_task->wait_reason == RTOS_WAIT_NONE);
    RTOS_ASSERT(rtos_current_task->wait_result == RTOS_WAIT_RESULT_NONE);
    RTOS_ASSERT(rtos_current_task->owned_mutexes == NULL);
    RTOS_ASSERT(rtos_current_task->wait_buffer.receive == NULL);
    RTOS_ASSERT(rtos_current_task->next == NULL);
    RTOS_ASSERT(rtos_current_task->delay_next == NULL);
    RTOS_ASSERT(rtos_current_task->effective_priority == rtos_current_task->base_priority);
    rtos_diag_rt_task_exit_hook(rtos_current_task);
    rtos_current_task->state = RTOS_TASK_TERMINATED; RTOS_TRACE(RTOS_TRACE_TASK_TERMINATED, rtos_current_task, 0U, 0U);
    rtos_sched_request(RTOS_SCHED_REASON_EXIT);
    rtos_critical_exit(irq_state);
    rtos_assert_fail("A terminated task was resumed", __FILE__,
                     (unsigned int)__LINE__);
}

size_t rtos_task_snapshot_locked(rtos_task_t **tasks)
{
    RTOS_ASSERT(rtos_port_interrupts_masked() && tasks != NULL);
    size_t live = 0U;
    for (size_t i = 0U; i < task_count; ++i) {
        if (task_pool[i].state != RTOS_TASK_UNUSED && task_pool[i].state != RTOS_TASK_TERMINATED) {
            tasks[live++] = &task_pool[i];
        }
    }
    return live;
}

#if INSIGHT_TRACE_ENABLED
uint32_t rtos_trace_generation_locked(const rtos_task_t *task)
{
    if (task == NULL || task->id == 0U || task->id > task_count) return 0U;
    const uint32_t slot = task->id - 1U;
    return task == &task_pool[slot] ? task_generation[slot] : 0U;
}
#endif

uint32_t rtos_task_slot_generation_locked(uint16_t slot)
{
    if ((size_t)slot < task_count &&
        task_pool[slot].state != RTOS_TASK_UNUSED &&
        task_pool[slot].state != RTOS_TASK_TERMINATED) {
        return task_generation[slot];
    }
    return 0U;
}

