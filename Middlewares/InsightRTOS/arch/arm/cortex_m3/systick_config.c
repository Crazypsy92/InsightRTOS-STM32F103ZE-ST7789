#include "port.h"

bool rtos_port_systick_reload(uint32_t cpu_hz, uint32_t tick_hz, uint32_t *reload)
{
    if (reload == NULL || cpu_hz == 0U || tick_hz == 0U ||
        cpu_hz % tick_hz != 0U) {
        return false;
    }
    const uint32_t cycles = cpu_hz / tick_hz;
    /* LOAD=0 never transitions 1->0 and cannot generate periodic exceptions. */
    if (cycles < 2U || cycles > UINT32_C(0x01000000)) {
        return false;
    }
    *reload = cycles - 1U;
    return true;
}
