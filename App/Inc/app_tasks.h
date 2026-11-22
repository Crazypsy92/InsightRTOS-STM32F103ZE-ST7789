#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "app_config.h"
#include "app_ipc.h"
#include "app_events.h"
#include "rtos.h"

extern rtos_task_ref_t g_display_task_refs[4];
extern volatile display_statistics_t g_display_statistics;
extern volatile display_metric_t g_monitor_last_metric;

_Noreturn void app_tasks_start(void);
void App_Tasks_RequireLCDTask(void);

#endif /* APP_TASKS_H */
