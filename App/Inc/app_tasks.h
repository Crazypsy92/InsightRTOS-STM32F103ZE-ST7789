#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "app_config.h"
#include "app_ipc.h"
#include "app_events.h"
#include "rtos.h"
#include "rtos_diag_rt.h"

typedef struct {
    uint32_t seq_begin;                         /* Seqlock publishing: odd while updating, even when published */
    rtos_diag_clock_cap_t clock_cap;            /* 20 bytes (offset 4) */
    rtos_diag_rt_snapshot_t task_snapshots[4];  /* 4 * 56 = 224 bytes (offset 24) */
    rtos_diag_rt_snapshot_t idle_snapshot;      /* 56 bytes (offset 248) */
    uint32_t seq_end;                           /* Seqlock publishing: equal to seq_begin when consistent (offset 304) */
    uint32_t published_tick;                    /* End of this Monitor publication (offset 308) */
} app_diag_rt_export_t;
_Static_assert(sizeof(app_diag_rt_export_t) == 312U, "app_diag_rt_export_t 312B");

extern rtos_task_ref_t g_display_task_refs[4];
extern volatile display_statistics_t g_display_statistics;
extern volatile display_metric_t g_monitor_last_metric;
extern volatile app_diag_rt_export_t g_diag_rt_export;

_Noreturn void app_tasks_start(void);
void App_Tasks_RequireLCDTask(void);

#endif /* APP_TASKS_H */
