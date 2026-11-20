#ifndef INSIGHT_FREESTANDING_MEMORY_H
#define INSIGHT_FREESTANDING_MEMORY_H
#include <stddef.h>
/* C11 memcpy ABI. Freestanding ARM builds supply lib/memcpy.c; hosted tests
 * may use the host C library. This is not a new RTOS public service. */
void *memcpy(void *restrict destination, const void *restrict source, size_t bytes);
#endif
