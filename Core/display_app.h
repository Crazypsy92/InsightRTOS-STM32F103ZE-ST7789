#ifndef INSIGHT_DISPLAY_APP_H
#define INSIGHT_DISPLAY_APP_H
#include "rtos.h"
#include "key_port.h"
#include <stdint.h>
#define DISPLAY_KEY_CAPACITY 32U
#define DISPLAY_COMMAND_CAPACITY 32U
#define DISPLAY_ACK_CAPACITY 4U
#define DISPLAY_METRIC_CAPACITY 4U

typedef struct { uint32_t event, at_ms; } display_key_t;
typedef struct { uint32_t sequence, event, at_ms; } display_command_t;
typedef struct {
    uint32_t sequence, page, automatic, ready, page_started_ms, frame_ms;
    int32_t error;
    uint32_t refreshes;
} display_ack_t;
typedef struct {
    uint32_t task_runs[4], tick, page, automatic, ready;
    uint32_t refreshes, page_changes, transport_errors, telemetry_overflows;
} display_metric_t;
typedef struct {
    volatile uint32_t task_runs[4];
    volatile uint32_t key_isr_events, key_task_events, controller_events, lcd_events;
    volatile uint32_t key_overflows, command_overflows, telemetry_overflows;
    volatile uint32_t transport_errors, initialization_errors, lcd_refreshes, page_changes;
    volatile uint32_t page, automatic, ready, tick, key_short, key_long;
    volatile uint32_t queue_depths[5];
    volatile uint32_t welcome_shows, monitor_refreshes, monitor_rows;
    volatile uint32_t display_fault;
    volatile uint32_t ll_recover_count;
    volatile int32_t last_lcd_error, ll_last_error;
    rtos_task_snapshot_t snapshots[4];
} display_statistics_t;
extern volatile display_statistics_t g_display_statistics;
extern volatile display_metric_t g_monitor_last_metric;
extern rtos_task_ref_t g_display_task_refs[4];
void Display_KeyFromISR(key_event_t event);
key_event_t Display_ConsumeKeyEvent(void);
void Display_RequireLCDTask(void);
void Display_PagesInit(display_ack_t *ack);
void Display_PagesExecute(const display_command_t *cmd, display_ack_t *ack);
bool Display_UpdateDue(uint32_t now, uint32_t page, uint32_t automatic,
                       uint32_t page_started, uint32_t frame_started);
_Noreturn void display_app_start(void);
#endif
