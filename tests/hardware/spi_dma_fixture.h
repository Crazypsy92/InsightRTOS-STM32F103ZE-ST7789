#ifndef INSIGHT_SPI_DMA_FIXTURE_H
#define INSIGHT_SPI_DMA_FIXTURE_H
#include "spi_stm32_ll.h"

typedef enum {
    SPI_DMA_TEST_ARMED, SPI_DMA_TEST_ACTIVE, SPI_DMA_TEST_WAITED, SPI_DMA_TEST_DRAINED
} spi_dma_test_stage_t;

void spi_dma_fixture_hook(const spi_stm32_ll_t *ll, spi_dma_test_stage_t stage,
                          uint32_t offset, uint32_t length, uint32_t timeout,
                          uint32_t start, spi_bus_status_t status);
_Noreturn void spi_dma_fixture_start(void);
#endif
