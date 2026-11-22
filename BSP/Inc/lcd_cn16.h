#ifndef INSIGHT_LCD_CN16_H
#define INSIGHT_LCD_CN16_H

#include <stdint.h>
#include "st7789.h"

/* Tiny fixed Chinese font: 16x16 pixels, 1 bpp, 32 bytes/glyph.
   Built for UI-only subset. No dynamic memory or Unicode font files required.
   Call only from the LCD-owning task (same contract as LCD_ShowPicture). */
LCD_Result LCD_CN16_DrawGlyph(LCD_Device *dev, uint16_t x, uint16_t y,
                             uint32_t codepoint, uint16_t foreground, uint16_t background);
/* Draws mixed UTF-8 Chinese and 8x16 ASCII at same baseline.
   Invalid UTF-8 is drawn as '?' ASCII. Unsupported Chinese glyph is '??' ASCII.
   Output must remain within display bounds; returns first LCD error. */
LCD_Result LCD_CN16_ShowText(LCD_Device *dev, uint16_t x, uint16_t y,
                            const char *utf8, uint16_t foreground, uint16_t background);
uint32_t LCD_CN16_GlyphCount(void);

#endif
