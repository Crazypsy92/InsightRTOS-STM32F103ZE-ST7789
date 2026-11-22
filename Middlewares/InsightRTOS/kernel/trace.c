#include "trace_internal.h"
#if INSIGHT_TRACE_ENABLED
#include "port.h"
#if INSIGHT_DIAG_EXPLAIN
#include <stdio.h>
#include "kernel_internal.h"
#endif

static rtos_trace_record_t trace_records[RTOS_TRACE_CAPACITY];
static uint32_t trace_write_index, trace_count, trace_next_sequence, trace_overwritten;
#if INSIGHT_DIAG_EXPLAIN
static uint32_t trace_next_sequence_high;
static uint32_t trace_status;
#endif
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
    if (event < RTOS_TRACE_TASK_CREATE || event > RTOS_TRACE_EVENT_MAX) return;
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
#if INSIGHT_DIAG_EXPLAIN
    if (trace_next_sequence == 0U) {
        if (trace_next_sequence_high == UINT32_MAX) {
            trace_status |= 1U;
        } else {
            ++trace_next_sequence_high;
        }
    }
#endif
    if (trace_count < RTOS_TRACE_CAPACITY) ++trace_count;
    else if (trace_overwritten != UINT32_MAX) ++trace_overwritten;
}
void rtos_trace_select_locked(const rtos_task_t *previous, const rtos_task_t *next, uint32_t reasons)
{
    const uint32_t generation = rtos_trace_generation_locked(previous);
    rtos_trace_emit_locked(RTOS_TRACE_SCHED_SELECT, next, generation,
        (reasons << 16) | slot_of(previous, generation));
}

#if INSIGHT_DIAG_EXPLAIN
void rtos_trace_context_switch_locked(const rtos_task_t *previous, const rtos_task_t *next,
                                      uint32_t reasons, bool prev_was_running)
{
    if (previous == NULL || next == NULL || previous == next) return;

    rtos_commit_class_t commit_class;
    if (previous->id == RTOS_IDLE_TASK_ID || next->id == RTOS_IDLE_TASK_ID) {
        commit_class = RTOS_COMMIT_IDLE_TRANSITION;
    } else if ((reasons & RTOS_SCHED_REASON_EXIT) != 0U) {
        commit_class = RTOS_COMMIT_EXIT;
    } else if ((reasons & RTOS_SCHED_REASON_BLOCK) != 0U) {
        commit_class = RTOS_COMMIT_BLOCK_OR_WAKE_OTHER;
    } else {
        const bool has_higher = (reasons & RTOS_SCHED_REASON_HIGHER) != 0U;
        const bool has_yield = (reasons & RTOS_SCHED_REASON_YIELD) != 0U;
        const bool has_slice = (reasons & RTOS_SCHED_REASON_SLICE) != 0U;
        const bool has_wake = (reasons & RTOS_SCHED_REASON_WAKE) != 0U;

        if (has_higher && (has_yield || has_slice)) {
            commit_class = RTOS_COMMIT_MIXED;
        } else if (has_higher) {
            if (next->effective_priority > previous->effective_priority && prev_was_running) {
                commit_class = RTOS_COMMIT_HIGHER_PREEMPT;
            } else if (has_wake) {
                commit_class = RTOS_COMMIT_BLOCK_OR_WAKE_OTHER;
            } else {
                commit_class = RTOS_COMMIT_MIXED;
            }
        } else if (has_yield && has_slice) {
            commit_class = RTOS_COMMIT_MIXED;
        } else if (has_yield) {
            commit_class = RTOS_COMMIT_YIELD;
        } else if (has_slice) {
            commit_class = RTOS_COMMIT_SLICE_ROTATION;
        } else if (has_wake) {
            commit_class = RTOS_COMMIT_BLOCK_OR_WAKE_OTHER;
        } else {
            commit_class = RTOS_COMMIT_OTHER;
        }
    }

    const uint32_t prev_gen = rtos_trace_generation_locked(previous);
    const uint16_t prev_slot = slot_of(previous, prev_gen);
    const uint32_t prev_prio = (uint32_t)(previous->effective_priority & 0x1FU);
    const uint32_t next_prio = (uint32_t)(next->effective_priority & 0x1FU);
    const uint32_t arg1 = (uint32_t)prev_slot | (prev_prio << 16) | (next_prio << 21) |
                          (((uint32_t)commit_class & 0x0FU) << 26);
    rtos_trace_emit_locked(RTOS_TRACE_CONTEXT_SWITCH, next, prev_gen, arg1);
}
#endif

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

