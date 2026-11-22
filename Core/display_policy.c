#include "display_app.h"

bool Display_UpdateDue(uint32_t now, uint32_t page, uint32_t automatic,
                       uint32_t page_started, uint32_t frame_started)
{
    (void)page;
    (void)automatic;
    (void)page_started;
    return (uint32_t)(now - frame_started) >= 250U;
}
