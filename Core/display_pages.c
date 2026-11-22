#include "display_app.h"
#include "display_ui.h"
#include "st7789_port.h"
#include "delay.h"

static key_event_t staged_event;
static uint32_t staged_consumptions;
static uint32_t refresh_count;

static uint8_t s_page = 0U;
static uint8_t s_auto_run = 0U;
static uint8_t s_ready = 0U;
static uint32_t s_page_start_ms = 0U;
static uint32_t s_last_frame_ms = 0U;

key_event_t Display_ConsumeKeyEvent(void)
{
    Display_RequireLCDTask();
    const key_event_t value = staged_event;
    staged_event = KEY_EVENT_NONE;
    ++staged_consumptions;
    return value;
}

static void snapshot(uint32_t sequence, display_ack_t *ack)
{
    ack->sequence = sequence;
    ack->page = s_page;
    ack->automatic = s_auto_run;
    ack->ready = s_ready;
    ack->page_started_ms = s_page_start_ms;
    ack->frame_ms = s_last_frame_ms;
    ack->error = (int32_t)LCD_GetLastError(&g_lcd);
    ack->refreshes = refresh_count;
}

void Display_PagesInit(display_ack_t *ack)
{
    Display_RequireLCDTask();
    staged_event = KEY_EVENT_NONE;
    s_page = 0U;
    s_auto_run = 0U;
    s_ready = 0U;
    if (ST7789_PortInit() == LCD_OK) {
        s_ready = 1U;
        Display_UIConsolidated(true);
        ++refresh_count;
    }
    s_page_start_ms = GetTick_ms();
    s_last_frame_ms = s_page_start_ms;
    snapshot(0U, ack);
}

void Display_PagesExecute(const display_command_t *cmd, display_ack_t *ack)
{
    Display_RequireLCDTask();
    if (s_ready == 0U) {
        staged_event = (key_event_t)cmd->event;
        (void)Display_ConsumeKeyEvent();
        snapshot(cmd->sequence, ack);
        return;
    }
    RTOS_ASSERT(staged_event == KEY_EVENT_NONE);
    const uint32_t before = staged_consumptions;
    LCD_ClearError(&g_lcd);
    staged_event = (key_event_t)cmd->event;
    (void)Display_ConsumeKeyEvent();
    RTOS_ASSERT(staged_consumptions == before + 1U);
    RTOS_ASSERT(staged_event == KEY_EVENT_NONE);

    Display_UIConsolidated(false);
    s_last_frame_ms = GetTick_ms();
    ++refresh_count;
    snapshot(cmd->sequence, ack);
}
