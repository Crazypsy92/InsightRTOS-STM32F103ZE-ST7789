/**
 * @file bsp_includes.h
 * @brief BSP 总头文件
 * @details 裸机工程统一头文件入口：系统基础头文件 + 全部 BSP 模块头文件。
 * @author 西伯利亚小菜狗
 * @version 3.0 (bare-metal, ST7789)
 */
#ifndef __BSP_INCLUDES_H__
#define __BSP_INCLUDES_H__

/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

/* BSP 模块 */
#include "delay.h"          /* 统一时间基准 Timebase */
#include "multi_button.h"   /* MultiButton 状态机（裸机） */
#include "key_port.h"       /* 按键硬件适配层 */
#include "spi_bus.h"        /* SPI 总线抽象接口（MCU 无关） */
#include "bsp_spi.h"        /* 板级 SPI 总线（SPI1/SPI2/SPI3 共享） */
#include "st7789.h"         /* ST7789 TFT 屏幕驱动（协议层） */
#include "st7789_port.h"    /* ST7789 移植层（SPI3 + PD11/PD12/PD13） */
#include "app_lcd_test.h"   /* 屏幕测试程序 */

#endif /* __BSP_INCLUDES_H__ */
