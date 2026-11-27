#if defined(INSIGHT_TEST_SPI_DMA)
#include "spi_dma_fixture.h"
#endif

/**
 * @file    spi_stm32_ll.c
 * @brief   SPI 总线的 STM32F1 LL 实现
 * @details 负责 GPIO 复用、SPI 外设时钟/参数配置与阻塞式全双工传输。
 *          这是本工程中唯一产生 SPI 时序的文件；上层只看到 spi_bus.h。
 *
 *          错误处理统一用 spi_bus_status_t：
 *          - 参数非法        SPI_BUS_ERR_PARAM
 *          - 等待超时        SPI_BUS_ERR_TIMEOUT
 *          - DMA 传输错误    SPI_BUS_ERR_DMA（TEIF）
 *          - 外设忙无法配置  SPI_BUS_ERR_BUSY
 *          - 恢复失败        SPI_BUS_ERR_RECOVERY
 *          传输错误会尝试恢复 DMA/SPI，保留原传输错误码；
 *          总线释放后，下次 acquire 会再次恢复，失败则维持 ERROR。
 */
#include "spi_stm32_ll.h"
#include <string.h>

/* 超时预算（单位：CPU 周期，72 MHz）。基于实际传输时间 + 余量，非随意循环次数。 */
#define SPI_BYTE_TIMEOUT_CYCLES   500000U   /* 单字节 TXE/RXNE：约 7 ms 余量充足 */
#define SPI_BSY_TIMEOUT_CYCLES    500000U   /* BSY 清零等待：约 7 ms */
#define SPI_RECOVER_IDLE_CYCLES   1000000U  /* 恢复阶段等待外设空闲：约 14 ms */
#define SPI_DMA_MARGIN_MS         20U       /* 启动、抢占与观测余量 */

#define SPI_DMA_MIN_LEN           16U       /* 小于该长度走轮询，DMA 启动开销更大 */
#define SPI_DMA_MAX_CHUNK         65535U    /* CNDTR 为 16 位，单次 DMA 上限 */

/* 调试计数（HIL 验收用） */
volatile uint32_t g_ll_recover_count = 0U;
volatile int32_t  g_ll_last_error    = 0;

/* STM32F1 has one receive buffer. With DMA stopped and no more SCK,
 * the LL DR->SR sequence drains RXNE and clears OVR. A stuck flag is
 * an error rather than a reason to read DR indefinitely. */
static inline int spi_clear_rx(SPI_TypeDef *spi)
{
    LL_SPI_ClearFlag_OVR(spi);
    return (spi->SR & (SPI_SR_RXNE | SPI_SR_OVR)) == 0U;
}

/* ================================================================================== */
/* 时钟使能                                                           */
/* ================================================================================== */

static void gpio_clock_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOA);
    } else if (port == GPIOB) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOB);
    } else if (port == GPIOC) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOC);
    } else if (port == GPIOD) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOD);
    } else if (port == GPIOE) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOE);
    }
}

static void spi_clock_enable(SPI_TypeDef *periph)
{
    if (periph == SPI1) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    } else if (periph == SPI2) {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    } else if (periph == SPI3) {
        LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI3);
    }
}

static void dma_clock_enable(DMA_TypeDef *dma)
{
    if (dma == DMA1) {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    } else if (dma == DMA2) {
        LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);
    }
}

/* ================================================================================== */
/* DWT 计时辅助（基于实际时间，处理计数器 32 位回绕）                                 */
/* ================================================================================== */

static inline uint32_t ll_dwt_cycles(void)
{
    return DWT->CYCCNT;
}

/* Sample time before status, so a completion latched while preempted
 * is observed before deciding that the peripheral is still stalled. */
static int spi_wait_status(SPI_TypeDef *spi, uint32_t mask, uint32_t expected,
                           uint32_t timeout)
{
    const uint32_t start = ll_dwt_cycles();

    for (;;) {
        const uint32_t elapsed = ll_dwt_cycles() - start;
        if ((spi->SR & mask) == expected) {
            return 1;
        }
        if (elapsed >= timeout) {
            return 0;
        }
    }
}

/* ================================================================================== */
/* 参数换算                                                           */
/* ================================================================================== */

