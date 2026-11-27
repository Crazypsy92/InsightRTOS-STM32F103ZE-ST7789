#include "spi_dma_fixture.h"
#include "bsp_spi.h"
#include "rtos.h"
#include <stdalign.h>

enum { PROBE, POLL, MIN_DMA, MAX_DMA, PLUS_ONE, DOUBLE_MAX, FAULT, RETRY, TRANSFERS };
#define SOURCE UINT32_C(0x08010000)
#define CS (1U << 11)
#define DMA_FLAGS UINT32_C(0xff)
#define DMA_TX_TC UINT32_C(0x20)
#define DMA_TX_TE UINT32_C(0x80)
#define SPI_CLEAN_MASK (SPI_SR_TXE | SPI_SR_BSY | SPI_SR_RXNE | SPI_SR_OVR)

typedef struct {
    uint32_t cr1, cr2, sr, isr, tx_ccr, rx_ccr, cmar, cndtr, cs, cycles;
} regs_t;

typedef struct {
    regs_t initial, waited, drained, injection, stopped;
    uint32_t offset, length, budget, elapsed;
    int32_t status;
} chunk_t;

typedef struct {
    regs_t regs;
    uint32_t state, owner, depth, recover;
    int32_t error;
} bus_view_t;

typedef struct {
    uint32_t source, length, chunks, cycles;
    int32_t acquire, transfer, release;
    bus_view_t acquired, transferred, released;
    chunk_t chunk[2];
} run_t;

volatile struct {
    uint32_t state, current, completed, hclk, pclk, rcc_cfgr, dwt_ctrl;
    run_t runs[TRANSFERS];
} g_spi_dma_fixture;

__attribute__((section(".user_stacks"), used))
alignas(8) static uint32_t display_stacks[RTOS_MAX_TASKS][4096U / sizeof(uint32_t)];
static uint8_t owner;

static regs_t registers(void)
{
    return (regs_t){SPI3->CR1, SPI3->CR2, SPI3->SR, DMA2->ISR,
        DMA2_Channel2->CCR, DMA2_Channel1->CCR, DMA2_Channel2->CMAR,
        DMA2_Channel2->CNDTR, GPIOD->IDR & CS, DWT->CYCCNT};
}

void spi_dma_fixture_hook(const spi_stm32_ll_t *ll, spi_dma_test_stage_t stage,
                          uint32_t offset, uint32_t length, uint32_t timeout,
                          uint32_t start, spi_bus_status_t status)
{
    const uint32_t current = g_spi_dma_fixture.current;
    RTOS_ASSERT(current < TRANSFERS && ll->hw->periph == SPI3 && ll->hw->dma == DMA2 &&
                ll->hw->dma_tx_ch == 2U && ll->hw->dma_rx_ch == 1U &&
                (GPIOD->IDR & CS) != 0U);
    volatile run_t *const run = &g_spi_dma_fixture.runs[current];
    if (stage == SPI_DMA_TEST_ARMED) {
        const uint32_t at = run->chunks;
        RTOS_ASSERT(at < 2U);
        volatile chunk_t *const chunk = &run->chunk[at];
        chunk->offset = offset;
        chunk->length = length;
        chunk->budget = timeout;
        chunk->initial = registers();
        run->chunks = at + 1U;
        return;
    }
    RTOS_ASSERT(run->chunks > 0U && run->chunks <= 2U);
    volatile chunk_t *const chunk = &run->chunk[run->chunks - 1U];
    if (stage == SPI_DMA_TEST_ACTIVE && current == FAULT) {
        while (DMA2_Channel2->CNDTR > length - 128U && DWT->CYCCNT - start < 72000U) {
            /* Wait for an observed partial transfer, with a 1 ms DWT bound. */
        }
        chunk->injection = registers();
        RTOS_ASSERT(chunk->injection.cndtr > 0U && chunk->injection.cndtr < length &&
                    (chunk->injection.tx_ccr & DMA_CCR_EN) != 0U &&
                    (chunk->injection.isr & (DMA_TX_TC | DMA_TX_TE)) == 0U);
        DMA2_Channel2->CCR &= ~DMA_CCR_EN;
        chunk->stopped = registers();
    } else if (stage == SPI_DMA_TEST_WAITED) {
        chunk->waited = registers();
        chunk->elapsed = chunk->waited.cycles - start;
        chunk->status = status;
    } else if (stage == SPI_DMA_TEST_DRAINED) {
        chunk->drained = registers();
    }
}

