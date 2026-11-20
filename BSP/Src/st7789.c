/**
 * @file    st7789.c
 * @brief   ST7789 240x240 SPI TFT 驱动实现（纯协议层，MCU 无关）
 * @details 移植自“彩色液晶屏驱动-基于1.54寸240x240分辨率”例程的初始化序列与
 *          绘图/字库接口，并按 GC9A01 工程的架构拆分为协议层 + port 层：
 *          本文件只通过 LCD_Port 回调访问硬件，不包含任何 MCU 头文件。
 *
 *          像素数据统一按 RGB565 大端字节序，经 8 位 SPI 分块发送；
 *          大块传输由 port 层决定是否走 DMA。
 */
#include "st7789.h"
#include <string.h>

/* 行缓冲区：240 像素 * 2 字节 = 480 字节，静态分配避免栈溢出 */
static uint8_t s_line_buf[LCD_W * 2];

/* ================================================================================== */
/* 内部辅助                                                                           */
/* ================================================================================== */

static inline LCD_Result lcd_set_error(LCD_Device *dev, LCD_Result err)
{
    if (dev != NULL && err != LCD_OK) {
        if (dev->last_error == LCD_OK) {
            dev->last_error = err; /* 保留首个故障原因 */
        }
    }
    return err;
}

static inline LCD_Result lcd_cs_low(LCD_Device *dev)
{
    if (dev == NULL || dev->port == NULL || dev->port->cs == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    LCD_Result res = dev->port->cs(dev->port->ctx, 0U);
    return (res == LCD_OK) ? LCD_OK : lcd_set_error(dev, res);
}

static inline LCD_Result lcd_cs_high(LCD_Device *dev)
{
    if (dev == NULL || dev->port == NULL || dev->port->cs == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    LCD_Result res = dev->port->cs(dev->port->ctx, 1U);
    return (res == LCD_OK) ? LCD_OK : lcd_set_error(dev, res);
}

/** @brief 连续发送多字节（按 Port 限制自动分块，分块期间 CS/DC 保持不变） */
static LCD_Result lcd_write_bytes(LCD_Device *dev, const uint8_t *buf, uint32_t len)
{
    if (dev == NULL || dev->port == NULL || dev->port->write == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (len == 0U) {
        return LCD_OK;
    }
    if (buf == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    uint16_t chunk_limit = (dev->port->max_chunk_len > 0U) ? dev->port->max_chunk_len : 65535U;

    while (len > 0U) {
        uint16_t chunk = (len > chunk_limit) ? chunk_limit : (uint16_t)len;
        LCD_Result res = dev->port->write(dev->port->ctx, buf, chunk);
        if (res != LCD_OK) {
            return lcd_set_error(dev, res);
        }
        buf += chunk;
        len -= chunk;
    }
    return LCD_OK;
}

static inline void lcd_put_color(uint8_t *dst, uint16_t color)
{
    dst[0] = (uint8_t)(color >> 8);
    dst[1] = (uint8_t)(color & 0xFFU);
}

/** @brief 绘制一个像素；越界时静默忽略（供直线/圆等裁剪型图元使用） */
static void lcd_plot_clip(LCD_Device *dev, int32_t x, int32_t y, uint16_t color)
{
    if (dev == NULL) {
        return;
    }
    if (x < 0 || y < 0 || x >= (int32_t)dev->width || y >= (int32_t)dev->height) {
        return;
    }
    (void)LCD_DrawPoint(dev, (uint16_t)x, (uint16_t)y, color);
}

/* ================================================================================== */
/* 初始化与方向                                                                       */
/* ================================================================================== */

#define LCD_INIT_REG(cmd) do { \
    LCD_Result _r = LCD_WR_REG(dev, (cmd)); \
    if (_r != LCD_OK) return _r; \
} while (0)

#define LCD_INIT_DATA8(val) do { \
    LCD_Result _r = LCD_WR_DATA8(dev, (val)); \
    if (_r != LCD_OK) return _r; \
} while (0)

LCD_Result LCD_SetRotation(LCD_Device *dev, uint16_t rotation)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (rotation != 0U && rotation != 90U && rotation != 180U && rotation != 270U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    uint8_t madctl;
    uint16_t x_off = 0U;
    uint16_t y_off = 0U;

    switch (rotation) {
        case 90U:
            madctl = 0x70U;          /* 横屏 */
            break;
        case 180U:
            madctl = 0xC0U;          /* 竖屏翻转 */
            y_off = 80U;
            break;
        case 270U:
            madctl = 0xA0U;          /* 横屏翻转 */
            x_off = 80U;
            break;
        case 0U:
        default:
            madctl = 0x00U;          /* 竖屏 */
            break;
    }

    LCD_Result res = LCD_WR_REG(dev, 0x36U);   /* MADCTL */
    if (res != LCD_OK) return res;
    res = LCD_WR_DATA8(dev, madctl);
    if (res != LCD_OK) return res;

    dev->rotation = rotation;
    dev->width    = LCD_W;
    dev->height   = LCD_H;
    dev->x_offset = x_off;
    dev->y_offset = y_off;
    return LCD_OK;
}

LCD_Result ST7789_Init(LCD_Device *dev, const LCD_Port *port)
{
    if (dev == NULL || port == NULL || port->cs == NULL || port->dc == NULL ||
        port->delay_ms == NULL || port->write == NULL) {
        return (dev != NULL) ? lcd_set_error(dev, LCD_ERROR_PARAM) : LCD_ERROR_PARAM;
    }

    dev->port       = port;
    dev->width      = LCD_W;
    dev->height     = LCD_H;
    dev->x_offset   = 0U;
    dev->y_offset   = 0U;
    dev->rotation   = 0U;
    dev->last_error = LCD_OK;

    LCD_BL_OFF(dev);

    /* 1. 复位：有硬件复位脚则做复位脉冲，否则用软件复位命令 */
    if (port->rst != NULL) {
        LCD_RST_CLR(dev);
        LCD_DELAY_MS(dev, 20);
        LCD_RST_SET(dev);
        LCD_DELAY_MS(dev, 120);
    } else {
        LCD_INIT_REG(0x01U);        /* SWRESET */
        LCD_DELAY_MS(dev, 150);
    }

    /* 2. 退出睡眠 */
    LCD_INIT_REG(0x11U);            /* SLPOUT */
    LCD_DELAY_MS(dev, 120);

    /* 3. 颜色格式与显存方向 */
    LCD_INIT_REG(0x3AU);            /* COLMOD */
    LCD_INIT_DATA8(0x05U);          /* 16 位 RGB565 */

    LCD_INIT_REG(0x36U);            /* MADCTL，方向稍后由 LCD_SetRotation 统一设置 */
    LCD_INIT_DATA8(0x00U);

    /* 4. 厂商推荐电压/伽马参数（与参考例程一致） */
    LCD_INIT_REG(0xB2U);            /* PORCTRL */
    LCD_INIT_DATA8(0x0CU);
    LCD_INIT_DATA8(0x0CU);
    LCD_INIT_DATA8(0x00U);
    LCD_INIT_DATA8(0x33U);
    LCD_INIT_DATA8(0x33U);

    LCD_INIT_REG(0xB7U);            /* GCTRL */
    LCD_INIT_DATA8(0x35U);

    LCD_INIT_REG(0xBBU);            /* VCOMS */
    LCD_INIT_DATA8(0x19U);

    LCD_INIT_REG(0xC0U);            /* LCMCTRL */
    LCD_INIT_DATA8(0x2CU);

    LCD_INIT_REG(0xC2U);            /* VDVVRHEN */
    LCD_INIT_DATA8(0x01U);

    LCD_INIT_REG(0xC3U);            /* VRHS */
    LCD_INIT_DATA8(0x12U);

    LCD_INIT_REG(0xC4U);            /* VDVS */
    LCD_INIT_DATA8(0x20U);

    LCD_INIT_REG(0xC6U);            /* FRCTRL2 */
    LCD_INIT_DATA8(0x0FU);          /* 60Hz */

    LCD_INIT_REG(0xD0U);            /* PWCTRL1 */
    LCD_INIT_DATA8(0xA4U);
    LCD_INIT_DATA8(0xA1U);

    LCD_INIT_REG(0xE0U);            /* PVGAMCTRL */
    LCD_INIT_DATA8(0xD0U);
    LCD_INIT_DATA8(0x04U);
    LCD_INIT_DATA8(0x0DU);
    LCD_INIT_DATA8(0x11U);
    LCD_INIT_DATA8(0x13U);
    LCD_INIT_DATA8(0x2BU);
    LCD_INIT_DATA8(0x3FU);
    LCD_INIT_DATA8(0x54U);
    LCD_INIT_DATA8(0x4CU);
    LCD_INIT_DATA8(0x18U);
    LCD_INIT_DATA8(0x0DU);
    LCD_INIT_DATA8(0x0BU);
    LCD_INIT_DATA8(0x1FU);
    LCD_INIT_DATA8(0x23U);

    LCD_INIT_REG(0xE1U);            /* NVGAMCTRL */
    LCD_INIT_DATA8(0xD0U);
    LCD_INIT_DATA8(0x04U);
    LCD_INIT_DATA8(0x0CU);
    LCD_INIT_DATA8(0x11U);
    LCD_INIT_DATA8(0x13U);
    LCD_INIT_DATA8(0x2CU);
    LCD_INIT_DATA8(0x3FU);
    LCD_INIT_DATA8(0x44U);
    LCD_INIT_DATA8(0x51U);
    LCD_INIT_DATA8(0x2FU);
    LCD_INIT_DATA8(0x1FU);
    LCD_INIT_DATA8(0x1FU);
    LCD_INIT_DATA8(0x20U);
    LCD_INIT_DATA8(0x23U);

    LCD_INIT_REG(0x21U);            /* INVON：参考例程使用反显 */
    LCD_INIT_REG(0x13U);            /* NORON：普通显示模式 */
    LCD_INIT_REG(0x29U);            /* DISPON */
    LCD_DELAY_MS(dev, 20);

    /* 5. 默认竖屏 180°（当前面板装配方向为倒置）并清屏 */
    LCD_Result res = LCD_SetRotation(dev, 180U);
    if (res != LCD_OK) {
        return res;
    }
    res = LCD_Clear(dev, BLACK);
    if (res != LCD_OK) {
        return res;
    }

    LCD_BL_ON(dev);
    return LCD_OK;
}

LCD_Result LCD_GetLastError(const LCD_Device *dev)
{
    return (dev == NULL) ? LCD_ERROR_PARAM : dev->last_error;
}

void LCD_ClearError(LCD_Device *dev)
{
    if (dev != NULL) {
        dev->last_error = LCD_OK;
    }
}

/* ================================================================================== */
/* 基础写操作                                                                         */
/* ================================================================================== */

LCD_Result LCD_WR_REG(LCD_Device *dev, uint8_t cmd)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    LCD_Result res = lcd_cs_low(dev);
    if (res != LCD_OK) {
        return res;
    }

    LCD_DC_CLR(dev);
    res = lcd_write_bytes(dev, &cmd, 1U);
    LCD_DC_SET(dev);

    LCD_Result rel = lcd_cs_high(dev);
    if (res != LCD_OK) {
        return res;
    }
    return rel;
}

LCD_Result LCD_WR_DATA8(LCD_Device *dev, uint8_t dat)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    LCD_Result res = lcd_cs_low(dev);
    if (res != LCD_OK) {
        return res;
    }

    LCD_DC_SET(dev);
    res = lcd_write_bytes(dev, &dat, 1U);

    LCD_Result rel = lcd_cs_high(dev);
    if (res != LCD_OK) {
        return res;
    }
    return rel;
}

