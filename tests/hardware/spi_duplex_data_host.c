#include "spi_duplex_data.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static duplex_buffer_t tx, rx;
    const uint32_t lengths[] = {15U, 16U, 257U, 2048U};
    for (size_t i = 0U; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        const uint32_t n = lengths[i];
        for (uint32_t pattern = 0U; pattern < 3U; ++pattern) {
            duplex_prepare(&tx, &rx, n, pattern);
            duplex_data_result_t result = duplex_check(&tx, &rx, n);
            assert(result.bounds_ok && result.mismatch_count == n && result.first_mismatch == 0U);
            memcpy(rx.bytes, tx.bytes, n);
            result = duplex_check(&tx, &rx, n);
            assert(result.bounds_ok && result.mismatch_count == 0U &&
                   result.first_mismatch == DUPLEX_NO_MISMATCH && result.tx_crc == result.rx_crc);
            printf("%u,%u,%08x\n", n, pattern, result.tx_crc);
            rx.bytes[n - 1U] ^= 1U;
            assert(duplex_check(&tx, &rx, n).first_mismatch == n - 1U);
            if (n < DUPLEX_CAPACITY) {
                rx.bytes[n] ^= 1U;
                assert(!duplex_check(&tx, &rx, n).bounds_ok);
            }
            rx.after ^= 1U;
            assert(!duplex_check(&tx, &rx, n).bounds_ok);
        }
    }
    return 0;
}
