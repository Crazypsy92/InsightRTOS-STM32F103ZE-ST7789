#ifndef INSIGHT_DISPLAY_UI_H
#define INSIGHT_DISPLAY_UI_H
#include "display_app.h"
typedef struct {
    rtos_task_snapshot_t tasks[4];
    uint32_t valid_mask, users, tick;
    uint32_t queue_errors, backpressure, refreshes, spi_errors, init_errors;
    int32_t last_spi_error;
} display_ui_view_t;
void Display_ReadUIView(display_ui_view_t *view);
bool Display_UIWelcome(void);
void Display_UIMonitor(bool entering);
void Display_UISchedTrace(bool entering);
void Display_UIConsolidated(bool entering);
#endif
