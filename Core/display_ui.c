#include "display_ui.h"
#include "st7789_port.h"
#include "rtos_diag.h"
#include "lcd_cn16.h"
#include <string.h>

#define UI_ROWS 10U
#define UI_COLUMNS 37U
static char previous[UI_ROWS][UI_COLUMNS+1U];
static uint16_t previous_color[UI_ROWS];
static const char *const task_names[4]={"KEY", "Controller", "LCD", "Monitor"};

/* Each public task snapshot is coherent. App counters are copied atomically;
 * these are observations, not a simultaneous stop-the-world kernel snapshot. */
void Display_ReadUIView(display_ui_view_t *view)
{
    memset(view,0,sizeof(*view));
    for (unsigned int i=0U;i<4U;++i) {
        const rtos_status_t s=rtos_task_get_snapshot(g_display_task_refs[i],&view->tasks[i]);
        if (s==RTOS_OK) {
            view->valid_mask|=UINT32_C(1)<<i;
            if (view->tasks[i].state!=RTOS_TASK_UNUSED &&
                view->tasks[i].state!=RTOS_TASK_TERMINATED) ++view->users;
        } else {
            /* Stale/invalid handles are visible as UNKNOWN and ERROR. */
            /* valid_mask records this failure without fabricating Queue errors. */
        }
    }
    const rtos_irq_state_t saved=rtos_critical_enter();
    view->queue_errors+=g_display_statistics.key_overflows+g_display_statistics.telemetry_overflows;
    view->backpressure=g_display_statistics.command_overflows;
    view->refreshes=g_display_statistics.lcd_refreshes;
    view->spi_errors=g_display_statistics.transport_errors;
    view->init_errors=g_display_statistics.initialization_errors;
    view->last_spi_error=g_display_statistics.ll_last_error;
    rtos_critical_exit(saved);
    view->tick=rtos_tick_get();
}
static const char *state_name(rtos_task_state_t state)
{
    switch (state) {
        case RTOS_TASK_UNUSED: return "UNUSED";
        case RTOS_TASK_READY: return "READY";
        case RTOS_TASK_RUNNING: return "RUN";
        case RTOS_TASK_TERMINATED: return "EXIT";
        case RTOS_TASK_BLOCKED: return "BLOCK";
        default: return "UNKNOWN";
    }
}
static void append(char *line,const char *text)
{
    size_t n=strlen(line);
    while (*text!='\0' && n<UI_COLUMNS) line[n++]=*text++;
    line[n]='\0';
}
static void number(char *line,uint32_t value)
{
    char digits[11];
    size_t n=0U;
    do { digits[n++]=(char)('0'+(value%10U)); value/=10U; } while (value!=0U);
    while (n!=0U) {
        char one[2]={digits[--n],'\0'};
        append(line,one);
    }
}
static void task_line(char *line,unsigned int index,const display_ui_view_t *view)
{
    line[0]='\0'; append(line,task_names[index]); append(line," ");
    if ((view->valid_mask&(UINT32_C(1)<<index))==0U) { append(line,"UNKNOWN"); return; }
    append(line,state_name(view->tasks[index].state)); append(line," P");
    number(line,view->tasks[index].base_priority); append(line,"/");
    number(line,view->tasks[index].effective_priority);
}
static void row(unsigned int index,char *line,uint16_t color,bool force)
{
    size_t n=strlen(line);
    while (n<UI_COLUMNS) line[n++]=' ';
    line[n]='\0';
    if (force || previous_color[index]!=color || strcmp(line,previous[index])!=0) {
        (void)LCD_ShowString(&g_lcd,8U,(uint16_t)(34U+index*20U),line,&ASCII_Font12,color,BLACK);
        memcpy(previous[index],line,UI_COLUMNS+1U);
        previous_color[index]=color;
        ++g_display_statistics.monitor_rows;
    }
}
bool Display_UIWelcome(void)
{
    Display_RequireLCDTask();
    display_ui_view_t view;
    Display_ReadUIView(&view);
    (void)LCD_Clear(&g_lcd,BLACK);
    (void)LCD_Fill(&g_lcd,0U,0U,240U,4U,CYAN);
    (void)LCD_ShowString(&g_lcd,20U,22U,"InsightRTOS",&ASCII_Font24,CYAN,BLACK);
    (void)LCD_ShowString(&g_lcd,20U,58U,"STM32F103ZET6",&ASCII_Font16,WHITE,BLACK);
    (void)LCD_ShowString(&g_lcd,20U,84U,"RTOS STARTING...",&ASCII_Font16,GREEN,BLACK);
    for (unsigned int i=0U;i<4U;++i) {
        char line[UI_COLUMNS+1U]; task_line(line,i,&view);
        (void)LCD_ShowString(&g_lcd,20U,(uint16_t)(118U+i*22U),line,&ASCII_Font12,WHITE,BLACK);
    }
    ++g_display_statistics.welcome_shows;
    if (LCD_GetLastError(&g_lcd)!=LCD_OK) return false;
    /* Tasks/interrupts continue during the welcome. No CPU-wide busy delay. */
    RTOS_ASSERT(rtos_delay(150U)==RTOS_OK);
    return true;
}
void Display_UIMonitor(bool entering)
{
    Display_RequireLCDTask();
    display_ui_view_t view;
    Display_ReadUIView(&view);
    const bool error=view.valid_mask!=15U || view.queue_errors!=0U ||
        view.spi_errors!=0U || view.init_errors!=0U || view.last_spi_error!=0;
    if (entering) {
        memset(previous,0,sizeof(previous));
        (void)LCD_Clear(&g_lcd,BLACK);
        (void)LCD_Fill(&g_lcd,0U,0U,240U,27U,DARKBLUE);
        (void)LCD_ShowString(&g_lcd,8U,3U,"InsightRTOS",&ASCII_Font20,WHITE,DARKBLUE);
    }
    char line[UI_COLUMNS+1U];
    line[0]='\0';append(line,error?"ERROR":"RUNNING");row(0U,line,error?RED:GREEN,entering);
    line[0]='\0';append(line,"Kernel Tick: ");number(line,view.tick);row(1U,line,CYAN,entering);
    line[0]='\0';append(line,"User tasks: ");number(line,view.users);row(2U,line,WHITE,entering);
    for (unsigned int i=0U;i<4U;++i) {task_line(line,i,&view);row(i+3U,line,WHITE,entering);}
    line[0]='\0';append(line,"Qerr:");number(line,view.queue_errors);append(line," Back:");number(line,view.backpressure);row(7U,line,YELLOW,entering);
    line[0]='\0';append(line,"Page refresh: ");number(line,view.refreshes);row(8U,line,WHITE,entering);
    line[0]='\0';append(line,"SPI/DMA err: ");number(line,view.spi_errors);row(9U,line,error?RED:GREEN,entering);
    ++g_display_statistics.monitor_refreshes;
    /* On any bus error, retry rows next time instead of caching failed pixels. */
    if (LCD_GetLastError(&g_lcd)!=LCD_OK) memset(previous,0,sizeof(previous));
}

