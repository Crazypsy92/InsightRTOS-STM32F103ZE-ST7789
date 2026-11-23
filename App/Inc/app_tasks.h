#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "app_config.h"
#include "app_ipc.h"
#include "app_events.h"
#include "rtos.h"
#include "rtos_diag_rt.h"

typedef struct {
    uint32_t sample_seq;
    rtos_diag_clock_cap_t clock_cap;
    rtos_diag_rt_snapshot_t task_snapshots[4];
    rtos_diag_rt_snapshot_t idle_snapshot;
} app_diag_rt_export_t;

extern rtos_task_ref_t g_display_task_refs[4];
extern volatile display_statistics_t g_display_statistics;
extern volatile display_metric_t g_monitor_last_metric;
extern volatile app_diag_rt_export_t g_diag_rt_export;
extern volatile uint32_t g_stress_inject_rate;

_Noreturn void app_tasks_start(void);
void App_Tasks_RequireLCDTask(void);

#endif /* APP_TASKS_H */
