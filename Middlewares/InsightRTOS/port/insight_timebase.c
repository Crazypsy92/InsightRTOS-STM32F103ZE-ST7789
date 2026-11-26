#include "insight_board_port.h"
#include "main.h"
#include "delay.h"
#include "rtos_assert.h"
#ifdef INSIGHT_TEST_SEMAPHORE
#include "semaphore_fixture.h"
#endif
#if defined(INSIGHT_TEST_MSP) && INSIGHT_TEST_MSP
#include "msp_bench.h"
#endif
#ifdef INSIGHT_DISPLAY_RTOS
#include "rtos.h"
#include "key_port.h"
#endif

volatile uint32_t g_tim2_ms_count;
volatile uint32_t g_sys_tick;

void Timebase_Init(void)
{
    LL_RCC_ClocksTypeDef clocks;
    LL_RCC_GetSystemClocksFreq(&clocks);
    RTOS_ASSERT(clocks.SYSCLK_Frequency == 72000000U);
    RTOS_ASSERT(clocks.HCLK_Frequency == 72000000U);
    RTOS_ASSERT(clocks.PCLK1_Frequency == 36000000U);
    RTOS_ASSERT(clocks.PCLK2_Frequency == 72000000U);
    RTOS_ASSERT(LL_RCC_GetAPB1Prescaler() == LL_RCC_APB1_DIV_2);
    /* APB1 divider > 1: TIM2 input is twice PCLK1, exactly 72 MHz. */
    RTOS_ASSERT(clocks.PCLK1_Frequency * 2U == 72000000U);
    RTOS_ASSERT(NVIC_GetPriorityGrouping() == NVIC_PRIORITYGROUP_4);
    RTOS_ASSERT((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0U);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    NVIC_DisableIRQ(TIM2_IRQn);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_TIM2);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_TIM2);
    TIM2->CR1 = 0U;
    TIM2->DIER = 0U;
    TIM2->PSC = 71U;
    TIM2->ARR = 999U;
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_SetPriority(TIM2_IRQn, 10U);
    TIM2->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
#if defined(INSIGHT_TEST_MSP) && INSIGHT_TEST_MSP
    msp_bench_irq_entry();
#endif
    if ((TIM2->SR & TIM_SR_UIF) != 0U) {
        TIM2->SR = 0U;
        ++g_tim2_ms_count;
        ++g_sys_tick;
#if defined(INSIGHT_TEST_SEMAPHORE)
        semaphore_fixture_tim2();
#elif defined(INSIGHT_DISPLAY_RTOS)
        KEY_OnSysTick1ms();
#endif
    }
}

uint32_t GetTick_ms(void) { return g_tim2_ms_count; }
void Delay_us(uint32_t us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) { }
}
void Delay_ms(uint32_t ms)
{
#ifdef INSIGHT_DISPLAY_RTOS
    if (ms!=0U) {
        const uint32_t ticks=ms/10U+((ms%10U)!=0U ? 1U : 0U)+1U;
        RTOS_ASSERT(rtos_delay(ticks)==RTOS_OK);
    }
#else
    const uint32_t start = GetTick_ms();
    while ((uint32_t)(GetTick_ms() - start) < ms) { }
#endif
}
