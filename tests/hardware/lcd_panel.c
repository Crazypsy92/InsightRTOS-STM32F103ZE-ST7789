#include "lcd_panel.h"
#include "display_app.h"
#include "st7789_port.h"
#include "delay.h"

enum { PATTERNS = 7, FRAME_MS = 1500, RUN_MS = 622000, CS = 1U << 11, DC = 1U << 12 };
#define SPI_MASK (SPI_SR_TXE | SPI_SR_RXNE | SPI_SR_BSY | SPI_SR_OVR)
#define DMA_ENABLES (SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN)
#define DMA_FLAGS UINT32_C(0xff)

typedef struct {
    uint32_t cmar, cndtr, ccr, rx_ccr, isr, cr1, cr2, sr, pins;
    uint32_t owner, depth, state, recover;
    int32_t last_error;
} registers_t;

typedef struct {
    uint32_t length;
    registers_t armed, waited, drained, returned, released;
} sample_t;

typedef struct {
    uint32_t dma, poll, command_writes, data_writes, selects, releases, returns;
    uint32_t dma_buckets[6], natural_ovr, invariants, failures;
    uint32_t active_since, maximum_transaction_cycles;
    sample_t row;
    registers_t last_release;
} bus_counts_t;

static bus_counts_t counters;
static sample_t active_row;
static bool capture_row;
static bool release_row;
static uint32_t start_ms, end_ms, last_frame_ms, full_frames, local_updates, pattern_counts[PATTERNS];
static uint32_t state, preinit_pins;
static bool label_drawn;

volatile struct {
    uint32_t seq_begin, state, hclk, rcc_cfgr, start_ms, end_ms, now_ms, full_frames, local_updates;
    uint32_t pattern_counts[PATTERNS], preinit_pins;
    bus_counts_t bus;
    int32_t lcd_error;
    uint32_t seq_end;
} g_lcd_panel_diag;

static registers_t read_regs(const spi_bus_t *bus)
{
    const spi_stm32_ll_t *ll = bus->ctx;
    return (registers_t){DMA2_Channel2->CMAR, DMA2_Channel2->CNDTR, DMA2_Channel2->CCR,
        DMA2_Channel1->CCR, DMA2->ISR, SPI3->CR1, SPI3->CR2, SPI3->SR, GPIOD->IDR & (CS | DC),
        (uintptr_t)bus->owner, bus->depth, bus->state, ll->recover_count, bus->last_error};
}

static bool drained(registers_t r)
{
    return (r.sr & SPI_MASK) == SPI_SR_TXE && (r.cr2 & DMA_ENABLES) == 0U &&
           ((r.ccr | r.rx_ccr) & DMA_CCR_EN) == 0U && (r.isr & DMA_FLAGS) == 0U;
}

void LCDPanel_PreInit(void)
{
    Display_RequireLCDTask();
    RCC->APB2ENR |= RCC_APB2ENR_IOPDEN;
    (void)RCC->APB2ENR;
    GPIOD->BSRR = CS;
    MODIFY_REG(GPIOD->CRH, 0xfU << 12U, 2U << 12U);
    preinit_pins = GPIOD->IDR & (CS | DC);
}

void LCDPanel_Select(const spi_bus_t *bus, const void *owner)
{
    ++counters.selects;
    counters.active_since = DWT->CYCCNT;
    if ((GPIOD->IDR & CS) != 0U || bus->owner != owner || bus->depth != 1U ||
        bus->state != SPI_BUS_STATE_BUSY) ++counters.invariants;
}

void LCDPanel_Poll(const spi_stm32_ll_t *ll, uint32_t length, bool tx_only)
{
    ++counters.poll;
    if (ll->hw->periph != SPI3 || !tx_only || (GPIOD->IDR & CS) != 0U ||
        length >= 16U) ++counters.invariants;
    if ((GPIOD->IDR & DC) == 0U) {
        ++counters.command_writes;
        if (length != 1U) ++counters.invariants;
    } else {
        ++counters.data_writes;
    }
}