/** @brief 按目标频率选择不大于目标值的最近分频（取整到 2 的幂） */
static uint32_t spi_select_prescaler(uint32_t pclk_hz, uint32_t target_hz)
{
    uint32_t div;

    if (target_hz == 0U || pclk_hz == 0U) {
        return LL_SPI_BAUDRATEPRESCALER_DIV8;
    }

    for (div = 2U; div < 256U; div <<= 1) {
        if ((pclk_hz / div) <= target_hz) {
            break;
        }
    }
    if (div > 256U) {
        div = 256U;
    }

    switch (div) {
        case 2U:   return LL_SPI_BAUDRATEPRESCALER_DIV2;
        case 4U:   return LL_SPI_BAUDRATEPRESCALER_DIV4;
        case 8U:   return LL_SPI_BAUDRATEPRESCALER_DIV8;
        case 16U:  return LL_SPI_BAUDRATEPRESCALER_DIV16;
        case 32U:  return LL_SPI_BAUDRATEPRESCALER_DIV32;
        case 64U:  return LL_SPI_BAUDRATEPRESCALER_DIV64;
        case 128U: return LL_SPI_BAUDRATEPRESCALER_DIV128;
        default:   return LL_SPI_BAUDRATEPRESCALER_DIV256;
    }
}

/* ================================================================================== */
/* 抽象总线操作实现                                                     */
/* ================================================================================== */

static int ll_configure(spi_bus_t *bus, const spi_bus_config_t *cfg)
{
    spi_stm32_ll_t *ll = (spi_stm32_ll_t *)bus->ctx;
    SPI_TypeDef *spi = ll->hw->periph;
    uint8_t need_spi_init;

    if (cfg == NULL || ll->hw == NULL) {
        return (int)SPI_BUS_ERR_PARAM;
    }
    /* 非法时钟：0 或超过外设时钟 */
    if (cfg->clock_hz == 0U || cfg->clock_hz > ll->hw->pclk_hz) {
        return (int)SPI_BUS_ERR_PARAM;
    }
    /* 非法模式 */
    if (cfg->mode > SPI_BUS_MODE_3) {
        return (int)SPI_BUS_ERR_PARAM;
    }
    /* 外设正忙或 DMA 通道仍在运行时禁止改配置 */
    if (LL_SPI_IsActiveFlag_BSY(spi)) {
        return (int)SPI_BUS_ERR_BUSY;
    }
    if (ll->hw->dma != NULL) {
        if (LL_DMA_IsEnabledChannel(ll->hw->dma, ll->hw->dma_tx_ch) ||
            LL_DMA_IsEnabledChannel(ll->hw->dma, ll->hw->dma_rx_ch)) {
            return (int)SPI_BUS_ERR_BUSY;
        }
    }

    /* SPI 寄存器只依赖 clock_hz 与 mode；use_dma 仅影响传输路径，无需改外设 */
    need_spi_init = (uint8_t)(!ll->configured ||
                              ll->current.clock_hz != cfg->clock_hz ||
                              ll->current.mode     != cfg->mode);

    if (need_spi_init) {
        LL_SPI_Disable(spi);

        LL_SPI_SetBaudRatePrescaler(spi, spi_select_prescaler(ll->hw->pclk_hz, cfg->clock_hz));

        switch (cfg->mode) {
            case SPI_BUS_MODE_1:
                LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_LOW);
                LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_2EDGE);
                break;
            case SPI_BUS_MODE_2:
                LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_HIGH);
                LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_1EDGE);
                break;
            case SPI_BUS_MODE_3:
                LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_HIGH);
                LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_2EDGE);
                break;
            case SPI_BUS_MODE_0:
            default:
                LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_LOW);
                LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_1EDGE);
                break;
        }

        LL_SPI_Enable(spi);
        if (!spi_clear_rx(spi)) {
            ll->configured = 0U;
            return (int)SPI_BUS_ERR_RECOVERY;
        }
    }

    /* use_dma 变化必须落到 current，无论是否重配外设 */
    ll->current = *cfg;
    ll->configured = 1U;
    return (int)SPI_BUS_OK;
}

/* ================================================================================== */
/* DMA 辅助                                                           */
/* ================================================================================== */

/** @brief GIF/TCIF/HTIF/TEIF for one DMA channel (ISR and IFCR share positions). */
static uint32_t dma_flag_mask(uint32_t ch, uint32_t flags)
{
    return flags << ((ch - 1U) * 4U);
}

