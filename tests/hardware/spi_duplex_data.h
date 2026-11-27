#ifndef INSIGHT_SPI_DUPLEX_DATA_H
#define INSIGHT_SPI_DUPLEX_DATA_H

#include <stdint.h>

#define DUPLEX_CAPACITY 2048U
#define DUPLEX_GUARD UINT32_C(0x59ad12c3)
#define DUPLEX_NO_MISMATCH UINT32_MAX

typedef struct {
    uint32_t before;
    _Alignas(4) uint8_t bytes[DUPLEX_CAPACITY];
    uint32_t after;
} duplex_buffer_t;

typedef struct {
    uint32_t mismatch_count, first_mismatch, tx_crc, rx_crc, bounds_ok;
} duplex_data_result_t;

static inline uint8_t duplex_sentinel(uint8_t tx)
{
    return tx == 0xa7U ? 0xa8U : 0xa7U;
}

static inline void duplex_prepare(duplex_buffer_t *tx, duplex_buffer_t *rx,
                                  uint32_t length, uint32_t pattern)
{
    tx->before = tx->after = rx->before = rx->after = DUPLEX_GUARD;
    uint32_t random = UINT32_C(0x98c0ffee);
    for (uint32_t i = 0; i < DUPLEX_CAPACITY; ++i) {
        uint8_t value = 0xc9U;
        if (i < length) {
            if (pattern == 0U) value = (uint8_t)i;
            else if (pattern == 1U) value = (i & 1U) == 0U ? 0x55U : 0xaaU;
            else {
                random ^= random << 13U;
                random ^= random >> 17U;
                random ^= random << 5U;
                value = (uint8_t)random;
            }
        }
        tx->bytes[i] = value;
        rx->bytes[i] = duplex_sentinel(value);
    }
}

static inline uint32_t duplex_crc_step(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint32_t bit = 0; bit < 8U; ++bit)
        crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? UINT32_C(0xedb88320) : 0U);
    return crc;
}

static inline duplex_data_result_t duplex_check(const duplex_buffer_t *tx, const duplex_buffer_t *rx,
                                                uint32_t length)
{
    duplex_data_result_t out = {
        0U, DUPLEX_NO_MISMATCH, UINT32_MAX, UINT32_MAX,
        tx->before == DUPLEX_GUARD && tx->after == DUPLEX_GUARD &&
        rx->before == DUPLEX_GUARD && rx->after == DUPLEX_GUARD
    };
    for (uint32_t i = 0U; i < length; ++i) {
        if (tx->bytes[i] != rx->bytes[i]) {
            if (out.mismatch_count == 0U) out.first_mismatch = i;
            ++out.mismatch_count;
        }
        out.tx_crc = duplex_crc_step(out.tx_crc, tx->bytes[i]);
        out.rx_crc = duplex_crc_step(out.rx_crc, rx->bytes[i]);
    }
    for (uint32_t i = length; i < DUPLEX_CAPACITY; ++i) {
        if (tx->bytes[i] != 0xc9U || rx->bytes[i] != duplex_sentinel(0xc9U)) out.bounds_ok = 0U;
    }
    out.tx_crc ^= UINT32_MAX;
    out.rx_crc ^= UINT32_MAX;
    return out;
}
#endif