LCD_Result LCD_WR_DATA(LCD_Device *dev, uint16_t dat)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    LCD_Result res = lcd_cs_low(dev);
    if (res != LCD_OK) {
        return res;
    }

    LCD_DC_SET(dev);
    uint8_t buf[2];
    lcd_put_color(buf, dat);
    res = lcd_write_bytes(dev, buf, 2U);

    LCD_Result rel = lcd_cs_high(dev);
    if (res != LCD_OK) {
        return res;
    }
    return rel;
}

LCD_Result LCD_Address_Set(LCD_Device *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (x1 > x2 || y1 > y2 || x2 >= dev->width || y2 >= dev->height) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    uint16_t xs = (uint16_t)(x1 + dev->x_offset);
    uint16_t xe = (uint16_t)(x2 + dev->x_offset);
    uint16_t ys = (uint16_t)(y1 + dev->y_offset);
    uint16_t ye = (uint16_t)(y2 + dev->y_offset);

    LCD_Result res = LCD_WR_REG(dev, 0x2AU);   /* 列地址 */
    if (res != LCD_OK) return res;
    res = LCD_WR_DATA(dev, xs);
    if (res != LCD_OK) return res;
    res = LCD_WR_DATA(dev, xe);
    if (res != LCD_OK) return res;

    res = LCD_WR_REG(dev, 0x2BU);              /* 行地址 */
    if (res != LCD_OK) return res;
    res = LCD_WR_DATA(dev, ys);
    if (res != LCD_OK) return res;
    res = LCD_WR_DATA(dev, ye);
    if (res != LCD_OK) return res;

    return LCD_WR_REG(dev, 0x2CU);             /* 开始写显存 */
}

