#include "trace_internal.h"
#if INSIGHT_TRACE_ENABLED
#include "port.h"

static rtos_trace_record_t trace_records[RTOS_TRACE_CAPACITY];
static uint32_t trace_write_index, trace_count, trace_next_sequence, trace_overwritten;
_Static_assert(sizeof(trace_records) == 3072U, "Trace RAM budget");

static uint16_t slot_of(const rtos_task_t *task, uint32_t generation)
{
    if (task == NULL) return RTOS_TRACE_UNKNOWN_SLOT;
    if (task->id == RTOS_IDLE_TASK_ID) return RTOS_TRACE_IDLE_SLOT;
    return generation != 0U ? (uint16_t)(task->id - 1U) : RTOS_TRACE_UNKNOWN_SLOT;
}
void rtos_trace_emit_locked(rtos_trace_event_t event, const rtos_task_t *task,
                            uint32_t arg0, uint32_t arg1)
{
    /* Invalid internal requests cannot alter kernel behavior or consume seq. */
    if (event < RTOS_TRACE_TASK_CREATE || event > RTOS_TRACE_TASK_RECLAIM) return;
    const uint32_t generation = rtos_trace_generation_locked(task);
    rtos_trace_record_t *const r = &trace_records[trace_write_index];
    r->sequence = trace_next_sequence;
    r->tick = rtos_trace_tick_locked();
    r->generation = generation;
    r->arg0 = arg0; r->arg1 = arg1;
    r->slot = slot_of(task, generation); r->event_type = (uint16_t)event;
    /* Commit occurs under the caller's PRIMASK, before any reader can enter. */
    trace_write_index = (trace_write_index + 1U) % RTOS_TRACE_CAPACITY;
    ++trace_next_sequence;
    if (trace_count < RTOS_TRACE_CAPACITY) ++trace_count;
    else if (trace_overwritten != UINT32_MAX) ++trace_overwritten;
}
void rtos_trace_select_locked(const rtos_task_t *previous, const rtos_task_t *next, uint32_t reasons)
{
    const uint32_t generation = rtos_trace_generation_locked(previous);
    rtos_trace_emit_locked(RTOS_TRACE_SCHED_SELECT, next, generation,
        (reasons << 16) | slot_of(previous, generation));
}
rtos_status_t rtos_trace_get_stats(rtos_trace_stats_t *out)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const uint32_t saved = rtos_port_irq_save();
    out->count = trace_count; out->capacity = RTOS_TRACE_CAPACITY;
    out->oldest_sequence = trace_next_sequence - trace_count;
    out->next_sequence = trace_next_sequence; out->overwritten = trace_overwritten;
    rtos_port_irq_restore(saved);
    return RTOS_OK;
}
rtos_status_t rtos_trace_read(uint32_t first_sequence, rtos_trace_record_t *out,
                             uint32_t capacity, uint32_t *copied)
{
    if (out == NULL || copied == NULL || capacity == 0U || capacity > RTOS_TRACE_BATCH_MAX)
        return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const uint32_t saved = rtos_port_irq_save();
    const uint32_t offset = first_sequence - (trace_next_sequence - trace_count);
    if (offset > trace_count) { rtos_port_irq_restore(saved); return RTOS_ERR_STALE; }
    uint32_t n = trace_count - offset;
    if (n > capacity) n = capacity;
    uint32_t index = (trace_write_index + RTOS_TRACE_CAPACITY - trace_count + offset) % RTOS_TRACE_CAPACITY;
    for (uint32_t i = 0U; i < n; ++i) {
        out[i] = trace_records[index];
        index = (index + 1U) % RTOS_TRACE_CAPACITY;
    }
    *copied = n;
    rtos_port_irq_restore(saved);
    return RTOS_OK;
}
#if RTOS_TESTING
void rtos_trace_test_reset_locked(uint32_t next_sequence)
{
    trace_write_index = 0U; trace_count = 0U;
    trace_next_sequence = next_sequence; trace_overwritten = 0U;
}
#endif
#endif
