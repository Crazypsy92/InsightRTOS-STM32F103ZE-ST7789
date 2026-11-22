# 最终链接资源与栈预算

Cortex-M3 Thumb soft-float，Trace 关闭，RTOS_TESTING=0，100 Hz。实测文件与 MAP/PT_LOAD 来自最终 clean build。原链接脚本、向量表、SystemInit、Reset_Handler、SVC/PendSV/SysTick/TIM2 归属保留。无未解析符号、semihosting、链接堆或测试入口。

| 资源 | C3 Debug | C3 O2 |
|---|---:|---:|
| Flash | 77,416 B（比 C1 +1,228） | 58,804 B（比 C1 +816） |
| Flash LMA 范围 | [0x08000000, 0x08012E68) | [0x08000000, 0x0800E5B4) |
| 覆盖页（2 KiB） | 0–37 | 0–28 |
| DATA | 144 B | 144 B |
| BSS（含 Idle 和 TCB） | 4,772 B | 4,768 B |
| TCB 池（已计入 BSS） | 576 B | 576 B |
| 用户静态栈（4 活跃、4 预留） | 32,768 B | 32,768 B |
| Idle 栈（已计入 BSS） | 512 B | 512 B |
| MSP（0x2000F000–0x20010000） | 4,096 B | 4,096 B |
| Heap | 0 B | 0 B |
| SRAM 已使用（含对齐和 MSP） | 41,784 B | 41,776 B |
| SRAM 剩余 | 23,752 B | 23,760 B |

SRAM 布局与 C1 一致。链接器 RAM 摘要以栈顶位置显示 64 KiB，以上是静态使用量及 MSP 区间核算。向量表 VMA 为 0x08000000，初始 MSP 0x20010000。Reset 向量分别为 0x080016C1 和 0x080010B9。DATA 位于 SRAM，加载数据在 Flash；NOBITS 不增加写入。ELF/BIN/HEX 地址和内容完全一致，装载区间及各分配段无交叉和越界。

| 任务/例程 | 预留 | Debug 编译器帧 | O2 编译器帧 |
|---|---:|---:|---:|
| KEY | 4,096 B | 32 B | 16 B |
| Controller | 4,096 B | 160 B | 176 B |
| LCD | 4,096 B | 64 B | 64 B |
| Monitor | 4,096 B | 96 B | 88 B |
| Idle | 512 B | 沿用内核 | 沿用内核 |
| TIM2 ISR（MSP） | 4,096 B MSP 共享 | 8 B | 0 B |
| SysTick ISR（MSP） | 同上 | 8 B | 0 B |
| Scheduler 切换 C 例程（MSP） | 同上 | 24 B | 16 B |

Debug 的 `lcd_task → Display_PagesExecute → LCD_Test_Task → Display_UIMonitor → row → LCD_ShowString → LCD_ShowChar → lcd_write_bytes → Port_Write → spi_bus_transfer → ll_transfer → ll_transfer_dma → dma_timeout_cycles` 可见帧合计 840 B。O2 中合并进 `ll_transfer`（120 B），同路径可见 C 帧合计 608 B。最终 ELF 的除法辅助函数经反汇编核算额外 48 B。计入 PSP 最多 68 B 线程异常上下文，该选链 Debug 956 B、O2 724 B。O2 所选错误路径经 `spi_recover → ll_configure`，加 40+32 B 帧与 68 B 异常上下文，合计 748 B。

上述是选定新增驱动链预算，完整记录保存在 `stack-usage-*.txt`、`division-stack-*.txt` 和编译参数。现有浮点字体页、库调用、嵌套 ISR 与真机 PSP/MSP 高水位继续按同一 4 KiB 栈预算验收。C3 物理栈水位为 **NOT MEASURED**。
