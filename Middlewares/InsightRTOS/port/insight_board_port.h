#ifndef INSIGHT_BOARD_PORT_H
#define INSIGHT_BOARD_PORT_H
#include <stdint.h>
extern volatile uint32_t g_tim2_ms_count;
extern volatile uint32_t g_rtos_assert_line;
extern const char *volatile g_rtos_assert_expression;
extern const char *volatile g_rtos_assert_file;
void TIM2_IRQHandler(void);
#endif