void LCDPanel_DMA(const spi_stm32_ll_t *ll, panel_dma_stage_t stage, uint32_t length,
                  spi_bus_status_t status, bool tx_only)
{
    const registers_t r = read_regs(&ll->bus);
    if (ll->hw->periph != SPI3 || ll->hw->dma != DMA2 || ll->hw->dma_tx_ch != 2U ||
        ll->hw->dma_rx_ch != 1U || !tx_only || r.pins != DC || r.owner == 0U || r.depth != 1U ||
        r.state != SPI_BUS_STATE_BUSY || (r.rx_ccr & DMA_CCR_EN) != 0U ||
        (r.cr2 & SPI_CR2_RXDMAEN) != 0U || r.recover != 0U || r.last_error != 0)
        ++counters.invariants;
    if (stage == PANEL_DMA_ARMED) {
        ++counters.dma;
        ++counters.data_writes;
        const uint32_t bucket = length < 64U ? 0U : length < 240U ? 1U : length == 240U ? 2U :
                                length < 480U ? 3U : length == 480U ? 4U : 5U;
        ++counters.dma_buckets[bucket];
        if (length < 16U || r.cndtr != length || (r.ccr & 0xfd1U) != 0x90U ||
            (r.cr1 & (SPI_CR1_BR | SPI_CR1_CPHA | SPI_CR1_CPOL)) != 0U ||
            (r.isr & DMA_FLAGS) != 0U || r.cmar < 0x20000000U || r.cmar > 0x20010000U - length)
            ++counters.invariants;
        capture_row = length == 480U;
        if (capture_row) {
            active_row.length = length;
            active_row.armed = r;
        }
        return;
    }
    if (status != SPI_BUS_OK || r.cndtr != 0U || (r.isr & 0xaaU) != DMA_ISR_TCIF2 ||
        (r.cr2 & DMA_ENABLES) != SPI_CR2_TXDMAEN) ++counters.invariants;
    if (stage == PANEL_DMA_WAITED) {
        if (capture_row) active_row.waited = r;
    } else {
        if ((r.sr & (SPI_SR_TXE | SPI_SR_BSY)) != SPI_SR_TXE) ++counters.invariants;
        if ((r.sr & SPI_SR_OVR) != 0U) ++counters.natural_ovr;
        if (capture_row) active_row.drained = r;
    }
}

void LCDPanel_WriteReturned(const spi_bus_t *bus, const void *owner, int status)
{
    const registers_t r = read_regs(bus);
    ++counters.returns;
    if (status != SPI_BUS_OK) ++counters.failures;
    if (r.owner != (uintptr_t)owner || r.depth != 1U || r.state != SPI_BUS_STATE_BUSY ||
        (r.pins & CS) != 0U || !drained(r)) ++counters.invariants;
    if (capture_row) {
        active_row.returned = r;
        release_row = true;
        capture_row = false;
    }
}

void LCDPanel_Release(const spi_bus_t *bus)
{
    const registers_t r = read_regs(bus);
    ++counters.releases;
    const uint32_t duration = DWT->CYCCNT - counters.active_since;
    if (duration > counters.maximum_transaction_cycles) counters.maximum_transaction_cycles = duration;
    if (r.owner != 0U || r.depth != 0U || r.state != SPI_BUS_STATE_READY || r.last_error != 0 ||
        (r.pins & CS) == 0U || !drained(r)) ++counters.invariants;
    counters.last_release = r;
    if (release_row) {
        active_row.released = r;
        counters.row = active_row;
        release_row = false;
    }
}

