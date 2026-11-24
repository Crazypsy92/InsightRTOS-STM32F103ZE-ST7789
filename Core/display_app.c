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
    const rtos_irq_state_t saved = rtos_critical_enter();
    const uint32_t now = GetTick_ms();
    const uint32_t id = App_Events_AllocateId(event, now, false);
    const display_key_t key = {id, (uint32_t)event, now};
    ++g_display_statistics.key_attempts;
    ++g_display_statistics.key_isr_events;
    const rtos_status_t status = id == 0U ? RTOS_ERR_CAPACITY : App_IPC_SendKeyFromISR(&key);
    if (status == RTOS_OK) {
        ++g_display_statistics.key_sent_ok;
    } else {
        App_Events_Reject(id);
        switch (status) {
        case RTOS_ERR_FULL:
            ++g_display_statistics.key_queue_full;
            ++g_display_statistics.key_overflows;
            break;
        case RTOS_ERR_CONTEXT: ++g_display_statistics.key_wrong_context; break;
        case RTOS_ERR_ARGUMENT: ++g_display_statistics.key_invalid_arg; break;
        default: ++g_display_statistics.key_other_errors; break;
        }
    }
    rtos_critical_exit(saved);
}

_Noreturn void display_app_start(void)
{
    app_tasks_start();
}
