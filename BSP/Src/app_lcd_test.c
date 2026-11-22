/**
 * @file    app_lcd_test.c
 * @brief   ST7789 屏幕测试程序实现（不使用串口）
 * @details 共 4 个页面：
 *          0 颜色条 / 1 图形 / 2 文字 / 3 动画。
 *          前 3 页每 2 秒自动切页，到动画页后停留；
 *          任意时刻短按 KEY1 可手动切到下一页。
 */
#include "app_lcd_test.h"
#include "st7789_port.h"
#include "delay.h"
#include "key_port.h"

/* ================================================================================== */
/* 页面管理                                                                            */
/* ================================================================================== */

#ifdef INSIGHT_DISPLAY_RTOS
#include "display_ui.h"
#if defined(INSIGHT_DIAG_EXPLAIN) && INSIGHT_DIAG_EXPLAIN
#define LCD_TEST_PAGE_COUNT   6U
#else
#define LCD_TEST_PAGE_COUNT   5U
#endif
#else
#define LCD_TEST_PAGE_COUNT   4U
#endif
#define LCD_TEST_AUTO_MS      2000U     /* 静态页自动切换间隔 */

static uint8_t  s_page;
static uint8_t  s_auto_run;
static uint8_t  s_ready;
static uint32_t s_page_start_ms;

static void lcd_test_draw_page(uint8_t page);

/* ================================================================================== */
/* 页面 0：颜色条                                                                      */
/* ================================================================================== */

static void lcd_test_draw_colors(void)
{
    static const uint16_t bar_color[8] = {
        RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, GRAY
    };
    static const char *const bar_name[8] = {
        "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA", "WHITE", "GRAY"
    };
    static const uint16_t text_color[8] = {
        WHITE, BLACK, WHITE, BLACK, BLACK, WHITE, BLACK, WHITE
    };

    LCD_Clear(&g_lcd, BLACK);
    LCD_Fill(&g_lcd, 0U, 0U, LCD_W, 26U, DARKBLUE);
    LCD_ShowString(&g_lcd, 6U, 5U, "Color Test", &ASCII_Font16, WHITE, DARKBLUE);

    for (uint8_t i = 0U; i < 8U; i++) {
        uint16_t y = (uint16_t)(26U + (uint16_t)i * 26U);
        LCD_Fill(&g_lcd, 0U, y, LCD_W, 26U, bar_color[i]);
        LCD_ShowString(&g_lcd, 6U, (uint16_t)(y + 5U), bar_name[i],
                       &ASCII_Font16, text_color[i], bar_color[i]);
    }
}

/* ================================================================================== */
/* 页面 1：图形                                                                        */
/* ================================================================================== */

static void lcd_test_draw_shapes(void)
{
    LCD_Clear(&g_lcd, BLACK);
    LCD_Fill(&g_lcd, 0U, 0U, LCD_W, 26U, DARKBLUE);
    LCD_ShowString(&g_lcd, 6U, 5U, "Shape Test", &ASCII_Font16, WHITE, DARKBLUE);

    /* 外框与对角线 */
    LCD_DrawRect(&g_lcd, 6U, 32U, 228U, 202U, WHITE);
    LCD_DrawLine(&g_lcd, 10U, 36U, 229U, 229U, GREEN);
    LCD_DrawLine(&g_lcd, 229U, 36U, 10U, 229U, BLUE);

    /* 圆：空心 / 实心 */
    LCD_DrawCircle(&g_lcd, 62U, 100U, 34U, RED);
    LCD_FillCircle(&g_lcd, 178U, 100U, 34U, YELLOW);

    /* 矩形填充 */
    LCD_Fill(&g_lcd, 60U, 160U, 120U, 34U, CYAN);
    LCD_DrawRect(&g_lcd, 60U, 160U, 120U, 34U, WHITE);
}

/* ================================================================================== */
/* 页面 2：文字                                                                        */
/* ================================================================================== */

static void lcd_test_draw_text(void)
{
    LCD_Clear(&g_lcd, BLACK);
    LCD_Fill(&g_lcd, 0U, 0U, LCD_W, 26U, DARKBLUE);
    LCD_ShowString(&g_lcd, 6U, 5U, "Text Test", &ASCII_Font16, WHITE, DARKBLUE);

    LCD_ShowString(&g_lcd, 8U, 38U, "ST7789 240x240", &ASCII_Font24, GREEN, BLACK);
    LCD_ShowString(&g_lcd, 8U, 74U, "Insight RTOS BSP", &ASCII_Font16, WHITE, BLACK);
    LCD_ShowString(&g_lcd, 8U, 96U, "ASCII 12/16/20/24/32", &ASCII_Font20, YELLOW, BLACK);
    LCD_ShowString(&g_lcd, 8U, 124U, "abcdefghijklmnopqrstuvwxyz", &ASCII_Font12, CYAN, BLACK);
    LCD_ShowString(&g_lcd, 8U, 142U, "0123456789", &ASCII_Font32, MAGENTA, BLACK);

    LCD_ShowString(&g_lcd, 8U, 182U, "Int:", &ASCII_Font16, WHITE, BLACK);
    LCD_ShowIntNum(&g_lcd, 52U, 182U, 1234, 4U, &ASCII_Font16, RED, BLACK);

    LCD_ShowString(&g_lcd, 8U, 204U, "Float:", &ASCII_Font16, WHITE, BLACK);
    LCD_ShowFloatNum(&g_lcd, 76U, 204U, 3.14f, 1U, 2U, &ASCII_Font16, GREEN, BLACK);

    LCD_ShowString(&g_lcd, 8U, 224U, "KEY1 -> next page", &ASCII_Font12, GRAY, BLACK);
}

