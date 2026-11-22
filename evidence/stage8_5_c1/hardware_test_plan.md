# 下一阶段硬件验收计划（未执行）

HARDWARE NOT EXECUTED。此阶段不连接目标、不擦写Flash、不改Option Bytes、不复位。必须针对选定C1包及新页范围取得明确授权后再执行；以下只准备步骤。

| 配置 | BIN长度 | 最大有效Flash地址 | 2KiB页数 | 页号 | 页范围末端exclusive |
|---|---|---|---|---|---|
| debug | 76188 | 0x0801299B | 38 | 0–37 | 0x08013000 |
| o2 | 57988 | 0x0800E283 | 29 | 0–28 | 0x0800E800 |

1. 指定ST-Link序列号190057000D0000393333574E核验目标电压、IDCODE、CPUID、UID、512KiB容量和2KiB页几何，与B1.3身份记录对照；Option Bytes/RDP/WRP只读前后快照，禁止解锁绕过。
2. 先校验包manifest与SHA、独立ELF/BIN/HEX验证，再按实际段和上述页范围制定受控擦写。不得沿用B1.4的55240B、27页或旧20页范围。BIN地址须显式0x08000000；HEX保留地址。禁止mass erase，不访问其他仓库。
3. 按授权方案读取需要保护的数据、限定页擦除/编程、独立读回逐字节对比；固件末尾至擦除页末应FF，范围外与操作前一致、Option Bytes一致；任何不一致停止。本文本阶段没有执行任何一项。
4. 板上RESET后记录欢迎约1.5秒及四任务状态、原自动0→1→2→3停留、短按3→4→0且停止自动；核对颜色/图形/5字体/浮点/动画和状态页布局。至少100次切换，记录真实输入/各FIFO计数/LED对应，长按保留原行为。
5. 示波器实测SPI3 SCK目标18MHz；寄存器和计数对照HSE/PLL72MHz、TIM2 1ms、SysTick100Hz。理论频率不能替代测量。
6. GDB观察真实task snapshot、tick、queue、页面刷新/error、PSP/MSP/CONTROL/EXC_RETURN和R4–R11；检查LCD绘制期间KEY/Controller能及时推进。无yield CPU压力需要独立审阅诊断方案，当前生产应用未额外增加测试任务。
7. 检查DMA源生命周期、分块/TC/TE/BSY/CS时序与恢复码。长块预算已知OPEN，必须独立驱动对照和硬件测试，不以当前短行路径证明所有DMA通过。
8. 运行至少30分钟，采集开始/中间/末尾的真实计数、错误、任务状态和所有active stack guard/水位/MSP；检查无饥饿、停滞、HardFault。最后板上RESET再验启动。真实闪烁、帧率、18MHz、guard和稳定性当前均NOT MEASURED。

没有新增UART、CPU使用率、上下文切换累计或HardFault计数接口，不能伪造输出。初始化失败只有有限重试与RAM诊断，确认现场后单独安排恢复；不得自动重烧/修改Option Bytes。
