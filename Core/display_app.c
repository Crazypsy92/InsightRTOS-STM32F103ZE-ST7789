#include "display_app.h"
#include "delay.h"
#include <stdalign.h>

_Static_assert(RTOS_MAX_TASKS == 8U && RTOS_TICK_HZ == 100U, "Audited profile");
_Static_assert(sizeof(display_metric_t) <= RTOS_QUEUE_MAX_ITEM_SIZE, "Queue item ABI");
__attribute__((section(".user_stacks"),used))
alignas(8) static uint32_t display_stacks[8][1024];
static rtos_queue_t raw_keys, controller_keys, lcd_commands, lcd_acks, monitor_messages;
static display_key_t raw_storage[DISPLAY_KEY_CAPACITY], controller_storage[DISPLAY_KEY_CAPACITY];
static display_command_t command_storage[DISPLAY_COMMAND_CAPACITY];
static display_ack_t ack_storage[DISPLAY_ACK_CAPACITY];
static display_metric_t metric_storage[DISPLAY_METRIC_CAPACITY];
volatile display_statistics_t g_display_statistics;
volatile display_metric_t g_monitor_last_metric;
volatile display_event_tracker_t g_event_tracker = {0};
extern volatile uint32_t g_ll_recover_count;
extern volatile int32_t g_ll_last_error;
rtos_task_ref_t g_display_task_refs[4];

