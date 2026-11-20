/**
 * @file    st7789.h
 * @brief   ST7789 240x240 SPI TFT 驱动头文件（纯协议层，MCU 无关）
 * @details 驱动只依赖下方 LCD_Port 回调表，不包含任何 MCU / LL / HAL 头文件。
 *          引脚、SPI 外设、时钟与 DMA 全部由 st7789_port.c 负责，
 *          换平台时本文件与 st7789.c 无需改动。
 */
#ifndef __ST7789_H_
#define __ST7789_H_

#include <stdint.h>
#include <stddef.h>
#include "lcd_fonts.h"

/* ==================== 屏幕参数 ==================== */

#define LCD_W 240
#define LCD_H 240

/* ==================== 统一状态码 ==================== */

typedef enum {
    LCD_OK            =  0,   /* 操作成功 */
    LCD_ERROR_PARAM   = -1,   /* 参数非法（NULL 指针/越界/非法字号/溢出） */
    LCD_ERROR_BUS     = -2,   /* 总线错误（底层 SPI 错误/CS 失败/未持有总线） */
    LCD_ERROR_TIMEOUT = -3    /* 通信超时 */
} LCD_Result;

/* ==================== 抽象硬件接口（MCU 无关） ==================== */

/**
 * @brief LCD 底层操作回调表
 * @note  由 port 层实现并填充；驱动只通过回调访问引脚、延时与 SPI，不感知 MCU。
 *        level: 1=高电平/释放，0=低电平/有效。
 */
typedef struct {
    LCD_Result (*cs)(void *ctx, uint8_t level);                       /* 片选：0=占用总线，1=释放总线 */
    void (*rst)(void *ctx, uint8_t level);                            /* 复位，可为 NULL（无硬件复位脚） */
    void (*dc)(void *ctx, uint8_t level);                             /* 数据/命令选择：0=CMD，1=DATA */
    void (*bl)(void *ctx, uint8_t level);                             /* 背光，可为 NULL（不控制） */
    void (*delay_ms)(void *ctx, uint32_t ms);                         /* 毫秒延时 */
    LCD_Result (*write)(void *ctx, const uint8_t *buf, uint16_t len); /* SPI 连续发送 */
    uint16_t max_chunk_len;                                           /* 单次传输上限（0 或 65535 表示 65535） */
    void *ctx;                                                        /* 传给回调的上下文 */
} LCD_Port;

/** @brief LCD 设备对象：持有抽象端口、屏幕信息与最后错误码 */
typedef struct {
    const LCD_Port *port;       /* 绑定的底层端口 */
    uint16_t width;             /* 当前方向下的逻辑宽度 */
    uint16_t height;            /* 当前方向下的逻辑高度 */
    uint16_t x_offset;          /* 显存列偏移（ST7789 240x320 显存裁剪） */
    uint16_t y_offset;          /* 显存行偏移 */
    uint16_t rotation;          /* 0 / 90 / 180 / 270 */
    LCD_Result last_error;      /* 最后一次错误记录 */
} LCD_Device;

/* ==================== 控制宏（基于抽象端口） ==================== */

#define LCD_RST_SET(dev)       do { if ((dev) && (dev)->port && (dev)->port->rst) (dev)->port->rst((dev)->port->ctx, 1); } while (0)
#define LCD_RST_CLR(dev)       do { if ((dev) && (dev)->port && (dev)->port->rst) (dev)->port->rst((dev)->port->ctx, 0); } while (0)
#define LCD_DC_SET(dev)        do { if ((dev) && (dev)->port && (dev)->port->dc)  (dev)->port->dc((dev)->port->ctx, 1); } while (0)
#define LCD_DC_CLR(dev)        do { if ((dev) && (dev)->port && (dev)->port->dc)  (dev)->port->dc((dev)->port->ctx, 0); } while (0)
#define LCD_DELAY_MS(dev, ms)  do { if ((dev) && (dev)->port && (dev)->port->delay_ms) (dev)->port->delay_ms((dev)->port->ctx, (ms)); } while (0)
#define LCD_BL_ON(dev)         do { if ((dev) && (dev)->port && (dev)->port->bl != NULL) (dev)->port->bl((dev)->port->ctx, 1); } while (0)
#define LCD_BL_OFF(dev)        do { if ((dev) && (dev)->port && (dev)->port->bl != NULL) (dev)->port->bl((dev)->port->ctx, 0); } while (0)

/* ==================== 颜色定义 (RGB565) ==================== */

#define WHITE         0xFFFF
#define BLACK         0x0000
#define BLUE          0x001F
#define MAGENTA       0xF81F
#define RED           0xF800
#define GREEN         0x07E0
#define CYAN          0x7FFF
#define YELLOW        0xFFE0
#define GRAY          0x8430
#define LIGHTGRAY     0xC618
#define DARKBLUE      0x01CF
#define LIGHTBLUE     0x7D7C

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化 ST7789（软复位 + 面板初始化序列）
 * @param dev  设备句柄
 * @param port 已实现回调的端口（内部不负责其硬件初始化）
 * @return LCD_OK 成功；其他为失败错误码
 */
LCD_Result ST7789_Init(LCD_Device *dev, const LCD_Port *port);

/** @brief 设置显示方向（0/90/180/270），内部会重新映射显存偏移 */
LCD_Result LCD_SetRotation(LCD_Device *dev, uint16_t rotation);

/** @brief 查询最后一次错误码 */
LCD_Result LCD_GetLastError(const LCD_Device *dev);

/** @brief 清除错误状态 */
void LCD_ClearError(LCD_Device *dev);

/* 基础寄存器与数据写操作 */
LCD_Result LCD_WR_REG(LCD_Device *dev, uint8_t cmd);
LCD_Result LCD_WR_DATA8(LCD_Device *dev, uint8_t dat);
LCD_Result LCD_WR_DATA(LCD_Device *dev, uint16_t dat);
LCD_Result LCD_Address_Set(LCD_Device *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/* 图形函数 */
LCD_Result LCD_Clear(LCD_Device *dev, uint16_t color);
LCD_Result LCD_Fill(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
LCD_Result LCD_DrawPoint(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t color);
LCD_Result LCD_DrawLine(LCD_Device *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
LCD_Result LCD_DrawLineH(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t color);
LCD_Result LCD_DrawLineV(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t height, uint16_t color);
LCD_Result LCD_DrawRect(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
LCD_Result LCD_DrawCircle(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t r, uint16_t color);
LCD_Result LCD_FillCircle(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t r, uint16_t color);

/* 字符与数字显示（ASCII 字库 12/16/20/24/32 号） */
LCD_Result LCD_ShowChar(LCD_Device *dev, uint16_t x, uint16_t y, uint8_t ch,
                        const pFONT *font, uint16_t fc, uint16_t bc);
LCD_Result LCD_ShowString(LCD_Device *dev, uint16_t x, uint16_t y, const char *str,
                          const pFONT *font, uint16_t fc, uint16_t bc);
LCD_Result LCD_ShowIntNum(LCD_Device *dev, uint16_t x, uint16_t y, int32_t num, uint8_t len,
                          const pFONT *font, uint16_t fc, uint16_t bc);
LCD_Result LCD_ShowFloatNum(LCD_Device *dev, uint16_t x, uint16_t y, float num, uint8_t len,
                            uint8_t decs, const pFONT *font, uint16_t fc, uint16_t bc);

/* 图片显示：pic 为 RGB565 大端字节流，长度 width*height*2 */
LCD_Result LCD_ShowPicture(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                           const uint8_t *pic);

#endif /* __ST7789_H_ */