static char previous_trace[UI_ROWS][UI_COLUMNS+1U];
static uint16_t previous_trace_color[UI_ROWS];

static void trace_row(unsigned int index, char *line, uint16_t color, bool force)
{
    size_t n = strlen(line);
    while (n < UI_COLUMNS) line[n++] = ' ';
    line[n] = '\0';
    if (force || previous_trace_color[index] != color || strcmp(line, previous_trace[index]) != 0) {
        (void)LCD_ShowString(&g_lcd, 8U, (uint16_t)(34U + index * 20U), line, &ASCII_Font12, color, BLACK);
        memcpy(previous_trace[index], line, UI_COLUMNS + 1U);
        previous_trace_color[index] = color;
    }
}

#if defined(INSIGHT_DIAG_EXPLAIN) && INSIGHT_DIAG_EXPLAIN
static void hex_u32(char *line, uint32_t val)
{
    char hex[9];
    for (int h = 7; h >= 0; --h) {
        uint32_t nib = val & 0x0FU;
        hex[h] = (char)(nib < 10U ? ((uint32_t)'0' + nib) : ((uint32_t)'A' + (nib - 10U)));
        val >>= 4U;
    }
    hex[8] = '\0';
    append(line, hex);
}

static const char *explain_kind_name(rtos_explain_kind_t kind)
{
    switch (kind) {
        case RTOS_EXPLAIN_TASK_CREATE: return "CREATE";
        case RTOS_EXPLAIN_TASK_READY: return "READY";
        case RTOS_EXPLAIN_TASK_BLOCK: return "BLOCK";
        case RTOS_EXPLAIN_SCHED_SELECT: return "SELECT";
        case RTOS_EXPLAIN_WAIT_COMPLETE: return "WAIT_OK";
        case RTOS_EXPLAIN_PRIORITY_CHANGE: return "PI_CHANGE";
        case RTOS_EXPLAIN_STOP_REQUEST: return "STOP";
        case RTOS_EXPLAIN_TASK_TERMINATED: return "TERMINATE";
        case RTOS_EXPLAIN_TASK_RECLAIM: return "RECLAIM";
        case RTOS_EXPLAIN_DELAY_EXPIRED: return "DELAY_EXP";
        case RTOS_EXPLAIN_TASK_YIELD: return "YIELD";
        case RTOS_EXPLAIN_CONTEXT_SWITCH: return "C_SWITCH";
        default: return "UNKNOWN";
    }
}

