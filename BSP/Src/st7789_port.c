/**
 * @file    st7789_port.c
 * @brief   ST7789 移植层实现（STM32F103 + LL）
 * @details 负责片选/数据命令/背光 GPIO 初始化、SPI 总线绑定与延时实现，
 *          并填充 LCD_Port 回调表。协议层（st7789.c）不感知任何 MCU 细节。
 *
 *          默认接线（与参考例程一致）：
 *          SPI3 SCK=PB3 / MOSI=PB5，CS=PD11，DC=PD12，BL=PD13，无 RST。
 */
#include "st7789_port.h"
#include "bsp_spi.h"
#include "delay.h"
#include <string.h>

/* ================================================================================== */
/* 默认板级配置                                                                        */
/* ================================================================================== */

#define ST7789_DEFAULT_SPI_CLOCK_HZ 18000000U          /* SPI3 36MHz / 2 = 18MHz */
#define ST7789_DEFAULT_SPI_MODE     SPI_BUS_MODE_0
#define ST7789_DEFAULT_SPI_USE_DMA  1U

#define ST7789_DEFAULT_CS_PORT      GPIOD
#define ST7789_DEFAULT_CS_PIN       LL_GPIO_PIN_11
#define ST7789_DEFAULT_DC_PORT      GPIOD
#define ST7789_DEFAULT_DC_PIN       LL_GPIO_PIN_12
#define ST7789_DEFAULT_BL_PORT      GPIOD
#define ST7789_DEFAULT_BL_PIN       LL_GPIO_PIN_13

#define ST7789_PORT_MAX_CHUNK       65535U             /* 单次传输上限（16 位长度） */

/* ================================================================================== */
/* 内部上下文                                                                          */
/* ================================================================================== */

typedef struct {
    LCD_Device       *owner_dev;   /* 绑定的设备句柄 */
    spi_bus_t        *bus;         /* SPI 抽象总线 */
    spi_bus_config_t  cfg;         /* 本设备的总线参数 */
    GPIO_TypeDef     *cs_port;
    uint32_t          cs_pin;
    GPIO_TypeDef     *dc_port;
    uint32_t          dc_pin;
    GPIO_TypeDef     *bl_port;
    uint32_t          bl_pin;
    LCD_Port          port;        /* 对外暴露的回调表 */
    uint8_t           in_use;
} ST7789_HW;

static ST7789_HW s_lcd_hw;

/* 全局默认设备句柄，应用层可直接使用 */
LCD_Device g_lcd;

/* ================================================================================== */
/* 内部辅助                                                                            */
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

/* ================================================================================== */
/* LCD_Port 回调实现                                                                   */
/* ================================================================================== */

/** @brief 片选/总线占用：进入时申请总线，完全释放时拉高 CS */
static LCD_Result Port_Cs(void *ctx, uint8_t level)
{
    ST7789_HW *hw = (ST7789_HW *)ctx;
    if (hw == NULL || hw->bus == NULL) {
        return LCD_ERROR_PARAM;
    }

    if (level == 0U) {
        /* 占用总线并切换到本设备参数，成功后才拉低片选 */
        if (spi_bus_acquire(hw->bus, hw, &hw->cfg) != 0) {
            return LCD_ERROR_BUS;
        }
        LL_GPIO_ResetOutputPin(hw->cs_port, hw->cs_pin);
        return LCD_OK;
    }

    /* 仅当本次释放把嵌套深度归零时才拉高片选 */
    int rel = spi_bus_release(hw->bus, hw);
    if (rel > 0) {
        LL_GPIO_SetOutputPin(hw->cs_port, hw->cs_pin);
        return LCD_OK;
    }
    return (rel == 0) ? LCD_OK : LCD_ERROR_BUS;
}

/** @brief 数据/命令选择：0=命令，1=数据 */
static void Port_Dc(void *ctx, uint8_t level)
{
    ST7789_HW *hw = (ST7789_HW *)ctx;
    if (hw == NULL || hw->dc_port == NULL) {
        return;
    }
    if (level) {
        LL_GPIO_SetOutputPin(hw->dc_port, hw->dc_pin);
    } else {
        LL_GPIO_ResetOutputPin(hw->dc_port, hw->dc_pin);
    }
}

/** @brief 背光控制：高电平点亮 */
static void Port_Bl(void *ctx, uint8_t level)
{
    ST7789_HW *hw = (ST7789_HW *)ctx;
    if (hw == NULL || hw->bl_port == NULL) {
        return;
    }
    if (level) {
        LL_GPIO_SetOutputPin(hw->bl_port, hw->bl_pin);
    } else {
        LL_GPIO_ResetOutputPin(hw->bl_port, hw->bl_pin);
    }
}

/** @brief SPI 连续发送（必须先通过 CS 回调持有总线） */
static LCD_Result Port_Write(void *ctx, const uint8_t *buf, uint16_t len)
{
    ST7789_HW *hw = (ST7789_HW *)ctx;
    if (hw == NULL || hw->bus == NULL) {
        return LCD_ERROR_PARAM;
    }
    if (len == 0U) {
        return LCD_OK;
    }
    if (buf == NULL) {
        return LCD_ERROR_PARAM;
    }
    if (spi_bus_owner(hw->bus) != hw) {
        return LCD_ERROR_BUS;
    }
    int ret = spi_bus_transfer(hw->bus, hw, buf, NULL, len);
    if (ret == SPI_BUS_OK) {
        return LCD_OK;
    }
    return (ret == SPI_BUS_ERR_TIMEOUT) ? LCD_ERROR_TIMEOUT : LCD_ERROR_BUS;
}

