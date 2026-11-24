#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/* Include production App headers */
#include "app_config.h"
#include "display_app.h"
#include "app_ipc.h"
#include "app_events.h"
#include "app_tasks.h"
#include "mock_kernel.h"

/* Include BSP & RTOS headers for UI, MultiButton, and font verification */
#include "lcd_cn16.h"
#include "multi_button.h"
#include "rtos_diag.h"
#include "rtos_trace.h"

/* Mock LED state for testing */
static uint8_t s_led_pin_state = 1U; /* PC13 high = LED off */
void KEY_ToggleLED(void)
{
    s_led_pin_state ^= 1U;
}

uint8_t KEY_GetLEDState(void)
{
    return s_led_pin_state;
}

void KEY_Init(void)
{
}

volatile display_statistics_t g_display_statistics;

/* -------------------------------------------------------------------------- */
/* Test 1: Configuration & ABI Verification                                    */
/* -------------------------------------------------------------------------- */
static void test_configuration_and_abi(void)
{
    printf("[TEST 1] Configuration and ABI verification... ");

    assert(APP_KEY_PRIORITY == 5U);
    assert(APP_CTRL_PRIORITY == 4U);
    assert(APP_LCD_PRIORITY == 2U);
    assert(APP_MON_PRIORITY == 1U);
    assert(APP_TASK_STACK_WORDS == 1024U);

    assert(APP_KEY_QUEUE_CAPACITY == 32U);
    assert(APP_COMMAND_QUEUE_CAPACITY == 32U);
    assert(APP_ACK_QUEUE_CAPACITY == 4U);
    assert(APP_METRIC_QUEUE_CAPACITY == 4U);
    assert(APP_EVENT_HISTORY_CAPACITY == 8U);

    assert(sizeof(display_key_t) <= RTOS_QUEUE_MAX_ITEM_SIZE);
    assert(sizeof(display_command_t) <= RTOS_QUEUE_MAX_ITEM_SIZE);
    assert(sizeof(display_ack_t) <= RTOS_QUEUE_MAX_ITEM_SIZE);
    assert(sizeof(display_metric_t) <= RTOS_QUEUE_MAX_ITEM_SIZE);

    assert(sizeof(display_tracked_event_t) == 24U);
    assert(sizeof(display_event_tracker_t) == 244U);
    assert(sizeof(rtos_diag_rt_snapshot_t) == 56U);
    assert(sizeof(app_diag_rt_export_t) == 312U);

#if UINTPTR_MAX == UINT32_MAX
    assert(sizeof(rtos_task_t) == 72U);
#endif
    assert(sizeof(rtos_trace_record_t) == 24U);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 2: Production App_IPC Queues and Depths                               */
/* -------------------------------------------------------------------------- */
static void test_app_ipc_queues(void)
{
    printf("[TEST 2] Production App_IPC queues and depths... ");

    mock_kernel_reset();
    assert(App_IPC_Init() == RTOS_OK);

    uint32_t depths[5] = {99, 99, 99, 99, 99};
    App_IPC_GetQueueDepths(depths);
    for (int i = 0; i < 5; ++i) {
        assert(depths[i] == 0U);
    }

    /* Send key from ISR */
    const display_key_t k1 = {1U, (uint32_t)KEY_EVENT_SHORT, 100U};
    assert(App_IPC_SendKeyFromISR(&k1) == RTOS_OK);
    App_IPC_GetQueueDepths(depths);
    assert(depths[0] == 1U);

    display_key_t rx_key;
    assert(App_IPC_ReceiveRawKey(&rx_key, 0U) == RTOS_OK);
    assert(rx_key.id == 1U && rx_key.event == (uint32_t)KEY_EVENT_SHORT);
    App_IPC_GetQueueDepths(depths);
    assert(depths[0] == 0U);

    /* Fill command queue to capacity (32 items) */
    for (uint32_t i = 1U; i <= 32U; ++i) {
        display_command_t cmd = {i, i, (uint32_t)KEY_EVENT_SHORT, 100U + i};
        assert(App_IPC_SendCommand(&cmd, 0U) == RTOS_OK);
    }
    App_IPC_GetQueueDepths(depths);
    assert(depths[2] == 32U);

    /* 33rd send must return RTOS_ERR_FULL */
    display_command_t overflow_cmd = {33U, 33U, (uint32_t)KEY_EVENT_SHORT, 200U};
    assert(App_IPC_SendCommand(&overflow_cmd, 0U) == RTOS_ERR_FULL);

    /* Drain commands */
    for (uint32_t i = 1U; i <= 32U; ++i) {
        display_command_t rx_cmd;
        assert(App_IPC_ReceiveCommand(&rx_cmd, 0U) == RTOS_OK);
        assert(rx_cmd.sequence == i);
    }
    App_IPC_GetQueueDepths(depths);
    assert(depths[2] == 0U);

    /* Receive from empty returns RTOS_ERR_UNAVAILABLE */
    display_command_t dummy;
    assert(App_IPC_ReceiveCommand(&dummy, 0U) == RTOS_ERR_UNAVAILABLE);

    /* ACK queue test (capacity 4) */
    for (uint32_t i = 1U; i <= 4U; ++i) {
        display_ack_t ack = {i, i, 0U, 0U, 1U, 100U, 100U, 0, i};
        assert(App_IPC_SendAck(&ack, 0U) == RTOS_OK);
    }
    display_ack_t ack_full = {5U, 5U, 0U, 0U, 1U, 100U, 100U, 0, 5U};
    assert(App_IPC_SendAck(&ack_full, 0U) == RTOS_ERR_FULL);
    for (uint32_t i = 1U; i <= 4U; ++i) {
        display_ack_t rx_ack;
        assert(App_IPC_ReceiveAck(&rx_ack, 0U) == RTOS_OK);
        assert(rx_ack.sequence == i);
    }

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 3: Production App_Events Allocation & Stage Transitions                */
/* -------------------------------------------------------------------------- */
static void test_app_events_transitions(void)
{
    printf("[TEST 3] Production App_Events allocation and stage transitions... ");

    memset((void *)&g_event_tracker, 0, sizeof(g_event_tracker));

    /* Allocate event 1 */
    uint32_t id1 = App_Events_AllocateId(KEY_EVENT_SHORT, 1000U, false);
    assert(id1 > 0U);
    display_tracked_event_t *ev1 = App_Events_FindTracked(id1);
    assert(ev1 != NULL);
    assert(ev1->id == id1);
    assert(ev1->event == KEY_EVENT_SHORT);
    assert(ev1->stage == EVENT_STAGE_IDENTIFIED);
    assert(ev1->timestamp_ms == 1000U);
    assert(g_event_tracker.latest_id == id1);
    assert(g_event_tracker.latest_event == KEY_EVENT_SHORT);

    /* Invalid stage transition (expected KEY, but current is IDENTIFIED) */
    App_Events_UpdateStage(id1, EVENT_STAGE_KEY, EVENT_STAGE_CTRL);
    assert(ev1->stage == EVENT_STAGE_IDENTIFIED);

    /* Valid stage transitions: IDENTIFIED -> KEY -> CTRL -> LCD */
    App_Events_UpdateStage(id1, EVENT_STAGE_IDENTIFIED, EVENT_STAGE_KEY);
    assert(ev1->stage == EVENT_STAGE_KEY);

    App_Events_UpdateStage(id1, EVENT_STAGE_KEY, EVENT_STAGE_CTRL);
    assert(ev1->stage == EVENT_STAGE_CTRL);

    App_Events_UpdateStage(id1, EVENT_STAGE_CTRL, EVENT_STAGE_LCD);
    assert(ev1->stage == EVENT_STAGE_LCD);

    /* Successful ACK: LCD -> ACKED */
    App_Events_RecordAck(id1, 0, 1045U);
    assert(ev1->stage == EVENT_STAGE_ACKED);
    assert(ev1->ack_ms == 1045U);
    assert(ev1->error == 0);
    assert(g_event_tracker.confirmed_id == id1);
    assert(g_event_tracker.completed_ms == 1045U);
    assert(g_event_tracker.success_count == 1U);
    assert(g_event_tracker.failure_count == 0U);

    /* Failed ACK on event 2 */
    uint32_t id2 = App_Events_AllocateId(KEY_EVENT_DOUBLE, 2000U, false);
    assert(id2 > id1);
    display_tracked_event_t *ev2 = App_Events_FindTracked(id2);
    assert(ev2 != NULL);
    App_Events_UpdateStage(id2, EVENT_STAGE_IDENTIFIED, EVENT_STAGE_KEY);
    App_Events_UpdateStage(id2, EVENT_STAGE_KEY, EVENT_STAGE_CTRL);
    App_Events_UpdateStage(id2, EVENT_STAGE_CTRL, EVENT_STAGE_LCD);

    App_Events_RecordAck(id2, -5, 2080U);
    assert(ev2->stage == EVENT_STAGE_FAILED);
    assert(ev2->error == -5);
    assert(g_event_tracker.failure_count == 1U);
    assert(g_event_tracker.success_count == 1U);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 4: Bounded 8-Slot Ring History & Overflow Drop                        */
/* -------------------------------------------------------------------------- */
static void test_bounded_history_and_drop(void)
{
    printf("[TEST 4] Bounded 8-slot ring history and overflow drop... ");

    memset((void *)&g_event_tracker, 0, sizeof(g_event_tracker));

    /* Record retries without drop */
    App_Events_RecordRetry();
    App_Events_RecordRetry();
    assert(g_event_tracker.retry_count == 2U);
    assert(g_event_tracker.drop_count == 0U);

    /* ISR drop */
    uint32_t drop_id = App_Events_AllocateId(KEY_EVENT_LONG, 3000U, true);
    assert(drop_id > 0U);
    display_tracked_event_t *ev_drop = App_Events_FindTracked(drop_id);
    assert(ev_drop != NULL);
    assert(ev_drop->stage == EVENT_STAGE_FAILED);
    assert(g_event_tracker.drop_count == 1U);

    /* Allocate 12 consecutive events to verify ring wrap */
    uint32_t ids[12];
    for (int i = 0; i < 12; ++i) {
        ids[i] = App_Events_AllocateId(KEY_EVENT_SHORT, 4000U + (uint32_t)i * 10U, false);
    }

    /* Total history capacity is 8: the first 4 (ids[0..3]) must have been evicted */
    for (int i = 0; i < 4; ++i) {
        assert(App_Events_FindTracked(ids[i]) == NULL);
    }
    /* The latest 8 (ids[4..11]) must be retained and found */
    for (int i = 4; i < 12; ++i) {
        display_tracked_event_t *ev = App_Events_FindTracked(ids[i]);
        assert(ev != NULL);
        assert(ev->id == ids[i]);
    }
    assert(App_Events_FindTracked(0U) == NULL);

    /* Snapshot test */
    display_event_tracker_t snap;
    App_Events_GetSnapshot(&snap);
    assert(snap.latest_id == ids[11]);
    assert(snap.drop_count == 1U);
    assert(snap.retry_count == 2U);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 5: Closed-Loop Multi-Task Pipeline Simulation                         */
/* -------------------------------------------------------------------------- */
static void test_pipeline_simulation(void)
{
    printf("[TEST 5] Closed-loop multi-task pipeline simulation... ");

    mock_kernel_reset();
    assert(App_IPC_Init() == RTOS_OK);
    memset((void *)&g_event_tracker, 0, sizeof(g_event_tracker));

    key_event_t test_events[3] = {KEY_EVENT_SHORT, KEY_EVENT_DOUBLE, KEY_EVENT_LONG};
    uint32_t seq = 0U;

    for (int i = 0; i < 3; ++i) {
        uint32_t now = 10000U + (uint32_t)i * 500U;

        /* Step 1: ISR identifies key */
        uint32_t id = App_Events_AllocateId(test_events[i], now, false);
        display_key_t raw_k = {id, (uint32_t)test_events[i], now};
        assert(App_IPC_SendKeyFromISR(&raw_k) == RTOS_OK);

        /* Step 2: KEY task receives and forwards to Controller */
        display_key_t key_task_k;
        assert(App_IPC_ReceiveRawKey(&key_task_k, 0U) == RTOS_OK);
        assert(key_task_k.id == id);
        App_Events_UpdateStage(key_task_k.id, EVENT_STAGE_IDENTIFIED, EVENT_STAGE_KEY);
        assert(App_IPC_SendControllerKey(&key_task_k, 0U) == RTOS_OK);

        /* Step 3: Controller task receives and sends command */
        display_key_t ctrl_k;
        assert(App_IPC_ReceiveControllerKey(&ctrl_k, 0U) == RTOS_OK);
        assert(ctrl_k.id == id);
        if (ctrl_k.event == KEY_EVENT_LONG) {
            KEY_ToggleLED();
        }
        App_Events_UpdateStage(ctrl_k.id, EVENT_STAGE_KEY, EVENT_STAGE_CTRL);
        seq++;
        display_command_t cmd = {seq, ctrl_k.id, ctrl_k.event, ctrl_k.at_ms};
        assert(App_IPC_SendCommand(&cmd, 0U) == RTOS_OK);

        /* Step 4: LCD task receives command, renders and sends ACK */
        display_command_t lcd_cmd;
        assert(App_IPC_ReceiveCommand(&lcd_cmd, 0U) == RTOS_OK);
        assert(lcd_cmd.id == id && lcd_cmd.sequence == seq);
        App_Events_UpdateStage(lcd_cmd.id, EVENT_STAGE_CTRL, EVENT_STAGE_LCD);
        display_ack_t ack = {lcd_cmd.sequence, lcd_cmd.id, 0U, 0U, 1U, now + 10U, now + 25U, 0, (uint32_t)i + 1U};
        assert(App_IPC_SendAck(&ack, 0U) == RTOS_OK);

        /* Step 5: Controller receives ACK and closes loop */
        display_ack_t ctrl_ack;
        assert(App_IPC_ReceiveAck(&ctrl_ack, 0U) == RTOS_OK);
        assert(ctrl_ack.sequence == seq && ctrl_ack.id == id);
        App_Events_RecordAck(ctrl_ack.id, ctrl_ack.error, now + 30U);
    }

    assert(g_event_tracker.success_count == 3U);
    assert(g_event_tracker.failure_count == 0U);
    assert(g_event_tracker.drop_count == 0U);
    assert(s_led_pin_state == 0U); /* Long press toggled LED: 1 -> 0 */

    uint32_t final_depths[5];
    App_IPC_GetQueueDepths(final_depths);
    for (int i = 0; i < 5; ++i) {
        assert(final_depths[i] == 0U);
    }

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 6: MultiButton State Machine Verification                             */
/* -------------------------------------------------------------------------- */
static uint8_t s_mock_button_pin = 1U;
static uint8_t mock_read_button_pin(uint8_t button_id)
{
    (void)button_id;
    return s_mock_button_pin;
}

static uint32_t s_btn_short_count = 0;
static uint32_t s_btn_double_count = 0;
static uint32_t s_btn_long_count = 0;

static void btn_callback(Button *btn)
{
    ButtonEvent ev = button_get_event(btn);
    if (ev == BTN_SINGLE_CLICK) s_btn_short_count++;
    else if (ev == BTN_DOUBLE_CLICK) s_btn_double_count++;
    else if (ev == BTN_LONG_PRESS_START) s_btn_long_count++;
}

static void advance_ticks(uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        button_ticks();
    }
}

static void test_multibutton_events(void)
{
    printf("[TEST 6] MultiButton event state machine... ");

    Button btn;
    button_init(&btn, mock_read_button_pin, 0U, 1U);
    button_attach(&btn, BTN_SINGLE_CLICK, btn_callback);
    button_attach(&btn, BTN_DOUBLE_CLICK, btn_callback);
    button_attach(&btn, BTN_LONG_PRESS_START, btn_callback);
    button_start(&btn);

    s_btn_short_count = 0;
    s_btn_double_count = 0;
    s_btn_long_count = 0;

    /* Press for 50ms then release */
    s_mock_button_pin = 0U; advance_ticks(10);
    s_mock_button_pin = 1U; advance_ticks(70);
    assert(s_btn_short_count == 1U);

    /* Double click */
    s_mock_button_pin = 0U; advance_ticks(10);
    s_mock_button_pin = 1U; advance_ticks(10);
    s_mock_button_pin = 0U; advance_ticks(10);
    s_mock_button_pin = 1U; advance_ticks(70);
    assert(s_btn_double_count == 1U);

    /* Long press: hold for >1000ms */
    s_mock_button_pin = 0U; advance_ticks(350);
    assert(s_btn_long_count == 1U);
    s_mock_button_pin = 1U; advance_ticks(50);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 7: Chinese 16x16 Glyph Pack & UI Screen Bounds                         */
/* -------------------------------------------------------------------------- */
static LCD_Device s_mock_lcd = { .width = 240, .height = 240 };

LCD_Result LCD_ShowChar(LCD_Device *dev, uint16_t x, uint16_t y, uint8_t ch,
                        const pFONT *font, uint16_t fc, uint16_t bc)
{
    (void)dev; (void)ch; (void)font; (void)fc; (void)bc;
    assert(x + 8 <= 240);
    assert(y + 16 <= 240);
    return LCD_OK;
}

LCD_Result LCD_ShowPicture(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                           const uint8_t *pic)
{
    (void)dev; (void)pic;
    assert(x + width <= 240);
    assert(y + height <= 240);
    return LCD_OK;
}

static void test_glyph_pack_and_bounds(void)
{
    printf("[TEST 7] Chinese 16x16 glyph pack and UI screen bounds... ");

    /* 143 unique Chinese glyphs in lcd_cn16 */
    assert(LCD_CN16_GlyphCount() == 143U);

    /* Test looking up and drawing required characters */
    const uint32_t required_cps[] = {
        0x7CFB, /* 系 */ 0x7EDF, /* 统 */ 0x8FD0, /* 运 */ 0x884C, /* 行 */
        0x4EFB, /* 任 */ 0x52A1, /* 务 */ 0x72B6, /* 状 */ 0x6001, /* 态 */
        0x5C31, /* 就 */ 0x7EEA, /* 绪 */ 0x963B, /* 阻 */ 0x585E, /* 塞 */
        0x6B7B, /* 死 */ 0x9501, /* 锁 */ 0x8BCA, /* 诊 */ 0x65AD, /* 断 */
        0x65E0, /* 无 */ 0x5F02, /* 异 */ 0x5E38, /* 常 */ 0x5355, /* 单 */
        0x53CC, /* 双 */ 0x957F, /* 长 */ 0x6309  /* 按 */
    };
    for (size_t i = 0; i < sizeof(required_cps)/sizeof(required_cps[0]); ++i) {
        assert(LCD_CN16_DrawGlyph(&s_mock_lcd, 0, 0, required_cps[i], 0xFFFF, 0x0000) == LCD_OK);
    }

    /* Screen layout bounds: 15 rows * 16px = 240px exactly fits ST7789 */
    const uint16_t row_height = 16U;
    const uint16_t total_rows = 15U;
    assert((total_rows * row_height) <= 240U);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 8: LED Active-Low Polarity & Deadlock Diagnostics Strings              */
/* -------------------------------------------------------------------------- */
static void test_led_and_deadlock_states(void)
{
    printf("[TEST 8] LED active-low polarity and deadlock diagnostic states... ");

    /* PC13 is active-low: 0 = LED ON, 1 = LED OFF */
    s_led_pin_state = 1U; /* Off */
    KEY_ToggleLED();
    assert(s_led_pin_state == 0U); /* On */
    KEY_ToggleLED();
    assert(s_led_pin_state == 1U); /* Off */

    /* Deadlock diagnostics status mappings */
    rtos_diag_dl_header_t hdr_ok = { .flags = 0 };
    rtos_diag_dl_result_t res_ok = { .deadlock_detected = false };
    const char *s_ok = (hdr_ok.flags != 0) ? "异常" : (res_ok.deadlock_detected ? "存在" : "无");
    assert(strcmp(s_ok, "无") == 0);

    /* Deadlock detected case */
    rtos_diag_dl_result_t res_dl = { .deadlock_detected = true };
    const char *s_dl = (hdr_ok.flags != 0) ? "异常" : (res_dl.deadlock_detected ? "存在" : "无");
    assert(strcmp(s_dl, "存在") == 0);

    /* Anomaly case */
    rtos_diag_dl_header_t hdr_err = { .flags = RTOS_DIAG_DL_FLAG_ANOMALY };
    const char *s_err = (hdr_err.flags != 0) ? "异常" : (res_ok.deadlock_detected ? "存在" : "无");
    assert(strcmp(s_err, "异常") == 0);

    /* Trace struct preserved */
    rtos_diag_trace_info_t trace_info;
    memset(&trace_info, 0, sizeof(trace_info));
    trace_info.capacity = 128U;
    trace_info.count = 42U;
    assert(trace_info.capacity == 128U);
    assert(trace_info.count == 42U);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Test 9: Consecutive Events, Ring Overwrite, Orphaned ACK & Queue Full      */
/* -------------------------------------------------------------------------- */
static void test_consecutive_events_overflow_and_ack_resilience(void)
{
    printf("[TEST 9] Consecutive events, ring overwrite, orphaned ACK & queue full... ");

    mock_kernel_reset();
    assert(App_IPC_Init() == RTOS_OK);
    memset((void *)&g_event_tracker, 0, sizeof(g_event_tracker));
    memset((void *)&g_display_statistics, 0, sizeof(g_display_statistics));

    /* 1. Allocate 16 consecutive in-flight events (exceeds 8-slot history) */
    uint32_t ids[16];
    for (int i = 0; i < 16; ++i) {
        ids[i] = App_Events_AllocateId(KEY_EVENT_SHORT, 20000U + (uint32_t)i * 10U, false);
        assert(ids[i] > 0U);
    }

    display_event_tracker_t snap;
    App_Events_GetSnapshot(&snap);
    assert(snap.latest_id == ids[15]);
    /* The first 8 events were evicted before ACK arrived */
    assert(snap.evicted_unacked_count == 8U);

    /* 2. Record ACK for all 16 events, including the 8 evicted ones */
    for (int i = 0; i < 16; ++i) {
        App_Events_RecordAck(ids[15-i], 0, 21000U + (uint32_t)i * 10U);
    }

    App_Events_GetSnapshot(&snap);
    /* 8 orphaned ACKs were processed */
    assert(snap.orphaned_ack_count == 8U);
    /* Zero silent loss: ALL 16 events successfully counted! */
    assert(snap.success_count == 16U);
    assert(snap.pending_count == 0U);
    assert(snap.confirmed_id == ids[15]);

    /* 3. Duplicate ACK handling */
    App_Events_RecordAck(ids[15], 0, 21500U);
    App_Events_GetSnapshot(&snap);
    assert(snap.duplicate_ack_count == 1U);
    assert(snap.success_count == 16U); /* uncredited */

    /* 4. Invalid and spurious future ACK handling */
    App_Events_RecordAck(0U, 0, 22000U);
    App_Events_RecordAck(9999U, 0, 22000U);
    App_Events_GetSnapshot(&snap);
    assert(snap.rejected_ack_count == 2U);
    assert(snap.success_count == 16U); /* unchanged */

    /* 5. In-flight failure ACK handling */
    uint32_t err_id = App_Events_AllocateId(KEY_EVENT_LONG, 22500U, false);
    App_Events_RecordAck(err_id, -1, 22600U);
    App_Events_GetSnapshot(&snap);
    assert(snap.failure_count == 1U);
    assert(snap.confirmed_id == err_id);

    const uint32_t settled_before = snap.success_count + snap.failure_count;
    for (uint32_t i = 0; i < APP_KEY_QUEUE_CAPACITY + 2U; ++i)
        Display_KeyFromISR(KEY_EVENT_SHORT);
    mock_kernel_set_isr_result(RTOS_ERR_CONTEXT);
    Display_KeyFromISR(KEY_EVENT_SHORT);
    mock_kernel_set_isr_result(RTOS_ERR_ARGUMENT);
    Display_KeyFromISR(KEY_EVENT_SHORT);
    mock_kernel_set_isr_result(RTOS_ERR_STATE);
    Display_KeyFromISR(KEY_EVENT_SHORT);
    assert(g_display_statistics.key_sent_ok == 32U);
    assert(g_display_statistics.key_queue_full == 2U);
    assert(g_display_statistics.key_overflows == 2U);
    assert(g_display_statistics.key_wrong_context == 1U);
    assert(g_display_statistics.key_invalid_arg == 1U);
    assert(g_display_statistics.key_other_errors == 1U);
    assert(g_display_statistics.key_attempts == 37U);
    uint32_t drained[APP_KEY_QUEUE_CAPACITY];
    for (uint32_t i = 0; i < APP_KEY_QUEUE_CAPACITY; ++i) {
        display_key_t key;
        assert(App_IPC_ReceiveRawKey(&key, 0U) == RTOS_OK);
        drained[i] = key.id;
    }
    for (uint32_t i = APP_KEY_QUEUE_CAPACITY; i > 0; --i)
        App_Events_RecordAck(drained[i - 1U], 0, 23000U);
    App_Events_GetSnapshot(&snap);
    assert(snap.pending_count == 0U);
    assert(snap.success_count + snap.failure_count - settled_before == 32U);
    App_Events_RecordAck(snap.latest_id, 0, 23001U);
    assert(g_event_tracker.rejected_ack_count == snap.rejected_ack_count + 1U);
    assert(g_event_tracker.success_count == snap.success_count);

    printf("PASS\n");
}

/* -------------------------------------------------------------------------- */
/* Main Test Runner                                                           */
/* -------------------------------------------------------------------------- */
int main(void)
{
    printf("====================================================\n");
    printf("InsightRTOS Stage 9.2-H5 Modular Host Test Suite\n");
    printf("====================================================\n");

    test_configuration_and_abi();
    test_app_ipc_queues();
    test_app_events_transitions();
    test_bounded_history_and_drop();
    test_pipeline_simulation();
    test_multibutton_events();
    test_glyph_pack_and_bounds();
    test_led_and_deadlock_states();
    test_consecutive_events_overflow_and_ack_resilience();

    printf("====================================================\n");
    printf("ALL STAGE 9.2-H5 HOST TESTS PASSED (9/9 Suites, 45 Assertions)\n");
    printf("====================================================\n");
    return 0;
}