static const char *trace_commit_class_name(uint16_t commit_class, uint16_t *color)
{
    switch (commit_class) {
        case RTOS_COMMIT_HIGHER_PREEMPT:
            if (color != NULL) *color = CYAN;
            return "HIGHER_PREEMPT";
        case RTOS_COMMIT_YIELD:
            if (color != NULL) *color = YELLOW;
            return "YIELD";
        case RTOS_COMMIT_SLICE_ROTATION:
            if (color != NULL) *color = WHITE;
            return "SLICE_ROTATION";
        case RTOS_COMMIT_BLOCK_OR_WAKE_OTHER:
            if (color != NULL) *color = MAGENTA;
            return "BLOCK_OR_WAKE";
        case RTOS_COMMIT_EXIT:
            if (color != NULL) *color = RED;
            return "EXIT";
        case RTOS_COMMIT_IDLE_TRANSITION:
            if (color != NULL) *color = GREEN;
            return "IDLE_TRANSITION";
        case RTOS_COMMIT_MIXED:
            if (color != NULL) *color = YELLOW;
            return "MIXED";
        default:
            if (color != NULL) *color = GRAY;
            return "UNKNOWN";
    }
}

static void append_task_ref(char *line, uint16_t slot, uint32_t gen, bool is_stale)
{
    if (slot == 0xFFFFU || slot == 65535U) {
        append(line, "Idle");
        return;
    }
    bool found = false;
    for (unsigned int i = 0U; i < 4U; ++i) {
        if (g_display_task_refs[i].slot == slot) {
            append(line, task_names[i]);
            found = true;
            break;
        }
    }
    if (!found) {
        append(line, "Task#");
        number(line, slot);
    }
    append(line, " [s");
    number(line, slot);
    append(line, " g");
    number(line, gen);
    append(line, "]");
    if (is_stale) {
        append(line, "?");
    }
}
#endif

