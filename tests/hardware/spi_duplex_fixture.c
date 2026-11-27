#include "spi_duplex_fixture.h"
#include "spi_duplex_data.h"
#include "bsp_spi.h"
#include "rtos.h"
#include <stdalign.h>

enum { FAULT = 20U, RETRY, TRANSFERS };
#define CS (1U << 11)
#define BOTH_TE (DMA_ISR_TEIF1 | DMA_ISR_TEIF2)
#define BOTH_TC (DMA_ISR_TCIF1 | DMA_ISR_TCIF2)
#define SPI_CLEAN_MASK (SPI_SR_TXE | SPI_SR_BSY | SPI_SR_RXNE | SPI_SR_OVR)

typedef struct {
    uint32_t cr1, cr2, sr, isr, tx_ccr, rx_ccr, tx_cmar, rx_cmar;
    uint32_t tx_cndtr, rx_cndtr, cs, cycles;
} regs_t;

typedef struct {
    regs_t initial, rx_first, waited, drained, injection, stopped;
    uint32_t budget, elapsed;
    int32_t status;
} chunk_t;

typedef struct {
    regs_t regs;
    uint32_t state, owner, depth, recover;
    int32_t error;
} bus_view_t;

typedef struct {
    uint32_t length, pattern, repeat, chunks, elapsed;
    int32_t acquire, transfer, release;
    duplex_data_result_t data;
    bus_view_t acquired, transferred, released;
    chunk_t chunk;
} run_t;

volatile struct {
    uint32_t state, current, completed, hclk, pclk, rcc_cfgr, dwt_ctrl, tx, rx;
    run_t runs[TRANSFERS];
} g_spi_duplex_fixture;

__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][4096U / sizeof(uint32_t)];
static duplex_buffer_t tx_buffer, rx_buffer;
static uint8_t owner;

static regs_t registers(void)
{
    return (regs_t){SPI3->CR1, SPI3->CR2, SPI3->SR, DMA2->ISR,
        DMA2_Channel2->CCR, DMA2_Channel1->CCR, DMA2_Channel2->CMAR, DMA2_Channel1->CMAR,
        DMA2_Channel2->CNDTR, DMA2_Channel1->CNDTR, GPIOD->IDR & CS, DWT->CYCCNT};
}

void spi_duplex_fixture_hook(const spi_stm32_ll_t *ll, spi_duplex_stage_t stage,
                             uint32_t offset, uint32_t length, uint32_t timeout,
                             uint32_t start, spi_bus_status_t status)
{
    const uint32_t at = g_spi_duplex_fixture.current;
    RTOS_ASSERT(at < TRANSFERS && ll->hw->periph == SPI3 && ll->hw->dma == DMA2 &&
                ll->hw->dma_tx_ch == 2U && ll->hw->dma_rx_ch == 1U &&
                offset == 0U && (GPIOD->IDR & CS) != 0U);
    volatile run_t *run = &g_spi_duplex_fixture.runs[at];
    volatile chunk_t *chunk = &run->chunk;
    switch (stage) {
    case SPI_DUPLEX_ARMED:
        RTOS_ASSERT(run->chunks == 0U && run->length == length);
        run->chunks = 1U;
        chunk->budget = timeout;
        chunk->initial = registers();
        break;
    case SPI_DUPLEX_RX_FIRST:
        chunk->rx_first = registers();
        break;
    case SPI_DUPLEX_ACTIVE:
        if (at == FAULT) {
            while (DMA2_Channel1->CNDTR > length - 128U && DWT->CYCCNT - start < 72000U) { }
            chunk->injection = registers();
            RTOS_ASSERT(chunk->injection.rx_cndtr > 0U && chunk->injection.rx_cndtr < length &&
                        chunk->injection.tx_cndtr > 0U && chunk->injection.tx_cndtr < length &&
                        (chunk->injection.tx_ccr & chunk->injection.rx_ccr & DMA_CCR_EN) != 0U &&
                        (chunk->injection.isr & (BOTH_TC | BOTH_TE)) == 0U);
            DMA2_Channel1->CCR &= ~DMA_CCR_EN;
            chunk->stopped = registers();
        }
        break;
    case SPI_DUPLEX_WAITED:
        chunk->waited = registers();
        chunk->elapsed = chunk->waited.cycles - start;
        chunk->status = status;
        break;
    case SPI_DUPLEX_DRAINED:
        chunk->drained = registers();
        break;
    }
}

