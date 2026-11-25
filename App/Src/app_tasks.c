#include "app_tasks.h"
#if defined(INSIGHT_TEST_JOB)
#include "job_fixture.h"
#endif
#include "delay.h"
#if defined(INSIGHT_TEST_MSP) && INSIGHT_TEST_MSP
#include "msp_bench.h"
#endif
#if defined(INSIGHT_TEST_TIMING)
#include "timing_bench.h"
#endif
#if INSIGHT_DIAG_RT
#include "main.h"
#endif
#include <stdalign.h>

/* Task stacks static allocation: RTOS_MAX_TASKS = 8 slots reserved */
__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][APP_TASK_STACK_WORDS];

rtos_task_ref_t g_display_task_refs[4];
volatile display_statistics_t g_display_statistics = {0};
volatile display_metric_t g_monitor_last_metric = {0};
volatile app_diag_rt_export_t g_diag_rt_export = {0};

extern volatile uint32_t g_ll_recover_count;
extern volatile int32_t g_ll_last_error;

/* Forward declarations from display layer */
extern void Display_PagesInit(display_ack_t *ack);
extern void Display_PagesExecute(const display_command_t *cmd, display_ack_t *ack);
extern bool Display_UpdateDue(uint32_t now, uint32_t page, uint32_t automatic,
                              uint32_t page_started, uint32_t frame_started);

void App_Tasks_RequireLCDTask(void)
{
    rtos_task_ref_t current;
    RTOS_ASSERT(rtos_task_self_ref(&current) == RTOS_OK);
    RTOS_ASSERT(current.slot == g_display_task_refs[2].slot &&
                current.generation == g_display_task_refs[2].generation);
}

/* -------------------------------------------------------------------------- */
/* Task 1: KEY Task (Priority 5)                                              */
/* Trigger: raw_keys queue (populated by TIM2 1ms -> MultiButton -> ISR)      */
/* Responsibility: key debounced event intake and forward to Controller       */
/* -------------------------------------------------------------------------- */
static void key_task(void *argument)
{
    (void)argument;
    const rtos_irq_state_t saved = rtos_critical_enter();
    KEY_Init(); /* Register Button before first scan */
    rtos_critical_exit(saved);

    for (;;) {
        display_key_t key;
        RTOS_ASSERT(App_IPC_ReceiveRawKey(&key, RTOS_WAIT_FOREVER) == RTOS_OK);
#if !defined(INSIGHT_TEST_STRESS) || INSIGHT_TEST_STRESS == 0
        RTOS_ASSERT(g_display_statistics.key_overflows == 0U);
#endif
        RTOS_ASSERT(g_display_statistics.key_wrong_context == 0U);
        RTOS_ASSERT(g_display_statistics.key_invalid_arg == 0U);
        RTOS_ASSERT(g_display_statistics.key_other_errors == 0U);

        App_Events_UpdateStage(key.id, EVENT_STAGE_IDENTIFIED, EVENT_STAGE_KEY);

        ++g_display_statistics.task_runs[0];
        ++g_display_statistics.key_task_events;
        RTOS_ASSERT(App_IPC_SendControllerKey(&key, RTOS_WAIT_FOREVER) == RTOS_OK);
    }
}

static display_metric_t collect_metrics(void)
{
    display_metric_t m;
    const rtos_irq_state_t saved = rtos_critical_enter();
    for (unsigned int i = 0U; i < 4U; ++i) {
        m.task_runs[i] = g_display_statistics.task_runs[i];
    }
    m.page = g_display_statistics.page;
    m.automatic = g_display_statistics.automatic;
    m.ready = g_display_statistics.ready;
    m.refreshes = g_display_statistics.lcd_refreshes;
    m.page_changes = g_display_statistics.page_changes;
    m.transport_errors = g_display_statistics.transport_errors;
    m.telemetry_overflows = g_display_statistics.telemetry_overflows;
    rtos_critical_exit(saved);
    m.tick = rtos_tick_get();
    return m;
}