#if INSIGHT_DIAG_EXPLAIN
rtos_status_t rtos_diag_trace_info(rtos_diag_trace_info_t *out)
{
    if (out == NULL) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const uint32_t saved = rtos_port_irq_save();
    out->capacity = RTOS_TRACE_CAPACITY;
    out->count = trace_count;
    out->schema_version = RTOS_TRACE_SCHEMA_VERSION;
    out->event_set_version = RTOS_TRACE_EVENT_SET_VERSION_2;
    out->tick_hz = RTOS_TICK_HZ;
    const uint64_t next_seq = ((uint64_t)trace_next_sequence_high << 32) | trace_next_sequence;
    out->next_sequence = next_seq;
    out->oldest_sequence = next_seq - trace_count;
    out->overwritten = trace_overwritten;
    out->producer_dropped = 0U;
    out->status = trace_status;
    rtos_port_irq_restore(saved);
    return RTOS_OK;
}

rtos_status_t rtos_diag_trace_read(uint64_t first_sequence,
                                   rtos_trace_record_t *out_records,
                                   uint32_t capacity,
                                   rtos_diag_trace_batch_t *out_batch)
{
    if (out_records == NULL || out_batch == NULL || capacity == 0U || capacity > RTOS_DIAG_TRACE_BATCH_MAX)
        return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const uint32_t saved = rtos_port_irq_save();
    const uint64_t next_seq = ((uint64_t)trace_next_sequence_high << 32) | trace_next_sequence;
    const uint64_t oldest_seq = next_seq - trace_count;
    if (first_sequence > next_seq) {
        rtos_port_irq_restore(saved);
        return RTOS_ERR_ARGUMENT;
    }
    if (first_sequence < oldest_seq) {
        rtos_port_irq_restore(saved);
        return RTOS_ERR_STALE;
    }
    const uint32_t offset = (uint32_t)(first_sequence - oldest_seq);
    uint32_t n = trace_count - offset;
    if (n > capacity) n = capacity;
    uint32_t index = (trace_write_index + RTOS_TRACE_CAPACITY - trace_count + offset) % RTOS_TRACE_CAPACITY;
    for (uint32_t i = 0U; i < n; ++i) {
        out_records[i] = trace_records[index];
        index = (index + 1U) % RTOS_TRACE_CAPACITY;
    }
    out_batch->oldest_sequence = oldest_seq;
    out_batch->next_sequence = next_seq;
    out_batch->count = trace_count;
    out_batch->copied = n;
    out_batch->overwritten = trace_overwritten;
    out_batch->producer_dropped = 0U;
    out_batch->status = trace_status;
    rtos_port_irq_restore(saved);
    return RTOS_OK;
}

rtos_status_t rtos_diag_trace_read_latest(rtos_trace_record_t *out_records,
                                          uint32_t capacity,
                                          rtos_diag_trace_batch_t *out_batch)
{
    if (out_records == NULL || out_batch == NULL || capacity == 0U || capacity > RTOS_DIAG_TRACE_BATCH_MAX)
        return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;
    const uint32_t saved = rtos_port_irq_save();
    const uint64_t next_seq = ((uint64_t)trace_next_sequence_high << 32) | trace_next_sequence;
    const uint32_t to_read = trace_count < capacity ? trace_count : capacity;
    const uint64_t first_seq = next_seq - to_read;
    const uint64_t oldest_seq = next_seq - trace_count;
    const uint32_t offset = (uint32_t)(first_seq - oldest_seq);
    uint32_t n = trace_count - offset;
    if (n > capacity) n = capacity;
    uint32_t index = (trace_write_index + RTOS_TRACE_CAPACITY - trace_count + offset) % RTOS_TRACE_CAPACITY;
    for (uint32_t i = 0U; i < n; ++i) {
        out_records[i] = trace_records[index];
        index = (index + 1U) % RTOS_TRACE_CAPACITY;
    }
    out_batch->oldest_sequence = oldest_seq;
    out_batch->next_sequence = next_seq;
    out_batch->count = trace_count;
    out_batch->copied = n;
    out_batch->overwritten = trace_overwritten;
    out_batch->producer_dropped = 0U;
    out_batch->status = trace_status;
    rtos_port_irq_restore(saved);
    return RTOS_OK;
}