static bus_view_t snapshot(const spi_bus_t *bus)
{
    const spi_stm32_ll_t *const ll = bus->ctx;
    return (bus_view_t){registers(), bus->state, (uintptr_t)bus->owner, bus->depth,
                        spi_stm32_ll_get_recover_count(ll), bus->last_error};
}

static bool clean(regs_t r)
{
    return (r.sr & SPI_CLEAN_MASK) == SPI_SR_TXE &&
           (r.cr2 & (SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN)) == 0U &&
           ((r.tx_ccr | r.rx_ccr) & DMA_CCR_EN) == 0U && (r.isr & DMA_FLAGS) == 0U &&
           r.cs == CS;
}

static void exercise(spi_bus_t *bus, uint32_t n, uint32_t length, uint32_t chunks)
{
    static const spi_bus_config_t config = {18000000U, SPI_BUS_MODE_0, 1U};
    volatile run_t *const out = &g_spi_dma_fixture.runs[n];
    g_spi_dma_fixture.current = n;
    out->source = SOURCE;
    out->length = length;
    out->acquire = spi_bus_acquire(bus, &owner, &config);
    out->acquired = snapshot(bus);
    RTOS_ASSERT(out->acquire == SPI_BUS_OK && out->acquired.state == SPI_BUS_STATE_BUSY &&
                out->acquired.owner == (uintptr_t)&owner && out->acquired.depth == 1U &&
                out->acquired.error == SPI_BUS_OK &&
                (out->acquired.regs.cr1 & (SPI_CR1_BR | SPI_CR1_CPOL | SPI_CR1_CPHA)) == 0U &&
                out->acquired.regs.cs == CS);
    const uint32_t start = DWT->CYCCNT;
    out->transfer = spi_bus_transfer(bus, &owner, (const uint8_t *)SOURCE, NULL, length);
    out->cycles = DWT->CYCCNT - start;
    out->transferred = snapshot(bus);
    out->release = spi_bus_release(bus, &owner);
    out->released = snapshot(bus);
    RTOS_ASSERT(out->transfer == (n == FAULT ? SPI_BUS_ERR_TIMEOUT : SPI_BUS_OK) &&
                out->chunks == chunks && out->release == 1 && out->released.depth == 0U &&
                out->released.owner == 0U && clean(out->released.regs));
    if (n == PROBE) {
        const volatile chunk_t *probe = &out->chunk[0];
        RTOS_ASSERT(probe->initial.cmar == SOURCE && probe->initial.cndtr == 16U &&
                    probe->waited.cndtr == 0U && (probe->waited.isr & DMA_TX_TC) != 0U &&
                    (probe->waited.isr & DMA_TX_TE) == 0U);
    }
    g_spi_dma_fixture.completed = n + 1U;
}

static void test_task(void *unused)
{
    (void)unused;
    static const uint32_t lengths[] = {16U, 15U, 16U, 65535U, 65536U, 131070U, 8192U, 16U};
    static const uint32_t chunks[] = {1U, 0U, 1U, 1U, 2U, 2U, 1U, 1U};
    spi_bus_t *const bus = BSP_SPI3_GetBus();
    RTOS_ASSERT(bus != NULL);
    const spi_stm32_ll_t *const ll = bus->ctx;
    g_spi_dma_fixture.hclk = SystemCoreClock;
    g_spi_dma_fixture.pclk = ll->hw->pclk_hz;
    g_spi_dma_fixture.rcc_cfgr = RCC->CFGR;
    g_spi_dma_fixture.dwt_ctrl = DWT->CTRL;
    RTOS_ASSERT(SystemCoreClock == 72000000U && ll->hw->pclk_hz == 36000000U &&
                (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
    g_spi_dma_fixture.state = 1U;
    for (uint32_t i = 0U; i < TRANSFERS; ++i) exercise(bus, i, lengths[i], chunks[i]);
    __DMB();
    g_spi_dma_fixture.state = 2U;
    rtos_task_exit();
}

_Noreturn void spi_dma_fixture_start(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPDEN;
    (void)RCC->APB2ENR;
    GPIOD->BSRR = CS;
    /* PD11 general-purpose push-pull output; preload high before enabling output. */
    MODIFY_REG(GPIOD->CRH, 0xFU << 12, 2U << 12);
    rtos_task_ref_t ref;
    RTOS_ASSERT(rtos_task_create_with_ref(&ref, "SPI fixture", test_task, NULL,
                display_stacks[0], 4096U / sizeof(uint32_t), 5U) == RTOS_OK);
    rtos_start();
}