void Display_RequireLCDTask(void)
{
    rtos_task_ref_t current;
    RTOS_ASSERT(rtos_task_self_ref(&current)==RTOS_OK);
    RTOS_ASSERT(current.slot==g_display_task_refs[2].slot
        && current.generation==g_display_task_refs[2].generation);
}
void Display_KeyFromISR(key_event_t event)
{
    const display_key_t key={(uint32_t)event,GetTick_ms()};
    const rtos_status_t status=rtos_queue_send_from_isr(&raw_keys,&key);
    ++g_display_statistics.key_isr_events;
    if (status != RTOS_OK) {
        ++g_display_statistics.key_overflows;
    } else {
        g_event_tracker.sequence++;
        g_event_tracker.last_event = event;
        g_event_tracker.stage = EVENT_STAGE_IDENTIFIED;
        g_event_tracker.timestamp_ms = key.at_ms;
    }
    /* Overflow is latched and checked by KEY; never overwrite or coalesce. */
}
static void key_task(void *argument)
{
    (void)argument;
    const rtos_irq_state_t saved=rtos_critical_enter();
    KEY_Init(); /* Register one Button before its first TIM2 scan. */
    rtos_critical_exit(saved);
    for (;;) {
        display_key_t key;
        RTOS_ASSERT(g_display_statistics.key_overflows==0U);
        RTOS_ASSERT(rtos_queue_receive(&raw_keys,&key,RTOS_WAIT_FOREVER)==RTOS_OK);
        RTOS_ASSERT(g_display_statistics.key_overflows==0U);
        g_event_tracker.stage = EVENT_STAGE_KEY;
        ++g_display_statistics.task_runs[0];
        ++g_display_statistics.key_task_events;
        RTOS_ASSERT(rtos_queue_send(&controller_keys,&key,RTOS_WAIT_FOREVER)==RTOS_OK);
    }
}
static display_metric_t metrics(void)
{
    display_metric_t m;
    const rtos_irq_state_t saved=rtos_critical_enter();
    for (unsigned int i=0U;i<4U;++i) m.task_runs[i]=g_display_statistics.task_runs[i];
    m.page=g_display_statistics.page; m.automatic=g_display_statistics.automatic;
    m.ready=g_display_statistics.ready; m.refreshes=g_display_statistics.lcd_refreshes;
    m.page_changes=g_display_statistics.page_changes;
    m.transport_errors=g_display_statistics.transport_errors;
    m.telemetry_overflows=g_display_statistics.telemetry_overflows;
    rtos_critical_exit(saved);
    m.tick=rtos_tick_get();
    return m;
}
static bool send_command(uint32_t *sequence, uint32_t event, uint32_t at_ms)
{
    uint32_t next=*sequence+1U;
    if (next==0U) next=1U; /* Sequence zero is reserved for initialization ACK. */
    const display_command_t command={next,event,at_ms};
    const rtos_status_t status=rtos_queue_send(&lcd_commands,&command,0U);
    if (status==RTOS_ERR_FULL) {
        ++g_display_statistics.command_overflows; /* Backpressure attempts, no drop. */
        return false;
    }
    RTOS_ASSERT(status==RTOS_OK);
    *sequence=next;
    return true;
}
static void controller_task(void *argument)
{
    (void)argument;
    uint32_t sequence=0U, acknowledged=0U, last_metric=rtos_tick_get();
    display_ack_t view={0};
    display_key_t pending_key={0};
    bool key_pending=false;
    for (;;) {
        ++g_display_statistics.task_runs[1];
        for (unsigned int i=0U;i<DISPLAY_ACK_CAPACITY;++i) {
            display_ack_t ack;
            const rtos_status_t s=rtos_queue_receive(&lcd_acks,&ack,0U);
            if (s==RTOS_ERR_UNAVAILABLE) break;
            RTOS_ASSERT(s==RTOS_OK);
            if (ack.sequence!=0U) { uint32_t next=acknowledged+1U; if(next==0U) next=1U; RTOS_ASSERT(ack.sequence==next); acknowledged=ack.sequence; }
            if (ack.page!=view.page) ++g_display_statistics.page_changes;
            view=ack;
            g_display_statistics.page=ack.page; g_display_statistics.automatic=ack.automatic;
            g_display_statistics.ready=ack.ready; g_display_statistics.lcd_refreshes=ack.refreshes;
        }
        /* Retain an unsent event while still draining ACKs: no full-queue cycle. */
        for (unsigned int i=0U;i<4U;++i) {
            if (!key_pending) {
                const rtos_status_t status=rtos_queue_receive(&controller_keys,&pending_key,0U);
                if (status==RTOS_ERR_UNAVAILABLE) break;
                RTOS_ASSERT(status==RTOS_OK);
                RTOS_ASSERT(pending_key.event==KEY_EVENT_SHORT || pending_key.event==KEY_EVENT_DOUBLE || pending_key.event==KEY_EVENT_LONG);
                if (pending_key.event==KEY_EVENT_LONG) {
                    KEY_ToggleLED();
                    ++g_display_statistics.key_long;
                } else if (pending_key.event==KEY_EVENT_SHORT) {
                    ++g_display_statistics.key_short;
                } else if (pending_key.event==KEY_EVENT_DOUBLE) {
                    ++g_display_statistics.key_double;
                }
                g_event_tracker.stage = EVENT_STAGE_CTRL;
                ++g_display_statistics.controller_events;
                key_pending=true;
            }
            if (!send_command(&sequence,pending_key.event,pending_key.at_ms)) break;
            key_pending=false;
        }
        /* Manual FIFO events take precedence over periodic redraw requests. */
        if (!key_pending && view.ready!=0U && sequence==acknowledged &&
            Display_UpdateDue(GetTick_ms(),view.page,view.automatic,view.page_started_ms,view.frame_ms))
            (void)send_command(&sequence,KEY_EVENT_NONE,GetTick_ms());
        const uint32_t now=rtos_tick_get();
        if ((uint32_t)(now-last_metric)>=100U) {
            const display_metric_t m=metrics();
            const rtos_status_t s=rtos_queue_send(&monitor_messages,&m,0U);
            if (s==RTOS_ERR_FULL) ++g_display_statistics.telemetry_overflows;
            else RTOS_ASSERT(s==RTOS_OK);
            last_metric=now;
        }
        RTOS_ASSERT(rtos_delay(1U)==RTOS_OK);
    }
}
static void lcd_task(void *argument)
{
    (void)argument;
    display_ack_t ack;
    for (unsigned int attempt=0U;attempt<3U;++attempt) {
        ++g_display_statistics.task_runs[2];
        Display_PagesInit(&ack);
        if (ack.ready==0U) ++g_display_statistics.initialization_errors;
        RTOS_ASSERT(rtos_queue_send(&lcd_acks,&ack,RTOS_WAIT_FOREVER)==RTOS_OK);
        if (ack.ready!=0U) break;
        if (attempt<2U) RTOS_ASSERT(rtos_delay(100U)==RTOS_OK);
    }
    if (ack.ready==0U) g_display_statistics.display_fault=1U;
    for (;;) {
        display_command_t command;
        RTOS_ASSERT(rtos_queue_receive(&lcd_commands,&command,RTOS_WAIT_FOREVER)==RTOS_OK);
        ++g_display_statistics.task_runs[2];
        Display_PagesExecute(&command,&ack);
        if (command.event!=KEY_EVENT_NONE) {
            g_event_tracker.stage = EVENT_STAGE_LCD;
            g_event_tracker.completed_ms = GetTick_ms();
            g_event_tracker.total_processed++;
            ++g_display_statistics.lcd_events;
        }
        if (ack.error!=0) ++g_display_statistics.transport_errors;
        g_display_statistics.last_lcd_error=ack.error;
        g_display_statistics.ll_recover_count=g_ll_recover_count;
        g_display_statistics.ll_last_error=g_ll_last_error;
        RTOS_ASSERT(rtos_queue_send(&lcd_acks,&ack,RTOS_WAIT_FOREVER)==RTOS_OK);
        RTOS_ASSERT(rtos_delay(1U)==RTOS_OK); /* Bounded service; Monitor gets CPU. */
    }
}
static void monitor_task(void *argument)
{
    (void)argument;
    for (;;) {
        display_metric_t message;
        const rtos_status_t s=rtos_queue_receive(&monitor_messages,&message,100U);
        RTOS_ASSERT(s==RTOS_OK || s==RTOS_ERR_TIMEOUT);
        if (s==RTOS_OK) g_monitor_last_metric=message;
        ++g_display_statistics.task_runs[3];
        RTOS_ASSERT(g_display_statistics.key_overflows==0U);
        for (unsigned int i=0U;i<4U;++i) {
            rtos_task_snapshot_t snapshot;
            RTOS_ASSERT(rtos_task_get_snapshot(g_display_task_refs[i],&snapshot)==RTOS_OK);
            g_display_statistics.snapshots[i]=snapshot;
        }
        const rtos_irq_state_t saved=rtos_critical_enter();
        g_display_statistics.queue_depths[0]=raw_keys.count;
        g_display_statistics.queue_depths[1]=controller_keys.count;
        g_display_statistics.queue_depths[2]=lcd_commands.count;
        g_display_statistics.queue_depths[3]=lcd_acks.count;
        g_display_statistics.queue_depths[4]=monitor_messages.count;
        rtos_critical_exit(saved);
        g_display_statistics.tick=rtos_tick_get();
    }
}
_Noreturn void display_app_start(void)
{
    RTOS_ASSERT(rtos_queue_init(&raw_keys,raw_storage,sizeof(raw_storage),sizeof(raw_storage[0]),DISPLAY_KEY_CAPACITY)==RTOS_OK);
    RTOS_ASSERT(rtos_queue_init(&controller_keys,controller_storage,sizeof(controller_storage),sizeof(controller_storage[0]),DISPLAY_KEY_CAPACITY)==RTOS_OK);
    RTOS_ASSERT(rtos_queue_init(&lcd_commands,command_storage,sizeof(command_storage),sizeof(command_storage[0]),DISPLAY_COMMAND_CAPACITY)==RTOS_OK);
    RTOS_ASSERT(rtos_queue_init(&lcd_acks,ack_storage,sizeof(ack_storage),sizeof(ack_storage[0]),DISPLAY_ACK_CAPACITY)==RTOS_OK);
    RTOS_ASSERT(rtos_queue_init(&monitor_messages,metric_storage,sizeof(metric_storage),sizeof(metric_storage[0]),DISPLAY_METRIC_CAPACITY)==RTOS_OK);
    const rtos_task_entry_t entries[4]={key_task,controller_task,lcd_task,monitor_task};
    const char *const names[4]={"KEY","Controller","LCD","Monitor"};
    const uint8_t priorities[4]={5U,4U,2U,1U};
    for (unsigned int i=0U;i<4U;++i)
        RTOS_ASSERT(rtos_task_create_with_ref(&g_display_task_refs[i],names[i],entries[i],NULL,display_stacks[i],1024U,priorities[i])==RTOS_OK);
    rtos_start();
}
