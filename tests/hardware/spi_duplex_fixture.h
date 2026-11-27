#ifndef INSIGHT_SPI_DUPLEX_FIXTURE_H
#define INSIGHT_SPI_DUPLEX_FIXTURE_H
#include "spi_stm32_ll.h"
typedef enum {
    SPI_DUPLEX_ARMED, SPI_DUPLEX_RX_FIRST, SPI_DUPLEX_ACTIVE, SPI_DUPLEX_WAITED, SPI_DUPLEX_DRAINED
} spi_duplex_stage_t;
void spi_duplex_fixture_hook(const spi_stm32_ll_t *ll, spi_duplex_stage_t stage,
                             uint32_t offset, uint32_t length, uint32_t timeout,
                             uint32_t start, spi_bus_status_t status);
_Noreturn void spi_duplex_fixture_start(void);
#endif
