#ifndef INSIGHT_MSP_BENCH_H
#define INSIGHT_MSP_BENCH_H

#define MSP_BENCH_FILL 0xA55A3CC3
#define MSP_BENCH_GUARD 0xE25A5ADA
#define MSP_BENCH_GUARD_WORDS 4

#ifndef __ASSEMBLER__
#include "rtos.h"

void msp_bench_observe(uint32_t exception, uint32_t msp);
void msp_bench_controller_step(bool display_ready);

static inline void msp_bench_irq_entry(void)
{
    uint32_t exception, msp;
    __asm volatile("mrs %0, ipsr\nmrs %1, msp" : "=r"(exception), "=r"(msp));
    msp_bench_observe(exception, msp);
}
#endif
#endif