/* ================================================================================== */
/* 页面 3：动画                                                                        */
/* ================================================================================== */

#define BALL_SIZE     24
#define BALL_MIN_Y    40
#define BALL_MAX_Y    (LCD_H - BALL_SIZE)
#define BALL_MAX_X    (LCD_W - BALL_SIZE)
#define ANIM_FRAME_MS 25U

static int16_t  s_ball_x;
static int16_t  s_ball_y;
static int8_t   s_ball_vx;
static int8_t   s_ball_vy;
static uint32_t s_last_frame_ms;

static const uint16_t s_ball_colors[6] = {
    RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA
};

static void lcd_test_draw_anim(void)
{
    LCD_Clear(&g_lcd, BLACK);
    LCD_Fill(&g_lcd, 0U, 0U, LCD_W, 26U, DARKBLUE);
    LCD_ShowString(&g_lcd, 6U, 5U, "Animation", &ASCII_Font16, WHITE, DARKBLUE);

    s_ball_x = 60;
    s_ball_y = 120;
    s_ball_vx = 4;
    s_ball_vy = 3;
    s_last_frame_ms = GetTick_ms();

    LCD_Fill(&g_lcd, (uint16_t)s_ball_x, (uint16_t)s_ball_y,
             BALL_SIZE, BALL_SIZE, RED);
}

static void lcd_test_anim_task(void)
{
    uint32_t now = GetTick_ms();
    if ((now - s_last_frame_ms) < ANIM_FRAME_MS) {
        return;
    }
    s_last_frame_ms = now;

    /* 擦除旧位置 */
    LCD_Fill(&g_lcd, (uint16_t)s_ball_x, (uint16_t)s_ball_y,
             BALL_SIZE, BALL_SIZE, BLACK);

    /* 计算新位置并在边界反弹 */
    s_ball_x = (int16_t)(s_ball_x + s_ball_vx);
    s_ball_y = (int16_t)(s_ball_y + s_ball_vy);

    if (s_ball_x <= 0) {
        s_ball_x = 0;
        s_ball_vx = (int8_t)-s_ball_vx;
    } else if (s_ball_x >= BALL_MAX_X) {
        s_ball_x = BALL_MAX_X;
        s_ball_vx = (int8_t)-s_ball_vx;
    }
    if (s_ball_y <= BALL_MIN_Y) {
        s_ball_y = BALL_MIN_Y;
        s_ball_vy = (int8_t)-s_ball_vy;
    } else if (s_ball_y >= BALL_MAX_Y) {
        s_ball_y = BALL_MAX_Y;
        s_ball_vy = (int8_t)-s_ball_vy;
    }

    uint16_t color = s_ball_colors[(now / 200U) % 6U];
    LCD_Fill(&g_lcd, (uint16_t)s_ball_x, (uint16_t)s_ball_y,
             BALL_SIZE, BALL_SIZE, color);
}

/* ================================================================================== */
/* 页面调度                                                                            */
/* ================================================================================== */

static void lcd_test_draw_page(uint8_t page)
{
    switch (page) {
        case 0U:
            lcd_test_draw_colors();
            break;
        case 1U:
            lcd_test_draw_shapes();
            break;
        case 2U:
            lcd_test_draw_text();
            break;
#ifdef INSIGHT_DISPLAY_RTOS
        case 4U:
            Display_UIMonitor(true);
            s_last_frame_ms = GetTick_ms();
            break;
        case 5U:
            Display_UISchedTrace(true);
            s_last_frame_ms = GetTick_ms();
            break;
#endif
        case 3U:
        default:
            lcd_test_draw_anim();
            break;
    }
    s_page_start_ms = GetTick_ms();
}

static void lcd_test_next_page(void)
{
    s_page = (uint8_t)((s_page + 1U) % LCD_TEST_PAGE_COUNT);
    lcd_test_draw_page(s_page);
}

void LCD_Test_Init(void)
{
    s_page = 0U;
    s_auto_run = 1U;
    s_ready = 0U;

    if (ST7789_PortInit() != LCD_OK) {
        /* 初始化失败时保持背光关闭，便于与白屏/花屏区分 */
        return;
    }
#ifdef INSIGHT_DISPLAY_RTOS
    if (!Display_UIWelcome()) return;
#endif
    s_ready = 1U;
    lcd_test_draw_page(s_page);
}

void LCD_Test_Task(void)
{
    if (!s_ready) {
        return;
    }

    /* 短按 KEY1 手动翻页，并停止自动切页 */
    if (KEY_GetEvent() == KEY_EVENT_SHORT) {
        s_auto_run = 0U;
        lcd_test_next_page();
        return;
    }

    /* 自动依次播放前三个静态页，动画页停留 */
    if (s_auto_run && s_page < 3U &&
        (GetTick_ms() - s_page_start_ms) >= LCD_TEST_AUTO_MS) {
        lcd_test_next_page();
        return;
    }

#ifdef INSIGHT_DISPLAY_RTOS
    if (s_page == 4U && (uint32_t)(GetTick_ms() - s_last_frame_ms) >= 250U) {
        Display_UIMonitor(false);
        s_last_frame_ms = GetTick_ms();
    }
    if (s_page == 5U && (uint32_t)(GetTick_ms() - s_last_frame_ms) >= 250U) {
        Display_UISchedTrace(false);
        s_last_frame_ms = GetTick_ms();
    }
#endif
    if (s_page == 3U) {
        lcd_test_anim_task();
    }
}
