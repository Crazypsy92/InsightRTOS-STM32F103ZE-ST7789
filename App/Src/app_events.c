#include "app_events.h"
#include <string.h>

volatile display_event_tracker_t g_event_tracker = {0};
static volatile uint32_t s_next_event_id = 0U;

uint32_t App_Events_AllocateId(key_event_t event, uint32_t now_ms, bool is_dropped)
{
    uint32_t id = ++s_next_event_id;
    if (id == 0U) id = ++s_next_event_id;

    unsigned int slot = id % APP_EVENT_HISTORY_CAPACITY;
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
    }
    return id;
}

void App_Events_UpdateStage(uint32_t id, display_event_stage_t expected_stage, display_event_stage_t new_stage)
{
    display_tracked_event_t *ev = App_Events_FindTracked(id);
    if (ev != NULL && ev->stage == expected_stage) {
        ev->stage = new_stage;
    }
}

void App_Events_RecordAck(uint32_t id, int32_t error, uint32_t now_ms)
{
    if (id == 0U) return;
    display_tracked_event_t *ev = App_Events_FindTracked(id);
    if (ev != NULL) {
        ev->ack_ms = now_ms;
        ev->error = error;
        if (error == 0) {
            ev->stage = EVENT_STAGE_ACKED;
            g_event_tracker.confirmed_id = id;
            g_event_tracker.completed_ms = now_ms;
            g_event_tracker.success_count++;
        } else {
            ev->stage = EVENT_STAGE_FAILED;
            g_event_tracker.failure_count++;
        }
    }
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
