#include "rtos_diag.h"
#include "kernel_internal.h"
#include "port.h"
#include "rtos.h"
#include "rtos_mutex.h"

#if INSIGHT_DIAG_DL

static void dl_zero(void *dst, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0U; i < n; ++i) {
        p[i] = 0U;
    }
}

rtos_status_t rtos_diag_dl_capture(rtos_diag_dl_workspace_t *work)
{
    if (work == NULL) {
        return RTOS_ERR_ARGUMENT;
    }
    if (rtos_port_in_handler() || rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }

    dl_zero(work, sizeof(*work));

    const uint32_t saved = rtos_port_irq_save();
    work->header.capture_tick = rtos_tick_get();

    rtos_task_t *tasks[RTOS_MAX_TASKS];
    const size_t count = rtos_task_snapshot_locked(tasks);
    if (count > RTOS_MAX_TASKS) {
        rtos_port_irq_restore(saved);
        return RTOS_ERR_CAPACITY;
    }
    work->header.node_count = (uint16_t)count;

    /* First pass: capture all active tasks and their priorities/state */
    for (size_t i = 0U; i < count; ++i) {
        const rtos_task_t *const task = tasks[i];
        const uint16_t slot = (uint16_t)(task->id - 1U);
        work->nodes[i].slot = slot;
        work->nodes[i].generation = rtos_task_slot_generation_locked(slot);
        work->nodes[i].state = (uint16_t)task->state;
        work->nodes[i].base_priority = task->base_priority;
        work->nodes[i].effective_priority = task->effective_priority;
        work->nodes[i].owner_slot = RTOS_DIAG_DL_NO_OWNER;
        work->nodes[i].owner_generation = 0U;
        work->nodes[i].resource_id = 0U;
        work->nodes[i].edge_flags = 0U;
        work->nodes[i].wake_tick = 0U;
        work->task_slots[i] = slot;
    }

    /* Second pass: capture waiting relationships */
    for (size_t i = 0U; i < count; ++i) {
        const rtos_task_t *const task = tasks[i];
        if (task->effective_priority > task->base_priority) {
            work->nodes[i].edge_flags |= RTOS_DIAG_DL_EDGE_PI_BOOST;
        }

        if (task->state == RTOS_TASK_BLOCKED && task->wait_reason == RTOS_WAIT_MUTEX &&
            task->wait_result == RTOS_WAIT_RESULT_PENDING) {
            const rtos_mutex_t *const mutex = (const rtos_mutex_t *)task->wait_object;
            if (mutex == NULL || !mutex->initialized || mutex->owner == NULL) {
                work->header.flags |= RTOS_DIAG_DL_FLAG_ANOMALY;
                continue;
            }

            const rtos_task_t *const owner = mutex->owner;
            size_t owner_idx = 0U;
            while (owner_idx < count && tasks[owner_idx] != owner) {
                ++owner_idx;
            }

            if (owner_idx >= count) {
                /* Owner is not in active snapshot nodes */
                work->header.flags |= RTOS_DIAG_DL_FLAG_INCOMPLETE;
            } else if (owner_idx == i) {
                /* Self-edge is a graph invariant anomaly */
                work->header.flags |= RTOS_DIAG_DL_FLAG_ANOMALY;
            } else {
                const uint16_t owner_slot = (uint16_t)(owner->id - 1U);
                work->nodes[i].owner_slot = owner_slot;
                work->nodes[i].owner_generation = work->nodes[owner_idx].generation;
                ++work->header.edge_count;

                if (task->delay_queued) {
                    work->nodes[i].edge_flags |= RTOS_DIAG_DL_EDGE_FINITE;
                    work->nodes[i].wake_tick = task->wake_tick;
                }

                /* Assign compact snapshot resource ID */
                const uintptr_t key = (uintptr_t)mutex;
                uint16_t res_id = 0U;
                for (size_t m = 0U; m < work->header.mutex_count; ++m) {
                    if (work->resource_keys[m] == key) {
                        res_id = (uint16_t)(m + 1U);
                        break;
                    }
                }
                if (res_id == 0U) {
                    if (work->header.mutex_count < RTOS_DIAG_DL_MAX_NODES) {
                        work->resource_keys[work->header.mutex_count] = key;
                        res_id = (uint16_t)(++work->header.mutex_count);
                    } else {
                        work->header.flags |= RTOS_DIAG_DL_FLAG_OVERFLOW;
                        res_id = 1U;
                    }
                }
                work->nodes[i].resource_id = res_id;
            }
        }
    }

    rtos_port_irq_restore(saved);
    work->header.captured = 1U;
    work->header.analyzed = 0U;
    return RTOS_OK;
}

static int find_node_by_slot(const rtos_diag_dl_workspace_t *work, uint16_t slot)
{
    for (uint16_t i = 0U; i < work->header.node_count; ++i) {
        if (work->nodes[i].slot == slot) {
            return (int)i;
        }
    }
    return -1;
}