/* ================================================================================== */
/* 图形绘制                                                                           */
/* ================================================================================== */

LCD_Result LCD_Clear(LCD_Device *dev, uint16_t color)
{
    if (dev == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    return LCD_Fill(dev, 0U, 0U, dev->width, dev->height, color);
}

LCD_Result LCD_Fill(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (width == 0U || height == 0U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (((uint32_t)x + width) > dev->width || ((uint32_t)y + height) > dev->height) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    LCD_Result res = LCD_Address_Set(dev, x, y, (uint16_t)(x + width - 1U), (uint16_t)(y + height - 1U));
    if (res != LCD_OK) {
        return res;
    }

    /* 预填充一行像素 */
    for (uint32_t i = 0; i < (uint32_t)width * 2U; i += 2U) {
        lcd_put_color(&s_line_buf[i], color);
    }

    res = lcd_cs_low(dev);
    if (res != LCD_OK) {
        return res;
    }
    LCD_DC_SET(dev);

    for (uint16_t row = 0; row < height; row++) {
        res = lcd_write_bytes(dev, s_line_buf, (uint32_t)width * 2U);
        if (res != LCD_OK) {
            break;
        }
    }

    LCD_Result rel = lcd_cs_high(dev);
    if (res != LCD_OK) {
        return res;
    }
    return rel;
}

LCD_Result LCD_DrawPoint(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t color)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (x >= dev->width || y >= dev->height) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    LCD_Result res = LCD_Address_Set(dev, x, y, x, y);
    if (res != LCD_OK) {
        return res;
    }
    return LCD_WR_DATA(dev, color);
}

LCD_Result LCD_DrawLine(LCD_Device *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    if (dev == NULL || dev->port == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    int32_t delta_x = (int32_t)x2 - (int32_t)x1;
    int32_t delta_y = (int32_t)y2 - (int32_t)y1;
    int32_t x = (int32_t)x1;
    int32_t y = (int32_t)y1;
    int32_t incx = (delta_x > 0) ? 1 : ((delta_x == 0) ? 0 : -1);
    int32_t incy = (delta_y > 0) ? 1 : ((delta_y == 0) ? 0 : -1);
    int32_t xerr = 0;
    int32_t yerr = 0;
    int32_t distance;

    if (delta_x < 0) delta_x = -delta_x;
    if (delta_y < 0) delta_y = -delta_y;
    distance = (delta_x > delta_y) ? delta_x : delta_y;

    for (int32_t t = 0; t <= distance; t++) {
        lcd_plot_clip(dev, x, y, color);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance) {
            xerr -= distance;
            x += incx;
        }
        if (yerr > distance) {
            yerr -= distance;
            y += incy;
        }
    }
    return LCD_OK;
}

LCD_Result LCD_DrawLineH(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t color)
{
    return LCD_Fill(dev, x, y, width, 1U, color);
}

LCD_Result LCD_DrawLineV(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t height, uint16_t color)
{
    return LCD_Fill(dev, x, y, 1U, height, color);
}

LCD_Result LCD_DrawRect(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    if (dev == NULL || width == 0U || height == 0U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    LCD_Result res;
    res = LCD_DrawLineH(dev, x, y, width, color);
    if (res != LCD_OK) return res;
    res = LCD_DrawLineH(dev, x, (uint16_t)(y + height - 1U), width, color);
    if (res != LCD_OK) return res;
    res = LCD_DrawLineV(dev, x, y, height, color);
    if (res != LCD_OK) return res;
    res = LCD_DrawLineV(dev, (uint16_t)(x + width - 1U), y, height, color);
    return res;
}

LCD_Result LCD_DrawCircle(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    if (dev == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (r == 0U) {
        return LCD_DrawPoint(dev, x, y, color);
    }

    int32_t cx = (int32_t)x;
    int32_t cy = (int32_t)y;
    int32_t a = 0;
    int32_t b = (int32_t)r;
    int32_t d = 3 - ((int32_t)r << 1);

    while (a <= b) {
        lcd_plot_clip(dev, cx + b, cy + a, color);
        lcd_plot_clip(dev, cx + a, cy + b, color);
        lcd_plot_clip(dev, cx - a, cy + b, color);
        lcd_plot_clip(dev, cx - b, cy + a, color);
        lcd_plot_clip(dev, cx - b, cy - a, color);
        lcd_plot_clip(dev, cx - a, cy - b, color);
        lcd_plot_clip(dev, cx + a, cy - b, color);
        lcd_plot_clip(dev, cx + b, cy - a, color);

        if (d < 0) {
            d += (a << 2) + 6;
        } else {
            d += ((a - b) << 2) + 10;
            b--;
        }
        a++;
    }
    return LCD_OK;
}

LCD_Result LCD_FillCircle(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    if (dev == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    int32_t cx = (int32_t)x;
    int32_t cy = (int32_t)y;
    int32_t rr = (int32_t)r;

    for (int32_t dy = -rr; dy <= rr; dy++) {
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= rr * rr) {
            dx++;
        }
        int32_t y0 = cy + dy;
        int32_t x0 = cx - dx;
        if (y0 < 0 || y0 >= (int32_t)dev->height) {
            continue;
        }
        if (x0 < 0) {
            x0 = 0;
        }
        int32_t x1 = cx + dx;
        if (x1 >= (int32_t)dev->width) {
            x1 = (int32_t)dev->width - 1;
        }
        if (x1 >= x0) {
            (void)LCD_Fill(dev, (uint16_t)x0, (uint16_t)y0,
                           (uint16_t)(x1 - x0 + 1), 1U, color);
        }
    }
    return LCD_OK;
}

/* ================================================================================== */
/* 字符与数字显示                                                                     */
/* ================================================================================== */

LCD_Result LCD_ShowChar(LCD_Device *dev, uint16_t x, uint16_t y, uint8_t ch,
                        const pFONT *font, uint16_t fc, uint16_t bc)
{
    if (dev == NULL || dev->port == NULL || font == NULL || font->pTable == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (ch < ' ' || ch > '~') {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (font->Width == 0U || font->Height == 0U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (((uint32_t)x + font->Width) > dev->width || ((uint32_t)y + font->Height) > dev->height) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    uint16_t bytes_per_row = (uint16_t)((font->Width + 7U) / 8U);
    if ((uint32_t)bytes_per_row * font->Height > font->Sizes) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    const uint8_t *glyph = font->pTable + (uint32_t)(ch - ' ') * font->Sizes;

    LCD_Result res = LCD_Address_Set(dev, x, y,
                                     (uint16_t)(x + font->Width - 1U),
                                     (uint16_t)(y + font->Height - 1U));
    if (res != LCD_OK) {
        return res;
    }

    res = lcd_cs_low(dev);
    if (res != LCD_OK) {
        return res;
    }
    LCD_DC_SET(dev);

    for (uint16_t row = 0; row < font->Height; row++) {
        for (uint16_t col = 0; col < font->Width; col++) {
            uint8_t data = glyph[(uint32_t)row * bytes_per_row + (col >> 3)];
            uint16_t color = ((data >> (col & 7U)) & 0x01U) ? fc : bc;
            lcd_put_color(&s_line_buf[(uint32_t)col * 2U], color);
        }
        res = lcd_write_bytes(dev, s_line_buf, (uint32_t)font->Width * 2U);
        if (res != LCD_OK) {
            break;
        }
    }

    LCD_Result rel = lcd_cs_high(dev);
    if (res != LCD_OK) {
        return res;
    }
    return rel;
}

LCD_Result LCD_ShowString(LCD_Device *dev, uint16_t x, uint16_t y, const char *str,
                          const pFONT *font, uint16_t fc, uint16_t bc)
{
    if (dev == NULL || str == NULL || font == NULL) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    while (*str != '\0') {
        if (((uint32_t)x + font->Width) > dev->width) {
            break; /* 超出屏幕宽度安全截断 */
        }
        LCD_Result res = LCD_ShowChar(dev, x, y, (uint8_t)*str, font, fc, bc);
        if (res != LCD_OK) {
            return res;
        }
        x = (uint16_t)(x + font->Width);
        str++;
    }
    return LCD_OK;
}

LCD_Result LCD_ShowIntNum(LCD_Device *dev, uint16_t x, uint16_t y, int32_t num, uint8_t len,
                          const pFONT *font, uint16_t fc, uint16_t bc)
{
    if (dev == NULL || font == NULL || len == 0U || len > 10U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    uint32_t value = (num < 0) ? (uint32_t)(-num) : (uint32_t)num;
    uint8_t enshow = 0U;
    LCD_Result res;

    if (num < 0) {
        res = LCD_ShowChar(dev, x, y, '-', font, fc, bc);
        if (res != LCD_OK) return res;
        x = (uint16_t)(x + font->Width);
    }

    for (uint8_t t = 0U; t < len; t++) {
        uint32_t divisor = 1U;
        for (uint8_t k = 0U; k < (uint8_t)(len - t - 1U); k++) {
            divisor *= 10U;
        }
        uint8_t digit = (uint8_t)((value / divisor) % 10U);

        if (enshow == 0U && t < (uint8_t)(len - 1U)) {
            if (digit == 0U) {
                res = LCD_ShowChar(dev, (uint16_t)(x + (uint16_t)t * font->Width), y,
                                   ' ', font, fc, bc);
                if (res != LCD_OK) return res;
                continue;
            }
            enshow = 1U;
        }

        res = LCD_ShowChar(dev, (uint16_t)(x + (uint16_t)t * font->Width), y,
                           (uint8_t)(digit + '0'), font, fc, bc);
        if (res != LCD_OK) return res;
    }
    return LCD_OK;
}

LCD_Result LCD_ShowFloatNum(LCD_Device *dev, uint16_t x, uint16_t y, float num, uint8_t len,
                            uint8_t decs, const pFONT *font, uint16_t fc, uint16_t bc)
{
    if (dev == NULL || font == NULL || decs == 0U || decs > 4U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    if (num < 0.0f) {
        LCD_Result res = LCD_ShowChar(dev, x, y, '-', font, fc, bc);
        if (res != LCD_OK) return res;
        x = (uint16_t)(x + font->Width);
        num = -num;
    }

    uint32_t scale = 1U;
    for (uint8_t i = 0U; i < decs; i++) {
        scale *= 10U;
    }

    uint32_t scaled = (uint32_t)(num * (float)scale + 0.5f);
    uint32_t int_part = scaled / scale;
    uint32_t frac = scaled % scale;

    LCD_Result res = LCD_ShowIntNum(dev, x, y, (int32_t)int_part, len, font, fc, bc);
    if (res != LCD_OK) return res;
    x = (uint16_t)(x + (uint16_t)len * font->Width);

    res = LCD_ShowChar(dev, x, y, '.', font, fc, bc);
    if (res != LCD_OK) return res;
    x = (uint16_t)(x + font->Width);

    /* 小数部分补零显示 */
    for (uint8_t i = 0U; i < decs; i++) {
        uint32_t divisor = 1U;
        for (uint8_t k = 0U; k < (uint8_t)(decs - i - 1U); k++) {
            divisor *= 10U;
        }
        uint8_t digit = (uint8_t)((frac / divisor) % 10U);
        res = LCD_ShowChar(dev, x, y, (uint8_t)(digit + '0'), font, fc, bc);
        if (res != LCD_OK) return res;
        x = (uint16_t)(x + font->Width);
    }
    return LCD_OK;
}

/* ================================================================================== */
/* 图片显示                                                                           */
/* ================================================================================== */

LCD_Result LCD_ShowPicture(LCD_Device *dev, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                           const uint8_t *pic)
{
    if (dev == NULL || dev->port == NULL || pic == NULL || width == 0U || height == 0U) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }
    if (((uint32_t)x + width) > dev->width || ((uint32_t)y + height) > dev->height) {
        return lcd_set_error(dev, LCD_ERROR_PARAM);
    }

    LCD_Result res = LCD_Address_Set(dev, x, y,
                                     (uint16_t)(x + width - 1U),
                                     (uint16_t)(y + height - 1U));
    if (res != LCD_OK) {
        return res;
    }

    res = lcd_cs_low(dev);
    if (res != LCD_OK) {
        return res;
    }
    LCD_DC_SET(dev);

    res = lcd_write_bytes(dev, pic, (uint32_t)width * height * 2U);

    LCD_Result rel = lcd_cs_high(dev);
    if (res != LCD_OK) {
        return res;
    }
    return rel;
}
