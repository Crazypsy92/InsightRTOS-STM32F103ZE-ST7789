#ifndef APP_IPC_H
#define APP_IPC_H

#include "app_config.h"
#include "key_port.h"
#include <stdint.h>
#include <stdbool.h>

/* Message payload types */
typedef struct {
    uint32_t id;
    uint32_t event;
    uint32_t at_ms;
} display_key_t;

typedef struct {
    uint32_t sequence;
    uint32_t id;
    uint32_t event;
    uint32_t at_ms;
} display_command_t;

typedef struct {
    uint32_t sequence;
    uint32_t id;
    uint32_t page;
    uint32_t automatic;
    uint32_t ready;
    uint32_t page_started_ms;
    uint32_t frame_ms;
    int32_t error;
    uint32_t refreshes;
} display_ack_t;

typedef struct {
    uint32_t task_runs[4];
    uint32_t tick;
    uint32_t page;
    uint32_t automatic;
    uint32_t ready;
    uint32_t refreshes;
    uint32_t page_changes;
    uint32_t transport_errors;
    uint32_t telemetry_overflows;
} display_metric_t;

typedef struct {
    volatile uint32_t task_runs[4];
    volatile uint32_t key_isr_events, key_task_events, controller_events, lcd_events;
    volatile uint32_t key_overflows, command_overflows, telemetry_overflows;
    volatile uint32_t transport_errors, initialization_errors, lcd_refreshes, page_changes;
    volatile uint32_t page, automatic, ready, tick, key_short, key_long, key_double;
    volatile uint32_t queue_depths[5];
    volatile uint32_t welcome_shows, monitor_refreshes, monitor_rows;
    volatile uint32_t display_fault;
    volatile uint32_t ll_recover_count;
    volatile int32_t last_lcd_error, ll_last_error;
    rtos_task_snapshot_t snapshots[4];
    /* Stage 9.3-A3 explicit return code accounting */
    volatile uint32_t key_attempts;
    volatile uint32_t key_sent_ok;
    volatile uint32_t key_queue_full;
    volatile uint32_t key_wrong_context;
    volatile uint32_t key_invalid_arg;
    volatile uint32_t key_other_errors;
} display_statistics_t;

_Static_assert(sizeof(display_key_t) <= RTOS_QUEUE_MAX_ITEM_SIZE, "Queue item ABI: key");
_Static_assert(sizeof(display_command_t) <= RTOS_QUEUE_MAX_ITEM_SIZE, "Queue item ABI: command");
_Static_assert(sizeof(display_ack_t) <= RTOS_QUEUE_MAX_ITEM_SIZE, "Queue item ABI: ack");
_Static_assert(sizeof(display_metric_t) <= RTOS_QUEUE_MAX_ITEM_SIZE, "Queue item ABI: metric");

/* IPC API */
rtos_status_t App_IPC_Init(void);

rtos_status_t App_IPC_SendKeyFromISR(const display_key_t *key);
rtos_status_t App_IPC_SendRawKeyFromThread(const display_key_t *key, uint32_t timeout);
rtos_status_t App_IPC_ReceiveRawKey(display_key_t *key, uint32_t timeout);

rtos_status_t App_IPC_SendControllerKey(const display_key_t *key, uint32_t timeout);
rtos_status_t App_IPC_ReceiveControllerKey(display_key_t *key, uint32_t timeout);

rtos_status_t App_IPC_SendCommand(const display_command_t *cmd, uint32_t timeout);
rtos_status_t App_IPC_ReceiveCommand(display_command_t *cmd, uint32_t timeout);

rtos_status_t App_IPC_SendAck(const display_ack_t *ack, uint32_t timeout);
rtos_status_t App_IPC_ReceiveAck(display_ack_t *ack, uint32_t timeout);

rtos_status_t App_IPC_SendMetric(const display_metric_t *metric, uint32_t timeout);
rtos_status_t App_IPC_ReceiveMetric(display_metric_t *metric, uint32_t timeout);

void App_IPC_GetQueueDepths(uint32_t depths[5]);

#endif /* APP_IPC_H */