void Display_UISchedTrace(bool entering)
{
    Display_RequireLCDTask();
    if (entering) {
        memset(previous_trace, 0, sizeof(previous_trace));
        (void)LCD_Clear(&g_lcd, BLACK);
        (void)LCD_Fill(&g_lcd, 0U, 0U, 240U, 27U, DARKBLUE);
        (void)LCD_ShowString(&g_lcd, 8U, 3U, "SCHED TRACE", &ASCII_Font20, WHITE, DARKBLUE);
    }
    char line[UI_COLUMNS+1U];

#if defined(INSIGHT_DIAG_EXPLAIN) && INSIGHT_DIAG_EXPLAIN
    rtos_diag_trace_info_t info;
    const rtos_status_t status = rtos_diag_trace_info(&info);
    if (status != RTOS_OK) {
        line[0] = '\0'; append(line, "DIAG: ERROR"); trace_row(0U, line, RED, entering);
        line[0] = '\0'; append(line, "rtos_diag_trace_info failed"); trace_row(1U, line, GRAY, entering);
        return;
    }

    line[0] = '\0';
    append(line, "DIAG: ACTIVE (128 REC)");
    trace_row(0U, line, GREEN, entering);

    line[0] = '\0';
    append(line, "Seq:"); number(line, (uint32_t)info.next_sequence);
    append(line, " Ovr:"); number(line, info.overwritten);
    append(line, " Lost:"); number(line, info.producer_dropped);
    trace_row(1U, line, YELLOW, entering);

    /* Read up to 8 latest records to find the newest context switch or event */
    rtos_trace_record_t records[8];
    rtos_diag_trace_batch_t batch;
    const rtos_status_t r_stat = rtos_diag_trace_read_latest(records, 8U, &batch);

    rtos_diag_explain_t exp;
    memset(&exp, 0, sizeof(exp));
    uint32_t record_timestamp = 0U;
    bool found_event = false;

    if (r_stat == RTOS_OK && batch.copied > 0U) {
        /* Prefer newest CONTEXT_SWITCH record if present */
        for (int i = (int)batch.copied - 1; i >= 0; --i) {
            if (records[i].event_type == RTOS_TRACE_CONTEXT_SWITCH) {
                if (rtos_diag_explain_record(&records[i], &exp) == RTOS_OK) {
                    record_timestamp = records[i].tick;
                    found_event = true;
                    break;
                }
            }
        }
        /* Fallback to latest record */
        if (!found_event) {
            uint32_t idx = batch.copied - 1U;
            if (rtos_diag_explain_record(&records[idx], &exp) == RTOS_OK) {
                record_timestamp = records[idx].tick;
                found_event = true;
            }
        }
    }

    if (found_event) {
        /* Row 2: Event Type and Timestamp */
        line[0] = '\0';
        append(line, "Event: ");
        append(line, explain_kind_name(exp.kind));
        append(line, " @"); number(line, record_timestamp);
        trace_row(2U, line, WHITE, entering);

        /* Row 3: Commit Class */
        uint16_t class_color = WHITE;
        const char *class_str = trace_commit_class_name(exp.commit_class, &class_color);
        line[0] = '\0';
        append(line, "Class: ");
        append(line, class_str);
        trace_row(3U, line, class_color, entering);

        /* Row 4: Previous Task */
        line[0] = '\0';
        append(line, "From: ");
        append_task_ref(line, exp.related_slot, exp.related_generation, exp.related_stale);
        trace_row(4U, line, WHITE, entering);

        /* Row 5: Next Task */
        line[0] = '\0';
        append(line, "To  : ");
        append_task_ref(line, exp.primary_slot, exp.primary_generation, exp.primary_stale);
        trace_row(5U, line, CYAN, entering);

        /* Row 6: Detailed Cause */
        line[0] = '\0';
        append(line, "Cause: 0x");
        hex_u32(line, exp.reason_flags);
        if (exp.aux_value != 0U) {
            append(line, " Aux:"); number(line, exp.aux_value);
        }
        trace_row(6U, line, YELLOW, entering);
    } else {
        line[0] = '\0'; append(line, "No trace records yet"); trace_row(2U, line, GRAY, entering);
        line[0] = '\0'; trace_row(3U, line, BLACK, entering);
        line[0] = '\0'; trace_row(4U, line, BLACK, entering);
        line[0] = '\0'; trace_row(5U, line, BLACK, entering);
        line[0] = '\0'; trace_row(6U, line, BLACK, entering);
    }

#else
    line[0] = '\0';
    append(line, "DIAG: DISABLED (TRACE=OFF)");
    trace_row(0U, line, GRAY, entering);

    line[0] = '\0';
    append(line, "INSIGHT_DIAG_EXPLAIN not active");
    trace_row(1U, line, WHITE, entering);

    line[0] = '\0';
    append(line, "Build with TRACE_ENABLED=ON");
    trace_row(2U, line, GRAY, entering);

    line[0] = '\0'; trace_row(3U, line, BLACK, entering);
    line[0] = '\0'; trace_row(4U, line, BLACK, entering);
    line[0] = '\0'; trace_row(5U, line, BLACK, entering);
    line[0] = '\0'; trace_row(6U, line, BLACK, entering);
#endif

    /* Row 7: Mandatory C-Commit Notice */
    line[0] = '\0';
    append(line, "* C-COMMIT ONLY (PSP PEND)");
    trace_row(7U, line, YELLOW, entering);

    /* Row 8: Disclaimer */
    line[0] = '\0';
    append(line, "  (NOT ARM REG RESTORE PROOF)");
    trace_row(8U, line, GRAY, entering);

    /* Row 9: Navigation Hint */
    line[0] = '\0';
    append(line, "KEY1 -> Next Page [6/6]");
    trace_row(9U, line, WHITE, entering);

    if (LCD_GetLastError(&g_lcd) != LCD_OK) {
        memset(previous_trace, 0, sizeof(previous_trace));
    }
}

#define CONS_ROWS 15U
#define CONS_LINE_MAX_BYTES 64U
static char previous_cons[CONS_ROWS][CONS_LINE_MAX_BYTES];
static uint16_t previous_cons_color[CONS_ROWS];

static uint8_t s_selected_task = 0U;
static uint8_t s_detail_mode = 0U;