static bool send_command(uint32_t *sequence, uint32_t id, uint32_t event, uint32_t at_ms)
{
    uint32_t next = *sequence + 1U;
    if (next == 0U) next = 1U; /* Sequence zero is reserved for init ACK */
    const display_command_t command = {next, id, event, at_ms};
    const rtos_status_t status = App_IPC_SendCommand(&command, 0U);
    if (status == RTOS_ERR_FULL) {
        ++g_display_statistics.command_overflows; /* Backpressure attempts, no drop */
        App_Events_RecordRetry();
        return false;
    }
    RTOS_ASSERT(status == RTOS_OK);
    *sequence = next;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Task 2: Controller Task (Priority 4)                                       */
/* Trigger: controller_keys queue, periodic redraw timer                      */
/* Responsibility: Event logic, PC13 LED control, sequence ACK tracking       */
/* -------------------------------------------------------------------------- */
static void controller_task(void *argument)
{
    (void)argument;
    uint32_t sequence = 0U, acknowledged = 0U, last_metric = rtos_tick_get();
    display_ack_t view = {0};
    display_key_t pending_key = {0};
    bool key_pending = false;

    for (;;) {
        ++g_display_statistics.task_runs[1];

        /* Drain completed ACKs */
        for (unsigned int i = 0U; i < APP_ACK_QUEUE_CAPACITY; ++i) {
            display_ack_t ack;
            const rtos_status_t s = App_IPC_ReceiveAck(&ack, 0U);
            if (s == RTOS_ERR_UNAVAILABLE) break;
            RTOS_ASSERT(s == RTOS_OK);
            if (ack.sequence != 0U) {
                uint32_t next = acknowledged + 1U;
                if (next == 0U) next = 1U;
                RTOS_ASSERT(ack.sequence == next);
                acknowledged = ack.sequence;
                if (ack.id != 0U) {
                    App_Events_RecordAck(ack.id, ack.error, GetTick_ms());
                }
            }
            if (ack.page != view.page) ++g_display_statistics.page_changes;
            view = ack;
            g_display_statistics.page = ack.page;
            g_display_statistics.automatic = ack.automatic;
            g_display_statistics.ready = ack.ready;
            g_display_statistics.lcd_refreshes = ack.refreshes;
        }

        /* Forward pending or new keys */
        for (unsigned int i = 0U; i < 4U; ++i) {
            if (!key_pending) {
                const rtos_status_t status = App_IPC_ReceiveControllerKey(&pending_key, 0U);
                if (status == RTOS_ERR_UNAVAILABLE) break;
                RTOS_ASSERT(status == RTOS_OK);
                RTOS_ASSERT(pending_key.event == KEY_EVENT_SHORT ||
                            pending_key.event == KEY_EVENT_DOUBLE ||
                            pending_key.event == KEY_EVENT_LONG);

                if (pending_key.event == KEY_EVENT_LONG) {
                    KEY_ToggleLED();
                    ++g_display_statistics.key_long;
                } else if (pending_key.event == KEY_EVENT_SHORT) {
                    ++g_display_statistics.key_short;
                } else if (pending_key.event == KEY_EVENT_DOUBLE) {
                    ++g_display_statistics.key_double;
                }

                App_Events_UpdateStage(pending_key.id, EVENT_STAGE_KEY, EVENT_STAGE_CTRL);
                ++g_display_statistics.controller_events;
                key_pending = true;
            }
            if (!send_command(&sequence, pending_key.id, pending_key.event, pending_key.at_ms)) {
                break;
            }
            key_pending = false;
        }

        /* Periodic frame update check */
        if (!key_pending && view.ready != 0U && sequence == acknowledged &&
            Display_UpdateDue(GetTick_ms(), view.page, view.automatic,
                             view.page_started_ms, view.frame_ms)) {
            (void)send_command(&sequence, 0U, KEY_EVENT_NONE, GetTick_ms());
        }

        /* 100-tick telemetry push */
        const uint32_t now = rtos_tick_get();
        if ((uint32_t)(now - last_metric) >= 100U) {
            const display_metric_t m = collect_metrics();
            const rtos_status_t s = App_IPC_SendMetric(&m, 0U);
            if (s == RTOS_ERR_FULL) {
                ++g_display_statistics.telemetry_overflows;
            } else {
                RTOS_ASSERT(s == RTOS_OK);
            }
            last_metric = now;
        }

#if defined(INSIGHT_TEST_TIMING)
        timing_bench_controller_step(g_display_task_refs[1], view.ready != 0U);
#endif
#if defined(INSIGHT_TEST_MSP) && INSIGHT_TEST_MSP
        msp_bench_controller_step(view.ready != 0U);
#endif
        RTOS_ASSERT(rtos_delay(1U) == RTOS_OK);
    }
}

/* -------------------------------------------------------------------------- */
/* Task 3: LCD Task (Priority 2)                                              */
/* Trigger: lcd_commands queue                                                */
/* Responsibility: Exclusive owner of SPI3/DMA ST7789 display rendering       */
/* -------------------------------------------------------------------------- */
static void lcd_task(void *argument)
{
    (void)argument;
    display_ack_t ack;

    for (unsigned int attempt = 0U; attempt < 3U; ++attempt) {
        ++g_display_statistics.task_runs[2];
        Display_PagesInit(&ack);
        if (ack.ready == 0U) ++g_display_statistics.initialization_errors;
        RTOS_ASSERT(App_IPC_SendAck(&ack, RTOS_WAIT_FOREVER) == RTOS_OK);
        if (ack.ready != 0U) break;
        if (attempt < 2U) RTOS_ASSERT(rtos_delay(100U) == RTOS_OK);
    }
    if (ack.ready == 0U) g_display_statistics.display_fault = 1U;

    for (;;) {
        display_command_t command;
        RTOS_ASSERT(App_IPC_ReceiveCommand(&command, RTOS_WAIT_FOREVER) == RTOS_OK);
        ++g_display_statistics.task_runs[2];
        Display_PagesExecute(&command, &ack);

        if (command.event != KEY_EVENT_NONE) {
            App_Events_UpdateStage(command.id, EVENT_STAGE_CTRL, EVENT_STAGE_LCD);
            ++g_display_statistics.lcd_events;
        }

        if (ack.error != 0) ++g_display_statistics.transport_errors;
        g_display_statistics.last_lcd_error = ack.error;
        g_display_statistics.ll_recover_count = g_ll_recover_count;
        g_display_statistics.ll_last_error = g_ll_last_error;

        RTOS_ASSERT(App_IPC_SendAck(&ack, RTOS_WAIT_FOREVER) == RTOS_OK);
        RTOS_ASSERT(rtos_delay(1U) == RTOS_OK);
    }
}

/* -------------------------------------------------------------------------- */
/* Task 4: Monitor Task (Priority 1)                                          */
/* Trigger: monitor_messages queue (100ms periodic)                           */
/* Responsibility: System health, task snapshots, queue depth inspection      */
/* -------------------------------------------------------------------------- */
static void monitor_task(void *argument)
{
    (void)argument;
    for (;;) {
        display_metric_t message;
        const rtos_status_t s = App_IPC_ReceiveMetric(&message, 100U);
        RTOS_ASSERT(s == RTOS_OK || s == RTOS_ERR_TIMEOUT);
        if (s == RTOS_OK) g_monitor_last_metric = message;

        ++g_display_statistics.task_runs[3];

        for (unsigned int i = 0U; i < 4U; ++i) {
            rtos_task_snapshot_t snapshot;
            RTOS_ASSERT(rtos_task_get_snapshot(g_display_task_refs[i], &snapshot) == RTOS_OK);
            g_display_statistics.snapshots[i] = snapshot;
        }

#if defined(INSIGHT_DIAG_RT) && (INSIGHT_DIAG_RT != 0)
        app_diag_rt_export_t frame = {0};
        if (rtos_diag_rt_clock_cap(&frame.clock_cap) == RTOS_OK) {
            bool complete = true;
            for (unsigned int i = 0U; i < 4U; ++i) {
                if (rtos_diag_rt_snapshot(g_display_task_refs[i], &frame.task_snapshots[i]) != RTOS_OK)
                    complete = false;
            }
            if (rtos_diag_rt_idle_snapshot(&frame.idle_snapshot) != RTOS_OK) complete = false;
            if (complete) {
                frame.seq_begin = (g_diag_rt_export.seq_begin + 2U) & ~1U;
                frame.seq_end = frame.seq_begin;
                g_diag_rt_export.seq_begin = frame.seq_begin - 1U;
                __DMB();
                g_diag_rt_export.clock_cap = frame.clock_cap;
                for (unsigned int i = 0U; i < 4U; ++i)
                    g_diag_rt_export.task_snapshots[i] = frame.task_snapshots[i];
                g_diag_rt_export.idle_snapshot = frame.idle_snapshot;
                g_diag_rt_export.published_tick = rtos_tick_get();
                g_diag_rt_export.seq_end = frame.seq_end;
                __DMB();
                g_diag_rt_export.seq_begin = frame.seq_begin;
            }
        }
#endif

        App_IPC_GetQueueDepths((uint32_t *)g_display_statistics.queue_depths);
        g_display_statistics.tick = rtos_tick_get();
    }
}

/* -------------------------------------------------------------------------- */
/* Unified Task Launcher: app_tasks_start()                                   */
/* -------------------------------------------------------------------------- */
_Noreturn void app_tasks_start(void)
{
    RTOS_ASSERT(App_IPC_Init() == RTOS_OK);

    const rtos_task_entry_t entries[4] = {
        key_task, controller_task, lcd_task, monitor_task
    };
    const char *const names[4] = {
        "KEY", "Controller", "LCD", "Monitor"
    };
    const uint8_t priorities[4] = {
        APP_KEY_PRIORITY, APP_CTRL_PRIORITY, APP_LCD_PRIORITY, APP_MON_PRIORITY
    };

    for (unsigned int i = 0U; i < 4U; ++i) {
        RTOS_ASSERT(rtos_task_create_with_ref(&g_display_task_refs[i], names[i],
                                              entries[i], NULL, display_stacks[i],
                                              APP_TASK_STACK_WORDS, priorities[i]) == RTOS_OK);
    }

#if defined(INSIGHT_TEST_JOB)
    rtos_task_ref_t job_fixture_ref;
    RTOS_ASSERT(rtos_task_create_with_ref(&job_fixture_ref, "JobTest", insight_job_fixture, NULL,
                                         display_stacks[4], APP_TASK_STACK_WORDS, 3U) == RTOS_OK);
#endif
    rtos_start();
}