rtos_status_t rtos_diag_explain_record(const rtos_trace_record_t *record,
                                       rtos_diag_explain_t *out_explain)
{
    if (record == NULL || out_explain == NULL) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;

    rtos_diag_explain_t exp;
    for (size_t i = 0U; i < sizeof(exp); ++i) {
        ((uint8_t *)&exp)[i] = 0U;
    }

    exp.kind = (rtos_explain_kind_t)record->event_type;
    exp.primary_slot = record->slot;
    exp.primary_generation = record->generation;

    switch (record->event_type) {
    case RTOS_TRACE_TASK_CREATE:
    case RTOS_TRACE_TASK_READY:
    case RTOS_TRACE_STOP_REQUEST:
    case RTOS_TRACE_TASK_TERMINATED:
    case RTOS_TRACE_TASK_RECLAIM:
        break;
    case RTOS_TRACE_TASK_BLOCK:
        exp.aux_value = record->arg1; /* wake_tick */
        exp.reason_flags = record->arg0; /* wait_reason */
        break;
    case RTOS_TRACE_SCHED_SELECT:
        exp.related_generation = record->arg0;
        exp.related_slot = (uint16_t)(record->arg1 & 0xFFFFU);
        exp.reason_flags = (record->arg1 >> 16);
        break;
    case RTOS_TRACE_WAIT_COMPLETE:
        exp.aux_value = record->arg0; /* wait_result */
        exp.reason_flags = record->arg1; /* wait_reason */
        break;
    case RTOS_TRACE_PRIORITY_CHANGE:
        exp.previous_priority = (uint8_t)(record->arg0 & 0x1FU);
        exp.next_priority = (uint8_t)(record->arg1 & 0x1FU);
        break;
    case RTOS_TRACE_DELAY_EXPIRED:
        exp.aux_value = record->arg0; /* wake_tick */
        break;
    case RTOS_TRACE_TASK_YIELD:
        exp.reason_flags = record->arg0; /* YIELD reason bit */
        break;
    case RTOS_TRACE_CONTEXT_SWITCH:
        exp.related_generation = record->arg0;
        exp.related_slot = (uint16_t)(record->arg1 & 0xFFFFU);
        exp.previous_priority = (uint8_t)((record->arg1 >> 16) & 0x1FU);
        exp.next_priority = (uint8_t)((record->arg1 >> 21) & 0x1FU);
        exp.commit_class = (rtos_commit_class_t)((record->arg1 >> 26) & 0x0FU);
        break;
    default:
        return RTOS_ERR_ARGUMENT;
    }

    const uint32_t saved = rtos_port_irq_save();
    if (exp.primary_slot < RTOS_TRACE_UNKNOWN_SLOT) {
        if (exp.primary_slot == RTOS_TRACE_IDLE_SLOT) {
            exp.primary_stale = false;
        } else {
            const uint32_t cur_gen = rtos_task_slot_generation_locked(exp.primary_slot);
            exp.primary_stale = (cur_gen != exp.primary_generation);
        }
    }
    if (exp.kind == RTOS_EXPLAIN_SCHED_SELECT || exp.kind == RTOS_EXPLAIN_CONTEXT_SWITCH) {
        if (exp.related_slot < RTOS_TRACE_UNKNOWN_SLOT) {
            if (exp.related_slot == RTOS_TRACE_IDLE_SLOT) {
                exp.related_stale = false;
            } else {
                const uint32_t cur_gen = rtos_task_slot_generation_locked(exp.related_slot);
                exp.related_stale = (cur_gen != exp.related_generation);
            }
        }
    }
    rtos_port_irq_restore(saved);

    *out_explain = exp;
    return RTOS_OK;
}