static uint16_t text_pixel_width(const char *utf8)
{
    uint16_t w = 0U;
    const uint8_t *p = (const uint8_t *)utf8;
    while (*p != 0U) {
        if (*p < 0x80U) {
            w = (uint16_t)(w + 8U);
            ++p;
        } else if (p[0] >= 0xE0U && p[0] <= 0xEFU && p[1] != 0U && p[2] != 0U) {
            w = (uint16_t)(w + 16U);
            p += 3;
        } else {
            w = (uint16_t)(w + 8U);
            ++p;
        }
    }
    return w;
}

static void append_u(char *line, const char *text)
{
    size_t n = strlen(line);
    while (*text != '\0' && n + 1U < CONS_LINE_MAX_BYTES) {
        line[n++] = *text++;
    }
    line[n] = '\0';
}

static void number_u(char *line, uint32_t value)
{
    char digits[11];
    size_t n = 0U;
    do { digits[n++] = (char)('0' + (value % 10U)); value /= 10U; } while (value != 0U);
    while (n != 0U) {
        char one[2] = {digits[--n], '\0'};
        append_u(line, one);
    }
}

static void cons_row(unsigned int index, const char *line_text, uint16_t color, bool force)
{
    if (index >= CONS_ROWS) return;
    char buf[CONS_LINE_MAX_BYTES];
    size_t len = 0U;
    while (line_text[len] != '\0' && len + 1U < CONS_LINE_MAX_BYTES) {
        buf[len] = line_text[len];
        ++len;
    }
    buf[len] = '\0';

    uint16_t w = text_pixel_width(buf);
    while (w + 8U <= 240U && len + 1U < CONS_LINE_MAX_BYTES) {
        buf[len++] = ' ';
        w = (uint16_t)(w + 8U);
    }
    buf[len] = '\0';

    if (force || previous_cons_color[index] != color || strcmp(buf, previous_cons[index]) != 0) {
        const uint16_t y = (uint16_t)(index * 16U);
        (void)LCD_CN16_ShowText(&g_lcd, 0U, y, buf, color, BLACK);
        memcpy(previous_cons[index], buf, CONS_LINE_MAX_BYTES);
        previous_cons_color[index] = color;
    }
}

static const char *state_name_cn(rtos_task_state_t state)
{
    switch (state) {
        case RTOS_TASK_READY: return "就绪";
        case RTOS_TASK_RUNNING: return "运行";
        case RTOS_TASK_TERMINATED: return "退出";
        case RTOS_TASK_BLOCKED: return "阻塞";
        case RTOS_TASK_UNUSED: return "未知";
        default: return "未知";
    }
}

static const char *const task_short_names[4] = {"KEY ", "CTRL", "LCD ", "MON "};