static bus_view_t snapshot(const spi_bus_t *bus)
{
    const spi_stm32_ll_t *ll = bus->ctx;
    return (bus_view_t){registers(), bus->state, (uintptr_t)bus->owner, bus->depth,
                        spi_stm32_ll_get_recover_count(ll), bus->last_error};
}

static bool clean(regs_t r)
{
    return (r.sr & SPI_CLEAN_MASK) == SPI_SR_TXE && (r.cr2 & 3U) == 0U &&
           ((r.tx_ccr | r.rx_ccr) & DMA_CCR_EN) == 0U && (r.isr & 0xffU) == 0U &&
           r.cs == CS;
}

static void exercise(spi_bus_t *bus, uint32_t n, uint32_t length, uint32_t pattern, uint32_t repeat)
{
    static const spi_bus_config_t cfg = {18000000U, SPI_BUS_MODE_0, 1U};
    RTOS_ASSERT(length <= DUPLEX_CAPACITY && n < TRANSFERS);
    volatile run_t *run = &g_spi_duplex_fixture.runs[n];
    g_spi_duplex_fixture.current = n;
    run->length = length;
    run->pattern = pattern;
    run->repeat = repeat;
    duplex_prepare(&tx_buffer, &rx_buffer, length, pattern);
    run->acquire = spi_bus_acquire(bus, &owner, &cfg);
    run->acquired = snapshot(bus);
    RTOS_ASSERT(run->acquire == SPI_BUS_OK && run->acquired.regs.cs == CS);
    const uint32_t start = DWT->CYCCNT;
    run->transfer = spi_bus_transfer(bus, &owner, tx_buffer.bytes, rx_buffer.bytes, length);
    run->elapsed = DWT->CYCCNT - start;
    run->transferred = snapshot(bus);
    run->release = spi_bus_release(bus, &owner);
    run->released = snapshot(bus);
    run->data = duplex_check(&tx_buffer, &rx_buffer, length);
    RTOS_ASSERT(run->transfer == (n == FAULT ? SPI_BUS_ERR_TIMEOUT : SPI_BUS_OK) &&
                run->chunks == (length < 16U ? 0U : 1U) && run->release == 1 &&
                run->released.depth == 0U && run->released.owner == 0U && clean(run->released.regs) &&
                run->data.bounds_ok && (n == FAULT || run->data.mismatch_count == 0U));
    g_spi_duplex_fixture.completed = n + 1U;
}

static void test_task(void *unused)
{
    (void)unused;
    spi_bus_t *bus = BSP_SPI3_GetBus();
    RTOS_ASSERT(bus != NULL);
    const spi_stm32_ll_t *ll = bus->ctx;
    g_spi_duplex_fixture.hclk = SystemCoreClock;
    g_spi_duplex_fixture.pclk = ll->hw->pclk_hz;
    g_spi_duplex_fixture.rcc_cfgr = RCC->CFGR;
    g_spi_duplex_fixture.dwt_ctrl = DWT->CTRL;
    g_spi_duplex_fixture.tx = (uintptr_t)tx_buffer.bytes;
    g_spi_duplex_fixture.rx = (uintptr_t)rx_buffer.bytes;
    RTOS_ASSERT(SystemCoreClock == 72000000U && ll->hw->pclk_hz == 36000000U &&
                (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
    g_spi_duplex_fixture.state = 1U;
    exercise(bus, 0U, 15U, 0U, 0U);
    exercise(bus, 1U, 257U, 2U, 0U);
    uint32_t index = 2U;
    for (uint32_t length = 16U; length <= DUPLEX_CAPACITY; length *= 128U)
        for (uint32_t pattern = 0U; pattern < 3U; ++pattern)
            for (uint32_t repeat = 0U; repeat < 3U; ++repeat)
                exercise(bus, index++, length, pattern, repeat);
    RTOS_ASSERT(index == FAULT);
    exercise(bus, FAULT, DUPLEX_CAPACITY, 2U, 0U);
    exercise(bus, RETRY, 16U, 2U, 0U);
    __DMB();
    g_spi_duplex_fixture.state = 2U;
    rtos_task_exit();
}

_Noreturn void spi_duplex_fixture_start(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPDEN;
    (void)RCC->APB2ENR;
    GPIOD->BSRR = CS;
    MODIFY_REG(GPIOD->CRH, 0xFU << 12U, 2U << 12U);
    rtos_task_ref_t ref;
    RTOS_ASSERT(rtos_task_create_with_ref(&ref, "SPI duplex", test_task, NULL,
                display_stacks[0], 4096U / sizeof(uint32_t), 5U) == RTOS_OK);
    rtos_start();
}
