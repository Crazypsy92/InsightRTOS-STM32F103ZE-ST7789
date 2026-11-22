#include "display_app.h"
#include "delay.h"

void Display_RequireLCDTask(void)
{
    App_Tasks_RequireLCDTask();
}

display_tracked_event_t *Display_FindTrackedEvent(uint32_t id)
{
    return App_Events_FindTracked(id);
}

void Display_KeyFromISR(key_event_t event)
{
    const uint32_t now = GetTick_ms();
    uint32_t id = App_Events_AllocateId(event, now, false);
    const display_key_t key = {id, (uint32_t)event, now};
    const rtos_status_t status = App_IPC_SendKeyFromISR(&key);
    ++g_display_statistics.key_isr_events;

    if (status != RTOS_OK) {
        ++g_display_statistics.key_overflows;
        display_tracked_event_t *ev = App_Events_FindTracked(id);
        if (ev != NULL) {
            ev->stage = EVENT_STAGE_FAILED;
        }
        g_event_tracker.drop_count++;
    }
}

_Noreturn void display_app_start(void)
{
    app_tasks_start();
}