void Display_UIConsolidated(bool entering, key_event_t event)
{
    Display_RequireLCDTask();
    display_ui_view_t view;
    Display_ReadUIView(&view);

    if (event == KEY_EVENT_SHORT) {
        s_selected_task = (uint8_t)((s_selected_task + 1U) % 4U);
    } else if (event == KEY_EVENT_DOUBLE) {
        s_detail_mode = (uint8_t)(!s_detail_mode);
    }

    if (entering) {
        memset(previous_cons, 0, sizeof(previous_cons));
        (void)LCD_Clear(&g_lcd, BLACK);
    }

    char line[CONS_LINE_MAX_BYTES];

    /* Row 0: Title */
    line[0] = '\0';
    append_u(line, "InsightRTOS     系统运行");
    cons_row(0U, line, CYAN, entering);

    /* Row 1: Blank separator */
    line[0] = '\0';
    cons_row(1U, line, BLACK, entering);

    /* Row 2: Section header */
    line[0] = '\0';
    append_u(line, "任务状态");
    cons_row(2U, line, YELLOW, entering);

    /* Rows 3..6: 4 Tasks Status */
    for (unsigned int i = 0U; i < 4U; ++i) {
        line[0] = '\0';
        const bool selected = (i == s_selected_task);
        if (selected) {
            append_u(line, "> ");
        } else {
            append_u(line, "  ");
        }
        append_u(line, task_short_names[i]);
        append_u(line, "       ");
        if ((view.valid_mask & (UINT32_C(1) << i)) != 0U) {
            append_u(line, state_name_cn(view.tasks[i].state));
            append_u(line, "   P");
            number_u(line, view.tasks[i].effective_priority);
        } else {
            append_u(line, "未知   --");
        }
        cons_row(3U + i, line, selected ? YELLOW : WHITE, entering);
    }

    /* Row 7: Blank separator */
    line[0] = '\0';
    cons_row(7U, line, BLACK, entering);

    /* Row 8: Current task name */
    line[0] = '\0';
    append_u(line, "当前任务: ");
    if (s_selected_task < 4U) {
        append_u(line, task_names[s_selected_task]);
    } else {
        append_u(line, "未知");
    }
    cons_row(8U, line, YELLOW, entering);

    /* Row 9: Detail Row A */
    line[0] = '\0';
    if (s_detail_mode == 0U) {
        append_u(line, "运行次数: ");
        uint32_t count = 0U;
        if (s_selected_task == 0U) {
            count = g_display_statistics.key_task_events;
        } else if (s_selected_task < 4U) {
            count = g_display_statistics.task_runs[s_selected_task];
        }
        number_u(line, count);
    } else {
        append_u(line, "任务状态: ");
        if ((view.valid_mask & (UINT32_C(1) << s_selected_task)) != 0U) {
            append_u(line, state_name_cn(view.tasks[s_selected_task].state));
        } else {
            append_u(line, "未知");
        }
    }
    cons_row(9U, line, WHITE, entering);

    /* Row 10: Detail Row B */
    line[0] = '\0';
    if (s_detail_mode == 0U) {
        append_u(line, "优先级: ");
        if ((view.valid_mask & (UINT32_C(1) << s_selected_task)) != 0U) {
            number_u(line, view.tasks[s_selected_task].base_priority);
            append_u(line, "/");
            number_u(line, view.tasks[s_selected_task].effective_priority);
        } else {
            append_u(line, "--/--");
        }
    } else {
        append_u(line, "Task Gen: ");
        if ((view.valid_mask & (UINT32_C(1) << s_selected_task)) != 0U) {
            number_u(line, view.tasks[s_selected_task].identity.generation);
        } else {
            append_u(line, "--");
        }
    }
    cons_row(10U, line, WHITE, entering);

    /* Row 11: Deadlock Diagnostics */
    line[0] = '\0';
    append_u(line, "死锁诊断: ");
#if INSIGHT_DIAG_DL
    static rtos_diag_dl_workspace_t dl_work;
    const rtos_status_t cap_stat = rtos_diag_dl_capture(&dl_work);
    const rtos_diag_dl_result_t *dl_res = NULL;
    if (cap_stat == RTOS_OK) {
        if (rtos_diag_dl_analyze(&dl_work) == RTOS_OK) {
            dl_res = rtos_diag_dl_result(&dl_work);
        }
    }
    uint16_t diag_color = GREEN;
    if (cap_stat != RTOS_OK || dl_res == NULL ||
        (dl_work.header.flags & (RTOS_DIAG_DL_FLAG_INCOMPLETE | RTOS_DIAG_DL_FLAG_ANOMALY)) != 0U) {
        append_u(line, "诊断异常");
        diag_color = RED;
    } else if (dl_res->deadlock_detected) {
        append_u(line, "存在死锁");
        diag_color = RED;
    } else {
        append_u(line, "无死锁");
        diag_color = GREEN;
    }
    cons_row(11U, line, diag_color, entering);
#else
    append_u(line, "未启用");
    cons_row(11U, line, GRAY, entering);
#endif

    /* Row 12: Trace info */
    line[0] = '\0';
    append_u(line, "Trace: ");
#if INSIGHT_DIAG_EXPLAIN
    rtos_diag_trace_info_t trace_info;
    if (rtos_diag_trace_info(&trace_info) == RTOS_OK) {
        number_u(line, (uint32_t)trace_info.next_sequence);
    } else {
        append_u(line, "N/A");
    }
#else
    append_u(line, "OFF");
#endif
    cons_row(12U, line, WHITE, entering);

    /* Row 13: Errors & Long press hint */
    line[0] = '\0';
    append_u(line, "错误: ");
    const uint32_t total_errors = view.queue_errors + view.spi_errors + view.init_errors;
    number_u(line, total_errors);
    append_u(line, "    长按: LED");
    cons_row(13U, line, (total_errors != 0U) ? RED : WHITE, entering);

    /* Row 14: Interaction hint */
    line[0] = '\0';
    append_u(line, "单击选任务  双击查询");
    cons_row(14U, line, GRAY, entering);

    if (LCD_GetLastError(&g_lcd) != LCD_OK) {
        memset(previous_cons, 0, sizeof(previous_cons));
    }
}
