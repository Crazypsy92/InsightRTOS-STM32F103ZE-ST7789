#ifndef INSIGHT_DISPLAY_APP_H
#define INSIGHT_DISPLAY_APP_H

#include "app_config.h"
#include "app_ipc.h"
#include "app_events.h"
#include "app_tasks.h"

/* Legacy capacity aliases for backward compatibility */
#define DISPLAY_KEY_CAPACITY      APP_KEY_QUEUE_CAPACITY
#define DISPLAY_COMMAND_CAPACITY  APP_COMMAND_QUEUE_CAPACITY
#define DISPLAY_ACK_CAPACITY      APP_ACK_QUEUE_CAPACITY
#define DISPLAY_METRIC_CAPACITY   APP_METRIC_QUEUE_CAPACITY

void Display_KeyFromISR(key_event_t event);
key_event_t Display_ConsumeKeyEvent(void);
void Display_RequireLCDTask(void);
display_tracked_event_t *Display_FindTrackedEvent(uint32_t id);
void Display_PagesInit(display_ack_t *ack);
void Display_PagesExecute(const display_command_t *cmd, display_ack_t *ack);
bool Display_UpdateDue(uint32_t now, uint32_t page, uint32_t automatic,
                       uint32_t page_started, uint32_t frame_started);
_Noreturn void display_app_start(void);

#endif /* INSIGHT_DISPLAY_APP_H */