static const char *commit_class_name(rtos_commit_class_t c)
{
    switch (c) {
    case RTOS_COMMIT_HIGHER_PREEMPT: return "HIGHER_PREEMPT";
    case RTOS_COMMIT_YIELD: return "YIELD";
    case RTOS_COMMIT_SLICE_ROTATION: return "SLICE_ROTATION";
    case RTOS_COMMIT_BLOCK_OR_WAKE_OTHER: return "BLOCK_OR_WAKE_OTHER";
    case RTOS_COMMIT_EXIT: return "EXIT";
    case RTOS_COMMIT_IDLE_TRANSITION: return "IDLE_TRANSITION";
    case RTOS_COMMIT_MIXED: return "MIXED";
    default: return "OTHER";
    }
}

static void append_str(char *buf, size_t cap, size_t *pos, const char *s)
{
    if (buf == NULL || cap == 0U || pos == NULL || s == NULL) return;
    while (*s != '\0') {
        if (*pos + 1U < cap) {
            buf[*pos] = *s;
            ++(*pos);
        }
        ++s;
    }
    buf[*pos < cap ? *pos : cap - 1U] = '\0';
}

static void append_u32(char *buf, size_t cap, size_t *pos, uint32_t val)
{
    char tmp[12];
    uint32_t len = 0U;
    if (val == 0U) {
        tmp[len++] = '0';
    } else {
        while (val > 0U) {
            tmp[len++] = (char)('0' + (val % 10U));
            val /= 10U;
        }
    }
    char rev[12];
    for (uint32_t i = 0U; i < len; ++i) {
        rev[i] = tmp[len - 1U - i];
    }
    rev[len] = '\0';
    append_str(buf, cap, pos, rev);
}

static void append_hex(char *buf, size_t cap, size_t *pos, uint32_t val)
{
    append_str(buf, cap, pos, "0x");
    char tmp[9];
    uint32_t len = 0U;
    if (val == 0U) {
        tmp[len++] = '0';
    } else {
        while (val > 0U) {
            const uint32_t nibble = val & 0x0FU;
            tmp[len++] = (char)(nibble < 10U ? '0' + nibble : 'a' + (nibble - 10U));
            val >>= 4U;
        }
    }
    char rev[9];
    for (uint32_t i = 0U; i < len; ++i) {
        rev[i] = tmp[len - 1U - i];
    }
    rev[len] = '\0';
    append_str(buf, cap, pos, rev);
}

