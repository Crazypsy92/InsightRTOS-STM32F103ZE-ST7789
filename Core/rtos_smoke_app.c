#include "rtos_smoke_app.h"
#include "main.h"
#include "rtos.h"
#include <stdalign.h>

#define USER_STACK_WORDS 1024U
_Static_assert(RTOS_MAX_TASKS == 8U, "Eight reserved user slots");
_Static_assert(RTOS_TICK_HZ == 100U, "Audited 100 Hz profile");
_Static_assert(sizeof(rtos_task_t) == 72U, "Unchanged production TCB ABI");
__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t user_stacks[RTOS_MAX_TASKS][USER_STACK_WORDS];
static rtos_task_ref_t task_refs[2];
volatile uint32_t g_task_a_runs;
volatile uint32_t g_task_b_runs;
volatile uint32_t g_rtos_tick_count;
volatile uint32_t g_task_b_runs_while_a_delayed;
volatile uint32_t g_task_a_delay_pending;

static void task_a(void *argument)
{
    (void)argument;
    uint32_t release = rtos_tick_get();
    for (;;) {
        g_task_a_delay_pending = 0U;
        ++g_task_a_runs;
        g_rtos_tick_count = rtos_tick_get();
        LL_GPIO_TogglePin(led_GPIO_Port, led_Pin);
        g_task_a_delay_pending = 1U;
        RTOS_ASSERT(rtos_delay_until(&release, 50U) == RTOS_OK);
    }
}

static void task_b(void *argument)
{
    (void)argument;
    uint32_t release = rtos_tick_get();
    for (;;) {
        ++g_task_b_runs;
        if (g_task_a_delay_pending != 0U) { ++g_task_b_runs_while_a_delayed; }
        RTOS_ASSERT(rtos_delay_until(&release, 7U) == RTOS_OK);
    }
}

_Noreturn void rtos_smoke_start(void)
{
    LL_GPIO_SetOutputPin(led_GPIO_Port, led_Pin);
    RTOS_ASSERT(rtos_task_create_with_ref(&task_refs[0], "smoke-A", task_a, NULL,
        user_stacks[0], USER_STACK_WORDS, 16U) == RTOS_OK);
    RTOS_ASSERT(rtos_task_create_with_ref(&task_refs[1], "smoke-B", task_b, NULL,
        user_stacks[1], USER_STACK_WORDS, 8U) == RTOS_OK);
    rtos_start();
}