rtos_status_t rtos_diag_dl_analyze(rtos_diag_dl_workspace_t *work)
{
    if (work == NULL || work->header.captured == 0U) {
        return RTOS_ERR_ARGUMENT;
    }
    if (rtos_port_in_handler() || rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }

    dl_zero(&work->result, sizeof(work->result));
    work->result.capture_tick = work->header.capture_tick;

    if ((work->header.flags & RTOS_DIAG_DL_FLAG_ANOMALY) != 0U) {
        work->result.flags |= (RTOS_DIAG_DL_RES_ANOMALY | RTOS_DIAG_DL_RES_INCOMPLETE);
        work->header.analyzed = 1U;
        return RTOS_OK;
    }
    if ((work->header.flags & RTOS_DIAG_DL_FLAG_INCOMPLETE) != 0U) {
        work->result.flags |= RTOS_DIAG_DL_RES_INCOMPLETE;
        work->header.analyzed = 1U;
        return RTOS_OK;
    }

    uint8_t *const visited = &work->scratch[0];
    uint8_t *const path = &work->scratch[8];
    dl_zero(work->scratch, 24U);

    const uint16_t n = work->header.node_count;
    for (uint16_t start = 0U; start < n; ++start) {
        if (visited[start] != 0U) {
            continue;
        }

        uint8_t path_len = 0U;
        int curr = (int)start;

        while (curr >= 0) {
            if (visited[curr] == 0U) {
                visited[curr] = 1U; /* GRAY: on DFS path */
                path[path_len++] = (uint8_t)curr;

                const uint16_t owner_slot = work->nodes[curr].owner_slot;
                if (owner_slot == RTOS_DIAG_DL_NO_OWNER) {
                    /* End of chain */
                    for (uint8_t p = 0U; p < path_len; ++p) {
                        visited[path[p]] = 2U; /* BLACK: acyclic */
                    }
                    break;
                }

                const int next = find_node_by_slot(work, owner_slot);
                if (next < 0) {
                    /* Target owner not found in snapshot nodes */
                    work->result.flags |= (RTOS_DIAG_DL_RES_ANOMALY | RTOS_DIAG_DL_RES_INCOMPLETE);
                    for (uint8_t p = 0U; p < path_len; ++p) {
                        visited[path[p]] = 2U;
                    }
                    break;
                }

                if (visited[next] == 2U) {
                    /* Merges into already checked acyclic node */
                    for (uint8_t p = 0U; p < path_len; ++p) {
                        visited[path[p]] = 2U;
                    }
                    break;
                }

                if (visited[next] == 1U) {
                    /* CYCLE DETECTED! */
                    uint8_t cycle_start = 0U;
                    while (cycle_start < path_len && path[cycle_start] != (uint8_t)next) {
                        ++cycle_start;
                    }
                    const uint8_t cycle_len = (uint8_t)(path_len - cycle_start);
                    if (cycle_len >= 1U) {
                        /* Rotate cycle to start from the minimum slot index */
                        uint8_t min_k = 0U;
                        uint16_t min_slot = work->nodes[path[cycle_start]].slot;
                        for (uint8_t k = 1U; k < cycle_len; ++k) {
                            const uint16_t s = work->nodes[path[cycle_start + k]].slot;
                            if (s < min_slot) {
                                min_slot = s;
                                min_k = k;
                            }
                        }

                        uint16_t finite_edges = 0U;
                        uint32_t earliest_tick = 0U;
                        bool has_earliest = false;

                        for (uint8_t k = 0U; k < cycle_len; ++k) {
                            const uint8_t c_idx = path[cycle_start + ((min_k + k) % cycle_len)];
                            work->result.cycle_tasks[k] = work->nodes[c_idx].slot;
                            work->result.cycle_mutexes[k] = work->nodes[c_idx].resource_id;

                            if ((work->nodes[c_idx].edge_flags & RTOS_DIAG_DL_EDGE_FINITE) != 0U) {
                                ++finite_edges;
                                const uint32_t due = work->nodes[c_idx].wake_tick;
                                if (!has_earliest || (int32_t)(due - earliest_tick) < 0) {
                                    earliest_tick = due;
                                    has_earliest = true;
                                }
                            }
                        }

                        work->result.deadlock_detected = true;
                        work->result.cycle_length = cycle_len;
                        work->result.flags |= RTOS_DIAG_DL_RES_DEADLOCK;
                        work->result.finite_edge_count = finite_edges;
                        if (finite_edges > 0U) {
                            work->result.flags |= RTOS_DIAG_DL_RES_HAS_TIMEOUT;
                            work->result.earliest_due_tick = earliest_tick;
                        }
                    }

                    for (uint8_t p = 0U; p < path_len; ++p) {
                        visited[path[p]] = 2U;
                    }
                    break;
                }

                curr = next;
            } else {
                break;
            }
        }
    }

    work->header.analyzed = 1U;
    return RTOS_OK;
}

const rtos_diag_dl_result_t *rtos_diag_dl_result(const rtos_diag_dl_workspace_t *work)
{
    if (work == NULL || work->header.captured == 0U || work->header.analyzed == 0U) {
        return NULL;
    }
    return &work->result;
}

rtos_status_t rtos_diag_dl_verify_current(const rtos_diag_dl_workspace_t *work)
{
    if (work == NULL || work->header.captured == 0U) {
        return RTOS_ERR_ARGUMENT;
    }
    if (rtos_port_in_handler() || rtos_port_interrupts_masked()) {
        return RTOS_ERR_CONTEXT;
    }

    const uint32_t saved = rtos_port_irq_save();
    for (uint16_t i = 0U; i < work->header.node_count; ++i) {
        const uint16_t slot = work->nodes[i].slot;
        const uint32_t cur_gen = rtos_task_slot_generation_locked(slot);
        if (cur_gen != work->nodes[i].generation) {
            rtos_port_irq_restore(saved);
            return RTOS_ERR_STALE;
        }
    }
    rtos_port_irq_restore(saved);
    return RTOS_OK;
}

#endif /* INSIGHT_DIAG_DL */