rtos_status_t rtos_diag_format_explanation(const rtos_diag_explain_t *explain,
                                           char *buffer,
                                           size_t buffer_size)
{
    if (explain == NULL || buffer == NULL || buffer_size == 0U) return RTOS_ERR_ARGUMENT;
    if (!rtos_port_task_create_context()) return RTOS_ERR_CONTEXT;

    buffer[0] = '\0';
    size_t pos = 0U;
    const char *const pstale = explain->primary_stale ? " (STALE)" : "";
    const char *const rstale = explain->related_stale ? " (STALE)" : "";

    switch (explain->kind) {
    case RTOS_EXPLAIN_TASK_CREATE:
        append_str(buffer, buffer_size, &pos, "Task created [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "]");
        break;
    case RTOS_EXPLAIN_TASK_READY:
        append_str(buffer, buffer_size, &pos, "Task became READY [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "]");
        break;
    case RTOS_EXPLAIN_TASK_BLOCK:
        append_str(buffer, buffer_size, &pos, "Task BLOCKED [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "] reason=");
        append_u32(buffer, buffer_size, &pos, explain->reason_flags);
        append_str(buffer, buffer_size, &pos, " wake_tick=");
        append_u32(buffer, buffer_size, &pos, explain->aux_value);
        break;
    case RTOS_EXPLAIN_SCHED_SELECT:
        append_str(buffer, buffer_size, &pos, "Scheduler select: next [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "] from [slot ");
        append_u32(buffer, buffer_size, &pos, explain->related_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->related_generation);
        append_str(buffer, buffer_size, &pos, rstale);
        append_str(buffer, buffer_size, &pos, "] reasons=");
        append_hex(buffer, buffer_size, &pos, explain->reason_flags);
        break;
    case RTOS_EXPLAIN_WAIT_COMPLETE:
        append_str(buffer, buffer_size, &pos, "Wait complete [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "] result=");
        append_u32(buffer, buffer_size, &pos, explain->aux_value);
        append_str(buffer, buffer_size, &pos, " reason=");
        append_u32(buffer, buffer_size, &pos, explain->reason_flags);
        break;
    case RTOS_EXPLAIN_PRIORITY_CHANGE:
        append_str(buffer, buffer_size, &pos, "Priority changed [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "] ");
        append_u32(buffer, buffer_size, &pos, explain->previous_priority);
        append_str(buffer, buffer_size, &pos, " -> ");
        append_u32(buffer, buffer_size, &pos, explain->next_priority);
        break;
    case RTOS_EXPLAIN_STOP_REQUEST:
        append_str(buffer, buffer_size, &pos, "Stop request [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "]");
        break;
    case RTOS_EXPLAIN_TASK_TERMINATED:
        append_str(buffer, buffer_size, &pos, "Task terminated [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "]");
        break;
    case RTOS_EXPLAIN_TASK_RECLAIM:
        append_str(buffer, buffer_size, &pos, "Task slot reclaimed [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, "]");
        break;
    case RTOS_EXPLAIN_DELAY_EXPIRED:
        append_str(buffer, buffer_size, &pos, "Delay expired [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "] wake_tick=");
        append_u32(buffer, buffer_size, &pos, explain->aux_value);
        break;
    case RTOS_EXPLAIN_TASK_YIELD:
        append_str(buffer, buffer_size, &pos, "Explicit yield [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, "] reason=");
        append_hex(buffer, buffer_size, &pos, explain->reason_flags);
        break;
    case RTOS_EXPLAIN_CONTEXT_SWITCH:
        append_str(buffer, buffer_size, &pos, "Context switch commit to [slot ");
        append_u32(buffer, buffer_size, &pos, explain->primary_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->primary_generation);
        append_str(buffer, buffer_size, &pos, pstale);
        append_str(buffer, buffer_size, &pos, ", prio ");
        append_u32(buffer, buffer_size, &pos, explain->next_priority);
        append_str(buffer, buffer_size, &pos, "] from [slot ");
        append_u32(buffer, buffer_size, &pos, explain->related_slot);
        append_str(buffer, buffer_size, &pos, ", gen ");
        append_u32(buffer, buffer_size, &pos, explain->related_generation);
        append_str(buffer, buffer_size, &pos, rstale);
        append_str(buffer, buffer_size, &pos, ", prio ");
        append_u32(buffer, buffer_size, &pos, explain->previous_priority);
        append_str(buffer, buffer_size, &pos, "] class=");
        append_str(buffer, buffer_size, &pos, commit_class_name(explain->commit_class));
        break;
    default:
        append_str(buffer, buffer_size, &pos, "Unknown event");
        break;
    }
    return RTOS_OK;
}
#endif

#if RTOS_TESTING
void rtos_trace_test_reset_locked(uint32_t next_sequence)
{
    trace_write_index = 0U; trace_count = 0U;
    trace_next_sequence = next_sequence; trace_overwritten = 0U;
#if INSIGHT_DIAG_EXPLAIN
    trace_next_sequence_high = 0U;
    trace_status = 0U;
#endif
}

#if INSIGHT_DIAG_EXPLAIN
void rtos_diag_test_reset_locked(uint64_t next_sequence)
{
    trace_write_index = 0U; trace_count = 0U;
    trace_next_sequence = (uint32_t)next_sequence;
    trace_next_sequence_high = (uint32_t)(next_sequence >> 32);
    trace_overwritten = 0U;
    trace_status = 0U;
}
#endif
#endif
#endif
