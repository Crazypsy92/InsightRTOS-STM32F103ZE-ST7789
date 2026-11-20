/**
 * @file    app_lcd_test.h
 * @brief   ST7789 屏幕测试程序接口
 * @details 上电后自动依次显示颜色、图形、文字三个静态页面，
 *          最后进入动态动画页面；短按 KEY1 可手动切换到下一页。
 */
#ifndef __APP_LCD_TEST_H__
#define __APP_LCD_TEST_H__

/** @brief 初始化屏幕并显示第一页（需在 Timebase_Init / KEY_Init 之后调用） */
void LCD_Test_Init(void);

/** @brief 测试任务：主循环周期调用，处理翻页与动画刷新（非阻塞） */
void LCD_Test_Task(void);

#endif /* __APP_LCD_TEST_H__ */