static LCD_Result draw(uint32_t pattern)
{
    static const uint16_t colors[] = {BLACK, RED, GREEN, BLUE, WHITE};
    if (pattern < 5U) return LCD_Clear(&g_lcd, colors[pattern]);
    if (pattern == 5U) {
        static const uint16_t quadrants[] = {RED, GREEN, BLUE, WHITE};
        for (uint16_t i = 0U; i < 4U; ++i) {
            LCD_Result ret = LCD_Fill(&g_lcd, (i & 1U) * 120U, (i >> 1U) * 120U,
                                      120U, 120U, quadrants[i]);
            if (ret != LCD_OK) return ret;
        }
        return LCD_OK;
    }
    LCD_Result ret = LCD_Clear(&g_lcd, BLACK);
    for (uint16_t i = 0U; i < 4U && ret == LCD_OK; ++i) {
        ret = LCD_Fill(&g_lcd, 8U, 16U + i * 14U, 100U, 12U, colors[i + 1U]);
        if (ret == LCD_OK) ret = LCD_Fill(&g_lcd, 124U + i * 24U, 16U, 22U, 54U, colors[i + 1U]);
    }
    if (ret == LCD_OK) ret = LCD_DrawRect(&g_lcd, 4U, 4U, 232U, 232U, WHITE);
    if (ret == LCD_OK) ret = LCD_DrawLineH(&g_lcd, 8U, 120U, 224U, GREEN);
    if (ret == LCD_OK) ret = LCD_DrawLineV(&g_lcd, 120U, 75U, 142U, RED);
    return ret;
}

bool LCDPanel_Step(bool *entering)
{
    Display_RequireLCDTask();
    const uint32_t now = GetTick_ms();
    if (state == 2U) return false;
    if (state == 3U || counters.invariants != 0U || counters.failures != 0U) {
        state = 3U;
        return true;
    }
    if (state == 0U) {
        state = 1U;
        start_ms = now;
    }
    if (now - start_ms >= RUN_MS) {
        state = 2U;
        end_ms = now;
        *entering = true;
        return false;
    }
    if (full_frames == 0U || now - last_frame_ms >= FRAME_MS) {
        const uint32_t pattern = full_frames % PATTERNS;
        if (draw(pattern) != LCD_OK) state = 3U;
        else {
            ++pattern_counts[pattern];
            ++full_frames;
            last_frame_ms = GetTick_ms();
            label_drawn = false;
        }
    } else if (!label_drawn && now - last_frame_ms >= FRAME_MS / 2U) {
        LCD_Result ret = LCD_ShowString(&g_lcd, 8U, 216U, "ST7789 FRAME", &ASCII_Font16, WHITE, BLACK);
        if (ret == LCD_OK)
            ret = LCD_ShowIntNum(&g_lcd, 120U, 216U, (int32_t)full_frames, 4U, &ASCII_Font16, WHITE, BLACK);
        if (ret != LCD_OK) state = 3U;
        else {
            ++local_updates;
            label_drawn = true;
        }
    }
    return true;
}

void LCDPanel_Publish(void)
{
    const uint32_t next = g_lcd_panel_diag.seq_begin + 2U;
    g_lcd_panel_diag.seq_begin = next - 1U;
    __DMB();
    g_lcd_panel_diag.state = state;
    g_lcd_panel_diag.hclk = SystemCoreClock;
    g_lcd_panel_diag.rcc_cfgr = RCC->CFGR;
    g_lcd_panel_diag.start_ms = start_ms;
    g_lcd_panel_diag.end_ms = end_ms;
    g_lcd_panel_diag.now_ms = GetTick_ms();
    g_lcd_panel_diag.full_frames = full_frames;
    g_lcd_panel_diag.local_updates = local_updates;
    for (uint32_t i = 0; i < PATTERNS; ++i) g_lcd_panel_diag.pattern_counts[i] = pattern_counts[i];
    g_lcd_panel_diag.preinit_pins = preinit_pins;
    g_lcd_panel_diag.bus = counters;
    g_lcd_panel_diag.lcd_error = LCD_GetLastError(&g_lcd);
    g_lcd_panel_diag.seq_end = next;
    __DMB();
    g_lcd_panel_diag.seq_begin = next;
}
