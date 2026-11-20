#include "port.h"
#include "rtos.h"
#include "rtos_assert.h"

#define SCB_ICSR (*(volatile uint32_t *)UINT32_C(0xe000ed04))
#define SCB_CCR (*(volatile uint32_t *)UINT32_C(0xe000ed14))
#define SCB_SHPR2 (*(volatile uint32_t *)UINT32_C(0xe000ed1c))
#define SCB_SHPR3 (*(volatile uint32_t *)UINT32_C(0xe000ed20))

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t), "32-bit ARM port");
_Static_assert(offsetof(rtos_task_t, sp) == 0U, "assembly TCB SP offset");

uint32_t *rtos_port_stack_init(uint32_t *base, size_t words,
                               rtos_task_entry_t entry, void *arg)
{
    RTOS_ASSERT(base != NULL);
    RTOS_ASSERT(words >= 128U);
    uintptr_t top = (uintptr_t)(base + words) & ~(uintptr_t)7U;
    uint32_t *sp = (uint32_t *)top - 16;
    for (size_t i = 0U; i < words; ++i) {
        base[i] = RTOS_STACK_FILL;
    }
    base[0] = RTOS_STACK_GUARD;
    /* Software frame R4..R11, then the hardware exception frame. */
    for (size_t i = 0U; i < 16U; ++i) {
        sp[i] = 0U;
    }
    sp[8] = (uint32_t)(uintptr_t)arg;              /* R0 */
    sp[13] = (uint32_t)(uintptr_t)rtos_task_exit;   /* LR, Thumb pointer */
    sp[14] = (uint32_t)(uintptr_t)entry & ~UINT32_C(1); /* PC */
    sp[15] = UINT32_C(0x01000000);                 /* xPSR.T */
    return sp;
}

bool rtos_port_context_valid(const rtos_task_t *task)
{
    const uintptr_t end = (uintptr_t)(task->stack_base + task->stack_words);
    const uintptr_t available = end - (uintptr_t)task->sp;
    if (available < 16U * sizeof(uint32_t)) {
        return false;
    }
    /* A running frame can include one hardware STKALIGN padding word. */
    const size_t words = (task->sp[15] & (UINT32_C(1) << 9)) != 0U ? 17U : 16U;
    return available >= words * sizeof(uint32_t);
}

void rtos_port_init(void)
{
    SCB_CCR |= UINT32_C(1) << 9; /* STKALIGN, including exception entry. */
    SCB_SHPR2 &= UINT32_C(0x00ffffff); /* SVC highest configurable priority. */
    /* NVIC priority numbers are inverse to task priority numbers. */
    SCB_SHPR3 = (SCB_SHPR3 & UINT32_C(0x0000ffff)) | UINT32_C(0x80ff0000);
    rtos_port_tick_init();
    __asm volatile("dsb\nisb" ::: "memory");
}

_Noreturn void rtos_port_start(void)
{
    __asm volatile("svc 0" ::: "memory");
    __builtin_unreachable();
}

void rtos_port_yield(void)
{
    SCB_ICSR = UINT32_C(1) << 28;
    __asm volatile("dsb\nisb" ::: "memory");
}

bool rtos_port_in_handler(void)
{
    uint32_t value;
    __asm volatile("mrs %0, ipsr" : "=r"(value));
    return value != 0U;
}

bool rtos_port_in_kernel_isr(void)
{
    uint32_t exception;
    __asm volatile("mrs %0, ipsr" : "=r"(exception));
    return exception == 15U || exception >= 16U;
}

bool rtos_port_in_unmaskable_handler(void)
{
    uint32_t exception;
    __asm volatile("mrs %0, ipsr" : "=r"(exception));
    return exception == 2U || exception == 3U;
}

bool rtos_port_interrupts_masked(void)
{
    uint32_t primask;
    uint32_t basepri;
    uint32_t faultmask;
    __asm volatile("mrs %0, primask\nmrs %1, basepri\nmrs %2, faultmask"
                   : "=r"(primask), "=r"(basepri), "=r"(faultmask));
    return (primask | basepri | faultmask) != 0U;
}

uint32_t rtos_port_irq_save(void)
{
    uint32_t value;
    __asm volatile("mrs %0, primask\ncpsid i" : "=r"(value) :: "memory");
    return value;
}

void rtos_port_irq_restore(uint32_t saved)
{
    __asm volatile("msr primask, %0\nisb" :: "r"(saved) : "memory");
}

_Noreturn void rtos_port_idle(void)
{
    for (;;) {
        __asm volatile("wfi" ::: "memory");
    }
}

bool rtos_port_task_create_context(void)
{
    uint32_t exception, basepri, faultmask;
    __asm volatile("mrs %0, ipsr\nmrs %1, basepri\nmrs %2, faultmask"
                   : "=r"(exception), "=r"(basepri), "=r"(faultmask));
    return (exception | basepri | faultmask) == 0U;
}
