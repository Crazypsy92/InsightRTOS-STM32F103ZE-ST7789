# InsightRTOS Stage 8.5-C1 验收

2026-11-20。**SOFTWARE PASS**；**HARDWARE NOT EXECUTED**。只改本仓库实际B1.4四任务应用，未修改上游公共内核、TCB、SVC/PendSV汇编、SPI/DMA驱动、字体或MultiButton；旧阶段包与证据保留，没有烧录/擦除/Option Bytes操作或MCU复位。

| 项目 | 本轮结果 | 证据 |
|---|---|---|
| 源码归属/原状态机先审计 | PASS | page_state_machine.md，baseline冻结源 |
| 欢迎+五页、进入/退出Monitor | PASS SOFTWARE | Host Debug/O2各7/7，key_event_test.csv |
| 原四页/动画/MultiButton/LED语义 | PASS SOFTWARE，原绘制函数及字体不变 | integrity.json，Host日志，裸机BIN对照 |
| 真实公开snapshot/Tick/计数来源 | PASS SOFTWARE | task_snapshot_report.md，UI测试 |
| 队满背压/唯一键消费/终止重试 | PASS SOFTWARE | 102事件逐级一致，慢LCD2224次保留重试，最多3次初始化 |
| 原内核完整回归 | PASS，2155次执行 | final-regression.log及完整归档 |
| Debug/O2严格构建与裸机对照 | PASS | build-*.log，bare-*.log，build_report.md |
| 新增12项负面链接/静态内存/唯一向量 | PASS | negative-links.json，memory_map.md |
| ELF/BIN/HEX/MAP及SHA独立核验 | PASS | artifacts/stage8_5_c1，firmware-verify.log |
| SPI/DMA长块 | OPEN已确认预算缺陷；未宣称全路径PASS | dma-budget.csv，spi_dma_audit.md |
| 实机五页/100次切页/18MHz/抢占/栈/30分钟/RESET | NOT EXECUTED / NOT MEASURED | hardware_test_plan.md |

| 资源（B，子项不重复相加） | Debug | O2 |
|---|---|---|
| flash | 76188 | 57988 |
| data | 144 | 144 |
| bss | 4772 | 4768 |
| user_stacks | 32768 | 32768 |
| active_user_stacks | 16384 | 16384 |
| reserved_unused_user_stacks | 16384 | 16384 |
| idle_stack_in_bss | 512 | 512 |
| msp | 4096 | 4096 |
| heap | 0 | 0 |
| ram_with_alignment | 41784 | 41776 |
| remaining_sram | 23752 | 23760 |

| 配置 | BIN长度 | 最大有效Flash地址 | 2KiB页数 | 页号 | 页范围末端exclusive |
|---|---|---|---|---|---|
| debug | 76188 | 0x0801299B | 38 | 0–37 | 0x08013000 |
| o2 | 57988 | 0x0800E283 | 29 | 0–28 | 0x0800E800 |

固件是新版本，不能使用旧55240B长度或旧页范围。状态页250ms检查、只重画变化行，无CPU使用率/假切换计数；初始化失败有界处理并保留RAM诊断。实际电气时序、显示效果、闪烁及稳定性尚未验收。当前已停止，等待人工审核及针对C1新固件/擦除范围的明确烧录授权。