static void Port_DelayMs(void *ctx, uint32_t ms)
{
    (void)ctx;
    Delay_ms(ms);
}

/* ================================================================================== */
/* 控制引脚初始化                                                                      */
/* ================================================================================== */

static void ST7789_GpioInit(const ST7789_HW *hw)
{
    LL_GPIO_InitTypeDef init = {0};

    gpio_clock_enable(hw->cs_port);
    gpio_clock_enable(hw->dc_port);
    gpio_clock_enable(hw->bl_port);

    init.Mode       = LL_GPIO_MODE_OUTPUT;
    init.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
    init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;

    init.Pin = hw->cs_pin;
    LL_GPIO_Init(hw->cs_port, &init);

    init.Pin = hw->dc_pin;
    LL_GPIO_Init(hw->dc_port, &init);

    init.Pin = hw->bl_pin;
    LL_GPIO_Init(hw->bl_port, &init);

    /* 空闲态：CS 高、DC 高（数据）、背光关闭 */
    LL_GPIO_SetOutputPin(hw->cs_port, hw->cs_pin);
    LL_GPIO_SetOutputPin(hw->dc_port, hw->dc_pin);
    LL_GPIO_ResetOutputPin(hw->bl_port, hw->bl_pin);
}

/* ================================================================================== */
/* 对外接口                                                                            */
/* ================================================================================== */

LCD_Result ST7789_PortInitWithConfig(LCD_Device *dev, const ST7789_PortConfig *cfg)
{
    if (dev == NULL || cfg == NULL) {
        return LCD_ERROR_PARAM;
    }

    ST7789_HW *hw = &s_lcd_hw;

    /* 单实例：已被其它设备占用则拒绝 */
    if (hw->in_use && hw->owner_dev != dev) {
        return LCD_ERROR_PARAM;
    }

    /* 重复初始化时先释放可能残留的总线锁 */
    if (hw->in_use && hw->bus != NULL && hw->bus->owner == hw) {
        while (hw->bus->depth > 0U) {
            if (spi_bus_release(hw->bus, hw) < 0) {
                return LCD_ERROR_BUS;
            }
        }
    }

    spi_bus_t *bus = (cfg->bus != NULL) ? cfg->bus : BSP_SPI3_GetBus();
    if (bus == NULL) {
        return LCD_ERROR_BUS;
    }

    memset(hw, 0, sizeof(*hw));
    hw->owner_dev = dev;
    hw->in_use    = 1U;
    hw->bus       = bus;

    hw->cfg.clock_hz = (cfg->spi_clock_hz > 0U) ? cfg->spi_clock_hz : ST7789_DEFAULT_SPI_CLOCK_HZ;
    hw->cfg.mode     = cfg->spi_mode;
    hw->cfg.use_dma  = (cfg->use_dma != 0U) ? 1U : 0U;

    hw->cs_port = (cfg->cs_port != NULL) ? cfg->cs_port : ST7789_DEFAULT_CS_PORT;
    hw->cs_pin  = (cfg->cs_pin  != 0U)     ? cfg->cs_pin  : ST7789_DEFAULT_CS_PIN;
    hw->dc_port = (cfg->dc_port != NULL) ? cfg->dc_port : ST7789_DEFAULT_DC_PORT;
    hw->dc_pin  = (cfg->dc_pin  != 0U)     ? cfg->dc_pin  : ST7789_DEFAULT_DC_PIN;
    hw->bl_port = (cfg->bl_port != NULL) ? cfg->bl_port : ST7789_DEFAULT_BL_PORT;
    hw->bl_pin  = (cfg->bl_pin  != 0U)     ? cfg->bl_pin  : ST7789_DEFAULT_BL_PIN;

    hw->port.cs            = Port_Cs;
    hw->port.rst           = NULL;              /* 板子无硬件复位脚，驱动改用软件复位 */
    hw->port.dc            = Port_Dc;
    hw->port.bl            = Port_Bl;
    hw->port.delay_ms      = Port_DelayMs;
    hw->port.write         = Port_Write;
    hw->port.max_chunk_len = ST7789_PORT_MAX_CHUNK;
    hw->port.ctx           = hw;

    ST7789_GpioInit(hw);

    return ST7789_Init(dev, &hw->port);
}

LCD_Result ST7789_PortInit(void)
{
    static const ST7789_PortConfig default_cfg = {
        .bus          = NULL,                       /* 自动使用 BSP_SPI3_GetBus() */
        .spi_clock_hz = ST7789_DEFAULT_SPI_CLOCK_HZ,
        .spi_mode     = ST7789_DEFAULT_SPI_MODE,
        .use_dma      = ST7789_DEFAULT_SPI_USE_DMA,
        .cs_port      = ST7789_DEFAULT_CS_PORT,
        .cs_pin       = ST7789_DEFAULT_CS_PIN,
        .dc_port      = ST7789_DEFAULT_DC_PORT,
        .dc_pin       = ST7789_DEFAULT_DC_PIN,
        .bl_port      = ST7789_DEFAULT_BL_PORT,
        .bl_pin       = ST7789_DEFAULT_BL_PIN,
    };
    return ST7789_PortInitWithConfig(&g_lcd, &default_cfg);
}
