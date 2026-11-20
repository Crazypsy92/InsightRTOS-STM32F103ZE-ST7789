#include "insight_board_port.h"
#include "main.h"
#include "port.h"
#include "rtos_assert.h"
#include <errno.h>
#include <stddef.h>

volatile uint32_t g_rtos_assert_line;
const char *volatile g_rtos_assert_expression;
const char *volatile g_rtos_assert_file;

uint32_t rtos_port_cpu_hz(void)
{
    RTOS_ASSERT(SystemCoreClock == 72000000U);
    return SystemCoreClock;
}

_Noreturn void rtos_assert_fail(const char *expression, const char *file, unsigned int line)
{
    __disable_irq();
    g_rtos_assert_expression = expression;
    g_rtos_assert_file = file;
    g_rtos_assert_line = line;
    for (;;) { __WFI(); }
}

/* No newlib heap is available in this static-only image. */
void *_sbrk(ptrdiff_t increment);
void *_sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}
