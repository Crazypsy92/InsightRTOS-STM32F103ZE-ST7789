# Stage 9.3-B2c MSP 实机验收

**HARDWARE_MEASURED**。在同一 STM32F103ZET6 上独立测量启动及预热、正常四任务和 Idle、受控 TIM2 按键负载。实测镜像为 O2 MSP fixture，基线 `147fb17a24112d4f104e36fe3948451e12d07326`。

| 工况 | 时长 | 最低改写地址 | 4 KiB MSP 实测改写深度 | 剩余（含 guard） | 可用余量（扣除 16 B guard） |
|---|---:|---|---:|---:|---:|
| 启动及预热 | 3.000 s | 0x2000FF14 | 236 B | 3,860 B | 3,844 B |
| 正常运行 | 62.000 s | 0x2000FF7C | 132 B | 3,964 B | 3,948 B |
| TIM2 受控负载 | 62.000 s | 0x2000FF04 | 252 B | 3,844 B | 3,828 B |

三个阶段的 MSP 底部四字均保持 0xE25A5ADA。32 份非停机健康采样的四个用户栈和 Idle guard 均完整，无断言、故障、LCD/SPI/DMA 错误。常态无按键刺激，KEY 等待队列；负载接收并确认 600/600 个事件，队列满、拒绝、挂起和失败计数为 0。两轮均为 6,200 个 SysTick、62,000 个 TIM2 IRQ，DWT 有效。启动阶段观察到 SVC 一次；各阶段均观察到 PendSV、SysTick 和 TIM2。三个候选嵌套组合在入口 active 计数均为 0，标记 NOT_TESTED。启动前期填充完成前、NMI、HardFault 深链和 Debug 实机水位也未覆盖。

O2 镜像 52,104 B，ELF/BIN/HEX 和加载地址独立校验通过。先完整核对板上 B2b RT ON 固件，再受控擦写页 0–30。独立读回一致，尾部 11,384 B 为 FF，Option Bytes 前后一致。诊断镜像保留在板上，未再次擦写。

Host 应用及历史 RT 各 9/9，Job 77 + 53 项通过。ARM Debug/O2 MSP 与生产 O2 Trace 均构建通过；生产镜像无 MSP 探针或注入符号。Debug 镜像超出本轮 63,488 B 授权写入区间，仅作静态分析。没有改动生产调度语义，未重复 360 项历史测试。

[测量与静态分析](msp_stack.md) · [实测位图和入口计数](logs/Msp-watermark.json) · [独立读回](logs/Msp-readback.json)
