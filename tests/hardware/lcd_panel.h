#ifndef INSIGHT_LCD_PANEL_H
#define INSIGHT_LCD_PANEL_H
#include <stdbool.h>
#include "spi_stm32_ll.h"

typedef enum { PANEL_DMA_ARMED, PANEL_DMA_WAITED, PANEL_DMA_DRAINED } panel_dma_stage_t;
void LCDPanel_PreInit(void);
bool LCDPanel_Step(bool *entering);
void LCDPanel_Publish(void);
void LCDPanel_Poll(const spi_stm32_ll_t *ll, uint32_t length, bool tx_only);
void LCDPanel_DMA(const spi_stm32_ll_t *ll, panel_dma_stage_t stage, uint32_t length,
                  spi_bus_status_t status, bool tx_only);
void LCDPanel_Select(const spi_bus_t *bus, const void *owner);
void LCDPanel_WriteReturned(const spi_bus_t *bus, const void *owner, int status);
void LCDPanel_Release(const spi_bus_t *bus);
#endif
