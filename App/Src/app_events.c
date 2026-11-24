#include "app_events.h"
/* An event can be in either key queue, the command or ACK queue, or
 * held locally by a task. Collisions fail closed, even after long delays. */
#define PENDING_SLOTS 128U
static uint32_t pending_ids[PENDING_SLOTS];
static uint32_t settled_ids[PENDING_SLOTS];


volatile display_event_tracker_t g_event_tracker = {0};
static volatile uint32_t s_next_event_id = 0U;

uint32_t App_Events_AllocateId(key_event_t event, uint32_t now_ms, bool is_dropped)
{
    const rtos_irq_state_t saved = rtos_critical_enter();
    if (s_next_event_id == UINT32_MAX) {
        ++g_event_tracker.drop_count;
        rtos_critical_exit(saved);
        return 0U;
    }
    const uint32_t id = ++s_next_event_id;
    if (!is_dropped && pending_ids[id % PENDING_SLOTS] != 0U) {
        ++g_event_tracker.drop_count;
        rtos_critical_exit(saved);
        return 0U;
    }

    unsigned int slot = id % APP_EVENT_HISTORY_CAPACITY;
    if (g_event_tracker.history[slot].id != 0U) {
        display_event_stage_t old_stage = g_event_tracker.history[slot].stage;
        if (old_stage != EVENT_STAGE_ACKED &&
            old_stage != EVENT_STAGE_FAILED &&
            old_stage != EVENT_STAGE_IDLE) {
            g_event_tracker.evicted_unacked_count++;
        }
    }

    g_event_tracker.history[slot].id = id;
    g_event_tracker.history[slot].event = event;
    g_event_tracker.history[slot].timestamp_ms = now_ms;
    g_event_tracker.history[slot].ack_ms = 0U;
    g_event_tracker.history[slot].error = 0;
    g_event_tracker.latest_id = id;
    g_event_tracker.latest_event = event;

    if (is_dropped) {
        g_event_tracker.history[slot].stage = EVENT_STAGE_FAILED;
        g_event_tracker.drop_count++;
    } else {
        g_event_tracker.history[slot].stage = EVENT_STAGE_IDENTIFIED;
        pending_ids[id % PENDING_SLOTS] = id;
        ++g_event_tracker.pending_count;
    }
    rtos_critical_exit(saved);
    return id;
}

void App_Events_Reject(uint32_t id)
{
    const rtos_irq_state_t saved = rtos_critical_enter();
    if (id != 0U && pending_ids[id % PENDING_SLOTS] == id) {
        pending_ids[id % PENDING_SLOTS] = 0U;
        --g_event_tracker.pending_count;
        ++g_event_tracker.drop_count;
        display_tracked_event_t *ev = App_Events_FindTracked(id);
        if (ev != NULL) ev->stage = EVENT_STAGE_FAILED;
    }
    rtos_critical_exit(saved);
}

void App_Events_UpdateStage(uint32_t id, display_event_stage_t expected_stage, display_event_stage_t new_stage)
{
    const rtos_irq_state_t saved = rtos_critical_enter();
    display_tracked_event_t *ev = App_Events_FindTracked(id);
    if (ev != NULL && ev->stage == expected_stage) ev->stage = new_stage;
    rtos_critical_exit(saved);
}

void App_Events_RecordAck(uint32_t id, int32_t error, uint32_t now_ms)
{
    const rtos_irq_state_t saved = rtos_critical_enter();
    const uint32_t slot = id % PENDING_SLOTS;
    if (id == 0U || pending_ids[slot] != id) {
        if (id != 0U && settled_ids[slot] == id) ++g_event_tracker.duplicate_ack_count;
        else ++g_event_tracker.rejected_ack_count;
        rtos_critical_exit(saved);
        return;
    }
    pending_ids[slot] = 0U;
    settled_ids[slot] = id;
    --g_event_tracker.pending_count;
    display_tracked_event_t *ev = App_Events_FindTracked(id);
    if (ev == NULL) {
        ++g_event_tracker.orphaned_ack_count;
    } else {
        ev->ack_ms = now_ms;
        ev->error = error;
        ev->stage = error == 0 ? EVENT_STAGE_ACKED : EVENT_STAGE_FAILED;
    }
    if (id > g_event_tracker.confirmed_id) g_event_tracker.confirmed_id = id;
    g_event_tracker.completed_ms = now_ms;
    if (error == 0) ++g_event_tracker.success_count;
    else ++g_event_tracker.failure_count;
    rtos_critical_exit(saved);
}

void App_Events_RecordRetry(void)
{
    g_event_tracker.retry_count++;
}

display_tracked_event_t *App_Events_FindTracked(uint32_t id)
{
    if (id == 0U) return NULL;
    for (unsigned int i = 0U; i < APP_EVENT_HISTORY_CAPACITY; ++i) {
        if (g_event_tracker.history[i].id == id) {
            return (display_tracked_event_t *)&g_event_tracker.history[i];
        }
    }
    return NULL;
}

void App_Events_GetSnapshot(display_event_tracker_t *out_snapshot)
{
    if (out_snapshot == NULL) return;
    const rtos_irq_state_t saved = rtos_critical_enter();
    *out_snapshot = *(display_event_tracker_t *)&g_event_tracker;
    rtos_critical_exit(saved);
}
