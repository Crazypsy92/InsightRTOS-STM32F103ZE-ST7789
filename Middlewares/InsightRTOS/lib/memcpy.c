#include "memory.h"
/* No alignment assumption or over-read; compiled -fno-builtin. */
void *memcpy(void *restrict destination, const void *restrict source, size_t bytes)
{
    unsigned char *const output = destination;
    const unsigned char *const input = source;
    for (size_t i = 0U; i < bytes; ++i) output[i] = input[i];
    return destination;
}