static void spi_stop_dma(spi_stm32_ll_t *ll)
{
    SPI_TypeDef *spi = ll->hw->periph;
    DMA_TypeDef *dma = ll->hw->dma;

    LL_SPI_DisableDMAReq_TX(spi);
    LL_SPI_DisableDMAReq_RX(spi);
    if (dma != NULL) {
        LL_DMA_DisableChannel(dma, ll->hw->dma_tx_ch);
        LL_DMA_DisableChannel(dma, ll->hw->dma_rx_ch);
        /* Writing CGIF clears all four flags for that channel. */
        dma->IFCR = dma_flag_mask(ll->hw->dma_tx_ch, DMA_IFCR_CGIF1) |
                    dma_flag_mask(ll->hw->dma_rx_ch, DMA_IFCR_CGIF1);
    }
}

/**
 * @brief Compute a common TX/RX deadline for one 8-bit DMA chunk.
 * @return DWT cycles, or zero for an unsupported clock/budget.
 */
static uint32_t dma_timeout_cycles(const spi_stm32_ll_t *ll, uint32_t chunk)
{
    const uint32_t pclk = ll->hw->pclk_hz;
    const uint32_t hclk = SystemCoreClock;
    const uint32_t br = LL_SPI_GetBaudRatePrescaler(ll->hw->periph) >> SPI_CR1_BR_Pos;
    const uint32_t divider = 2U << br;

    if (pclk == 0U || hclk == 0U) {
        return 0U;
    }

    /* Ceil(chunk * 8 * HCLK / SCK), with SCK = PCLK / divider.
     * Add 25% for bus service gaps and 20 ms for setup/scheduling. */
    const uint64_t numerator = (uint64_t)chunk * 8U * divider * hclk;
    const uint64_t wire = (numerator + pclk - 1U) / pclk;
    const uint64_t margin = ((uint64_t)hclk * SPI_DMA_MARGIN_MS + 999U) / 1000U;
    const uint64_t budget = wire + (wire + 3U) / 4U + margin;
    return budget <= UINT32_MAX / 2U ? (uint32_t)budget : 0U;
}

static spi_bus_status_t dma_wait(DMA_TypeDef *dma, uint32_t tc_mask, uint32_t te_mask,
                                 uint32_t start, uint32_t timeout)
{
    for (;;) {
        const uint32_t elapsed = ll_dwt_cycles() - start;
        const uint32_t flags = dma->ISR;

        if ((flags & te_mask) != 0U) {
            return SPI_BUS_ERR_DMA;
        }
        if ((flags & tc_mask) == tc_mask) {
            return SPI_BUS_OK;
        }
        if (elapsed >= timeout) {
            return SPI_BUS_ERR_TIMEOUT;
        }
    }
}

