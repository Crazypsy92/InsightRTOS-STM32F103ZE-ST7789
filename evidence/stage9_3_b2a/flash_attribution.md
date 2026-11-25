# Stage 9.3-B2a Flash 归因

修改前后使用 ARM GCC 14.2.1、相同源码目录、相同应用源清单和链接参数。O2 使用 DISPLAY_RTOS_O2_Trace，Trace、Explain、DL 均开启，OFF 仅覆盖 INSIGHT_DIAG_RT。用原 Ninja 链接命令生成独立对照镜像，前后均以 GNU ld 的 -u 保留 configure、release、complete、abort、job_snapshot、clock_cap。OFF 自身无对应 API 定义。编译、链接命令及实际地址和大小分别来自原始构建日志、ELF、MAP、nm 和反汇编。

| O2 配置 | 优化前 / B | 优化后 / B | 变化 / B |
|---|---:|---:|---:|
| RT OFF | 48,908 | 48,908 | 0 |
| RT ON，完整 API | 52,780 | 52,156 | −624 |
| RT 增量 | 3,872 | 3,248 | −624 |

完整 API 版本中 diag_rt.c 的 MAP Flash 输入节 3,392 → 2,764 B，减少 628 B，其他文件贡献不变，布局和填充抵消 4 B。现有 SPI 传输在 OFF 与 ON 都需要 64 位除法帮助函数，因此只改写 clock capability 的除法不能消除该依赖。

| 函数或组合 | 之前 / B | 之后 / B |
|---|---:|---:|
| release | 452 | 260 |
| complete、abort 与共享结束处理 | 508 | 252 |
| configure | 252 | 200 |
| Job snapshot | 296 | 264 |
| switch hook | 384 | 296 |
| Tick hook | 128 | 108 |
| 任务和 Idle 快照 | 540 | 408 |
| DWT 周期拼接 | 108 | 92 |
| clock capability | 116 | 112 |

表内部分代码移至共用函数，差额不可直接相加。[MAP/nm 原始记录](logs/resources.json) · [调用反汇编](logs/disassembly-extract.log)。

试过进一步合并查询上下文、改写预算比较、重排切换分支。相对本方案，合并查询无收益，预算比较多 16 B，重排切换多 40 B。显式 libc 清零能额外省 8 B，但会在通用内核诊断中增加显式库依赖。因此保留公共接口、边界判断和直接字段导出，按任务约定记录剩余 176 B 的预算例外。

Debug RT OFF/ON BIN 分别为 68,764/72,224 B。O2 常规 RT ON 为 51,004 B，完整 API 的静态 SRAM 含 MSP 为 49,344 B。RT 内核状态由 960 B 任务表、24 B Idle、24 B 全局和 8 B 计数组成。O2 switch 和 Tick hook 的静态自身帧均为 40 B，尚无实际嵌套水位和周期数据。
