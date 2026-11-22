# PSP与MSP预算及调用链

8×4096B静态用户栈，四任务各4096B，四槽预留未创建；内核Idle512B；MSP独立4096B。内核填充/guard保持，TCB池576B。没有扩大B1.4栈预留、没有动态栈。硬件水位/guard状态NOT MEASURED。

| 配置 | 函数 | 编译器自身frame B |
|---|---|---|
| debug | key_task | 32 |
| debug | controller_task | 160 |
| debug | lcd_task | 64 |
| debug | monitor_task | 96 |
| debug | Display_UIWelcome | 152 |
| debug | Display_UIMonitor | 168 |
| debug | TIM2_IRQHandler | 8 |
| debug | rtos_sched_switch | 24 |
| o2 | key_task | 16 |
| o2 | controller_task | 176 |
| o2 | lcd_task | 64 |
| o2 | monitor_task | 88 |
| o2 | Display_UIWelcome | 168 |
| o2 | Display_UIMonitor | 168 |
| o2 | TIM2_IRQHandler | 0 |
| o2 | rtos_sched_switch | 16 |

Cortex-M3基本上下文由R4–R11软件保存32B及硬件R0–R3/R12/LR/PC/xPSR32B组成，STKALIGN可多4B；EXC_RETURN=0xFFFFFFFD，CONTROL.SPSEL沿用原移植。任何frame列不等于整个调用链最坏水位。

选择的同步显示链：lcd_task → Display_PagesExecute → LCD_Test_Task → Display_UIMonitor → row → LCD_ShowString → LCD_ShowChar → lcd_write_bytes → Port_Write → spi_bus_transfer → ll_transfer → ll_transfer_dma → dma_wait → ll_timeout_expired → ll_dwt_cycles。该链跨函数指针的Port/LL边根据原Port表人工确认，.su可见frame合计见stack-analysis.json。Debug选链所有可见frame合计716B，另留最多68B线程异常上下文；O2会内联合并部分函数，缺失项不按独立frame再次相加。UIWelcome路径使用同样协议栈，内部有有界38B文本和84B快照，不在栈分配像素图。

这只是已知同步链证据，不是覆盖所有分支、newlib汇编或所有间接调用的严格WCET/最大栈证明。完整-fstack-usage、最终反汇编和符号已交付。错误恢复、浮点文字页、公共Queue和断言分支亦需硬件高水位确认，不能用剩余SRAM替代guard检查。

MSP路径：TIM2 → 单按键扫描 → MultiButton回调 → 非阻塞FromISR Queue，可能被更高优先级SysTick抢占；PendSV优先级最低不会嵌套在TIM2内执行。SVC启动恢复PSP，PendSV在MSP保持8字节对齐并调用原scheduler。4096B预算与链接ASSERT保持，.su保留TIM2/scheduler等frame；无FPU扩展帧。尚未实测MSP最坏嵌套水位，列为后续验收项。
