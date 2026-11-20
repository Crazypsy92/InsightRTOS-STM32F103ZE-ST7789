/**
 * @file    st7789_port.h
 * @brief   ST7789 移植层接口（板级引脚 / SPI 总线绑定）
 * @details 默认接线（按参考例程）：
 *          - SPI3：SCK=PB3，MOSI=PB5（18MHz，模式 0）
 *          - CS = PD11，DC = PD12，BL = PD13，无硬件复位脚（软件复位）
 *
 *          可在调用 ST7789_PortInitWithConfig() 时注入其它总线或引脚；
 *          ST7789_PortInit() 使用上述默认接线初始化全局 g_lcd。
 *
 * @note    使用前必须先调用 Timebase_Init()（驱动内部依赖 Delay_ms）。
 */
#ifndef __ST7789_PORT_H__
#define __ST7789_PORT_H__

#include "st7789.h"
#include "spi_bus.h"
#include "main.h"

/** @brief ST7789 板级端口配置（字段为 0 / NULL 时使用默认值） */
typedef struct {
    spi_bus_t      *bus;          /**< SPI 抽象总线，NULL 时自动使用 SPI3 */
    uint32_t        spi_clock_hz; /**< SPI 时钟（Hz），0 时使用 18MHz */
    spi_bus_mode_t  spi_mode;     /**< SPI 模式，默认 SPI_BUS_MODE_0 */
    uint8_t         use_dma;      /**< 1: 大块传输走 DMA；0: 全部轮询 */
    GPIO_TypeDef   *cs_port;      /**< 片选端口，NULL 时使用 GPIOD */
    uint32_t        cs_pin;       /**< 片选引脚，0 时使用 PIN_11 */
    GPIO_TypeDef   *dc_port;      /**< 数据/命令端口，NULL 时使用 GPIOD */
    uint32_t        dc_pin;       /**< 数据/命令引脚，0 时使用 PIN_12 */
    GPIO_TypeDef   *bl_port;      /**< 背光端口，NULL 时使用 GPIOD */
    uint32_t        bl_pin;       /**< 背光引脚，0 时使用 PIN_13 */
} ST7789_PortConfig;

/** @brief 全局默认 LCD 设备句柄 */
extern LCD_Device g_lcd;

/**
 * @brief 使用默认板级接线初始化 ST7789（SPI3 + PD11/PD12/PD13）
 * @return LCD_OK 成功；其它为失败错误码
 */
LCD_Result ST7789_PortInit(void);

/**
 * @brief 使用外部配置初始化指定 LCD 设备
 * @param dev 设备句柄（不能为 NULL）
 * @param cfg 配置参数（可为临时变量，内部会复制保存）
 * @return LCD_OK 成功；其它为失败错误码
 */
LCD_Result ST7789_PortInitWithConfig(LCD_Device *dev, const ST7789_PortConfig *cfg);

#endif /* __ST7789_PORT_H__ */