/** @brief 配置一个 DMA 通道（字节宽度、单次模式、内存自增） */
static void dma_setup_channel(DMA_TypeDef *dma, uint32_t ch,
                              const uint8_t *mem, uint32_t periph_addr,
                              uint32_t direction, uint32_t len)
{
    LL_DMA_DisableChannel(dma, ch);
    dma->IFCR = dma_flag_mask(ch, DMA_IFCR_CGIF1);

    LL_DMA_SetDataTransferDirection(dma, ch, direction);
    LL_DMA_SetMode(dma, ch, LL_DMA_MODE_NORMAL);
    LL_DMA_SetPeriphIncMode(dma, ch, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(dma, ch, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(dma, ch, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(dma, ch, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetChannelPriorityLevel(dma, ch, LL_DMA_PRIORITY_HIGH);

    if (direction == LL_DMA_DIRECTION_MEMORY_TO_PERIPH) {
        LL_DMA_ConfigAddresses(dma, ch, (uint32_t)(uintptr_t)mem, periph_addr, direction);
    } else {
        LL_DMA_ConfigAddresses(dma, ch, periph_addr, (uint32_t)(uintptr_t)mem, direction);
    }
    LL_DMA_SetDataLength(dma, ch, len);
}

/* ================================================================================== */
/* 统一恢复                                                           */
/* ================================================================================== */

/**
 * @brief 把 SPI/DMA 拉回可用态：关闭 DMA 请求、禁用通道、清标志、
 *        等待 BSY 清零、读 DR/SR 清 OVR；必要时重新初始化 SPI。
 * @note  不触碰任何 CS 引脚——片选由 port 层按 depth 管理。
 */
static spi_bus_status_t spi_recover(spi_stm32_ll_t *ll)
{
    SPI_TypeDef *spi = ll->hw->periph;
    ll->recover_count++;
    g_ll_recover_count++;
    spi_stop_dma(ll);

    /* The master may still be shifting the last frame after DMA stops.
     * Treat a TX buffer that never empties as a recovery failure too. */
    const uint32_t idle_mask = SPI_SR_TXE | SPI_SR_BSY;
    if (!spi_wait_status(spi, idle_mask, SPI_SR_TXE, SPI_RECOVER_IDLE_CYCLES)) {
        LL_SPI_Disable(spi);
        LL_SPI_Enable(spi);
    }
    if ((spi->SR & idle_mask) != SPI_SR_TXE || !spi_clear_rx(spi)) {
        ll->configured = 0U;
        return SPI_BUS_ERR_RECOVERY;
    }

    /* 5. 强制按当前配置重配一次，确保寄存器处于已知状态（若尚未配置则维持初始化默认） */
    if (ll->current.clock_hz > 0U) {
        spi_bus_config_t saved = ll->current;
        ll->configured = 0U;                 /* 强制走完整初始化分支 */
        if (ll_configure(&ll->bus, &saved) != (int)SPI_BUS_OK) {
            return SPI_BUS_ERR_RECOVERY;
        }
    }

    return SPI_BUS_OK;
}

/* ================================================================================== */
/* 传输实现                                                           */
/* ================================================================================== */

/**
 * @brief DMA 传输（阻塞），自动处理 >65535 的分块
 * @note  分块期间 CS 由 port 层保持（不 release、不 configure）。
 */
static spi_bus_status_t ll_transfer_dma(spi_stm32_ll_t *ll,
                                       const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    SPI_TypeDef *spi = ll->hw->periph;
    DMA_TypeDef *dma = ll->hw->dma;
    uint32_t txc = ll->hw->dma_tx_ch;
    uint32_t rxc = ll->hw->dma_rx_ch;
    uint32_t offset = 0U;
    spi_bus_status_t status = SPI_BUS_OK;
    uint32_t tc_mask = dma_flag_mask(txc, DMA_ISR_TCIF1);
    uint32_t te_mask = dma_flag_mask(txc, DMA_ISR_TEIF1);

    if (rx != NULL) {
        tc_mask |= dma_flag_mask(rxc, DMA_ISR_TCIF1);
        te_mask |= dma_flag_mask(rxc, DMA_ISR_TEIF1);
    }
    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > SPI_DMA_MAX_CHUNK) {
            chunk = SPI_DMA_MAX_CHUNK;
        }

        const uint32_t timeout = dma_timeout_cycles(ll, chunk);
        if (timeout == 0U) {
            status = SPI_BUS_ERR_PARAM;
            break;
        }
        if (!spi_clear_rx(spi)) {
            status = SPI_BUS_ERR_TIMEOUT;
            break;
        }

        /* 先 RX 后 TX，保证时钟到来前接收端就绪 */
        if (rx != NULL) {
            dma_setup_channel(dma, rxc, rx + offset, LL_SPI_DMA_GetRegAddr(spi),
                              LL_DMA_DIRECTION_PERIPH_TO_MEMORY, chunk);
        }
        dma_setup_channel(dma, txc, tx + offset, LL_SPI_DMA_GetRegAddr(spi),
                          LL_DMA_DIRECTION_MEMORY_TO_PERIPH, chunk);

#if defined(INSIGHT_TEST_SPI_DMA)
        spi_dma_fixture_hook(ll, SPI_DMA_TEST_ARMED, offset, chunk, timeout, 0U, SPI_BUS_OK);
#endif
        const uint32_t start = ll_dwt_cycles();
        if (rx != NULL) {
            LL_SPI_EnableDMAReq_RX(spi);
            LL_DMA_EnableChannel(dma, rxc);
        }
        LL_SPI_EnableDMAReq_TX(spi);
        LL_DMA_EnableChannel(dma, txc);
#if defined(INSIGHT_TEST_SPI_DMA)
        spi_dma_fixture_hook(ll, SPI_DMA_TEST_ACTIVE, offset, chunk, timeout, start, SPI_BUS_OK);
#endif

        status = dma_wait(dma, tc_mask, te_mask, start, timeout);
#if defined(INSIGHT_TEST_SPI_DMA)
        spi_dma_fixture_hook(ll, SPI_DMA_TEST_WAITED, offset, chunk, timeout, start, status);
#endif
        if (status != SPI_BUS_OK) {
            break;
        }

        /* TC covers DMA beats. The SPI pipeline must also empty before
         * releasing CS or arming the next chunk. Bound each tail wait. */
        if (!spi_wait_status(spi, SPI_SR_TXE, SPI_SR_TXE, SPI_BYTE_TIMEOUT_CYCLES) ||
            !spi_wait_status(spi, SPI_SR_BSY, 0U, SPI_BSY_TIMEOUT_CYCLES)) {
            status = SPI_BUS_ERR_TIMEOUT;
            break;
        }

#if defined(INSIGHT_TEST_SPI_DMA)
        spi_dma_fixture_hook(ll, SPI_DMA_TEST_DRAINED, offset, chunk, timeout, start, status);
#endif
        spi_stop_dma(ll);
        if (rx == NULL && !spi_clear_rx(spi)) {
            status = SPI_BUS_ERR_TIMEOUT;
            break;
        }
        offset += chunk;
    }

    if (status != SPI_BUS_OK) {
        /* Recovery first disables DMA requests and both channels, so the
         * caller can release the bus and reuse the source/destination. */
        (void)spi_recover(ll);
    }
    return status;
}

/** @brief 轮询传输：每字节 TXE/RXNE 等待均带超时，末尾等 BSY 清零 */
static spi_bus_status_t ll_transfer_poll(spi_stm32_ll_t *ll,
                                        const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    SPI_TypeDef *spi = ll->hw->periph;

    if (!spi_clear_rx(spi)) {
        goto timeout;
    }
    for (uint32_t i = 0U; i < len; i++) {
        if (!spi_wait_status(spi, SPI_SR_TXE, SPI_SR_TXE, SPI_BYTE_TIMEOUT_CYCLES)) {
            goto timeout;
        }
        LL_SPI_TransmitData8(spi, tx[i]);

        if (!spi_wait_status(spi, SPI_SR_RXNE, SPI_SR_RXNE, SPI_BYTE_TIMEOUT_CYCLES)) {
            goto timeout;
        }
        const uint8_t byte = LL_SPI_ReceiveData8(spi);
        if (rx != NULL) {
            rx[i] = byte;
        }
    }

    if (!spi_wait_status(spi, SPI_SR_BSY, 0U, SPI_BSY_TIMEOUT_CYCLES) ||
        (rx == NULL && !spi_clear_rx(spi))) {
        goto timeout;
    }

    return SPI_BUS_OK;

timeout:
    (void)spi_recover(ll);
    return SPI_BUS_ERR_TIMEOUT;
}

static int ll_transfer(spi_bus_t *bus, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    spi_stm32_ll_t *ll = (spi_stm32_ll_t *)bus->ctx;
    int ret;

    if (tx == NULL || len == 0U) {
        return (int)SPI_BUS_ERR_PARAM;
    }
    if (ll->current.use_dma && ll->hw->dma != NULL && len >= SPI_DMA_MIN_LEN) {
        ret = (int)ll_transfer_dma(ll, tx, rx, len);
    } else {
        ret = (int)ll_transfer_poll(ll, tx, rx, len);
    }
    ll->last_error = ret;
    if (ret != (int)SPI_BUS_OK) {
        g_ll_last_error = ret;
    }
    return ret;
}

/** @brief 恢复操作：供 spi_bus_recover() 通过 ops->recover 调用 */
static int ll_recover_op(spi_bus_t *bus)
{
    spi_stm32_ll_t *ll = (spi_stm32_ll_t *)bus->ctx;

    if (ll == NULL || ll->hw == NULL) {
        return (int)SPI_BUS_ERR_PARAM;
    }
    return (int)spi_recover(ll);
}

static int ll_in_isr(spi_bus_t *bus)
{
    (void)bus;
    return (__get_IPSR() != 0U) ? 1 : 0;
}

static const spi_bus_ops_t s_stm32_ll_ops = {
    .configure = ll_configure,
    .transfer  = ll_transfer,
    .recover   = ll_recover_op,
    .in_isr    = ll_in_isr,
};

/* ================================================================================== */
/* 对外接口                                                           */
/* ================================================================================== */

void spi_stm32_ll_init(spi_stm32_ll_t *ll, const spi_stm32_ll_hw_t *hw)
{
    SPI_TypeDef *spi;
    LL_GPIO_InitTypeDef gpio = {0};

    if (ll == NULL || hw == NULL) {
        return;
    }

    ll->hw = hw;
    ll->bus.ops = &s_stm32_ll_ops;
    ll->bus.ctx = ll;
    ll->bus.owner = NULL;
    ll->bus.depth = 0U;
    ll->bus.state = SPI_BUS_STATE_UNINIT;
    ll->bus.last_error = SPI_BUS_OK;
    ll->bus.rejects = 0U;
    ll->configured = 0U;
    ll->recover_count = 0U;
    ll->last_error = 0;
    memset(&ll->current, 0, sizeof(ll->current));

    spi = hw->periph;

    /* SPI3 默认使用 PB3/PB4，这两个引脚复位后是 JTAG 功能，需先关闭 JTAG（保留 SWD） */
    if (spi == SPI3) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_AFIO);
        LL_GPIO_AF_Remap_SWJ_NOJTAG();
    }

    gpio_clock_enable(hw->sck_port);
    gpio_clock_enable(hw->miso_port);
    gpio_clock_enable(hw->mosi_port);
    spi_clock_enable(spi);
    if (hw->dma != NULL) {
        dma_clock_enable(hw->dma);
    }

    /* SCK / MOSI：复用推挽；MISO：浮空输入 */
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Pin = hw->sck_pin;
    LL_GPIO_Init(hw->sck_port, &gpio);
    gpio.Pin = hw->mosi_pin;
    LL_GPIO_Init(hw->mosi_port, &gpio);

    gpio.Mode = LL_GPIO_MODE_FLOATING;
    gpio.Pin = hw->miso_pin;
    LL_GPIO_Init(hw->miso_port, &gpio);

    /* SPI 主机、全双工、8 位、软件 NSS（片选由各设备自己管） */
    LL_SPI_Disable(spi);
    LL_SPI_SetMode(spi, LL_SPI_MODE_MASTER);
    LL_SPI_SetTransferDirection(spi, LL_SPI_FULL_DUPLEX);
    LL_SPI_SetDataWidth(spi, LL_SPI_DATAWIDTH_8BIT);
    LL_SPI_SetNSSMode(spi, LL_SPI_NSS_SOFT);
    LL_SPI_SetBaudRatePrescaler(spi, LL_SPI_BAUDRATEPRESCALER_DIV8);
    LL_SPI_SetClockPolarity(spi, LL_SPI_POLARITY_LOW);
    LL_SPI_SetClockPhase(spi, LL_SPI_PHASE_1EDGE);
    LL_SPI_Enable(spi);

    /* 首次 acquire 会再次核验 RXNE/OVR。 */
    (void)spi_clear_rx(spi);
}

spi_bus_t *spi_stm32_ll_bus(spi_stm32_ll_t *ll)
{
    return (ll == NULL) ? NULL : &ll->bus;
}

int spi_stm32_ll_recover(spi_stm32_ll_t *ll)
{
    if (ll == NULL || ll->hw == NULL) {
        return (int)SPI_BUS_ERR_PARAM;
    }
    /* 走抽象层，保证状态机与 last_error 同步更新 */
    return spi_bus_recover(&ll->bus);
}

uint32_t spi_stm32_ll_get_recover_count(const spi_stm32_ll_t *ll)
{
    return (ll == NULL) ? 0U : ll->recover_count;
}

int32_t spi_stm32_ll_get_last_error(const spi_stm32_ll_t *ll)
{
    return (ll == NULL) ? 0 : ll->last_error;
}

void spi_stm32_ll_clear_error(spi_stm32_ll_t *ll)
{
    if (ll != NULL) {
        ll->last_error = 0;
    }
}
