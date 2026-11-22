# C2 真机任务与上下文

PASS，源为指定ZET6的实际GDB/SWD证据。首次板载RESET捕获正常Reset_Handler/MSP，原SystemInit/main进入rtos_start/SVC；KEY5、Controller4、LCD2、Monitor1真实入口均捕获，四PSP落在各4096B栈内、CONTROL.SPSEL合法。Idle真实入口及其独立512B PSP通过，另有自由运行current TCB=Idle的直接观测（idle-free-run.json），不把抽样比例称CPU占用率。

三次PendSV对真实R4–R11逐字验证保存和恢复，至少一次实际TCB切换；SVC/PendSV EXC_RETURN=0xFFFFFFFD。SVC0/SysTick8/TIM2 10/PendSV15。VTOR=0按之前批准的Flash别名口径，与0x08000000向量逐字一致。

连续窗口计数：

| 任务 | 起点 | 末点 | 增量 |
|---|---|---|---|
| KEY | 6 | 17 | 11 |
| Controller | 9923 | 190429 | 180506 |
| LCD | 735 | 48277 | 47542 |
| Monitor | 186 | 3602 | 3416 |

实际窗口1805.013秒。SysTick平均100.00262Hz，TIM2平均1000.02451Hz；寄存器分别LOAD719999、PSC71/ARR999，HSE/PLL/HCLK72MHz配置通过。KEY在无人输入时阻塞，不把执行次数不增加误判为饥饿；人工输入事件已逐级完整递交。

没有新增固件上下文切换总计数或HardFault累计计数接口，报告的是已捕获切换与期间未观察到故障，不编造全局次数。
