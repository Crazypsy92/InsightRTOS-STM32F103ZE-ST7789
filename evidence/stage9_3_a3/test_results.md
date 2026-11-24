# Stage 9.3-A3 实测结果

| 实验 | 耗时 s | 有效/无效快照 | Attempt | Accepted | FULL | Context | 错误 ACK | 成功 ACK | Pending 起→终 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| normal | 62.288 | 13/0 | 0 | 0 | 0 | 0 | 0 | 0 | 0→0 |
| stress | 303.678 | 61/0 | 3000 | 3000 | 0 | 0 | 0 | 3000 | 0→0 |
| burst | 15.428 | 4/0 | 34 | 33 | 1 | 0 | 0 | 33 | 0→0 |

## 修订前后

| 项目 | A2 原报告 | A3 实际证据 |
|---|---|---|
| 常态 | 60 s、13 样本 | 62.288 s、13 有效样本，Tick 100.003 Hz、TIM2 1000.011 Hz |
| 300 s 压力 | 574 次被称受控丢弃，未保存返回码 | TIM2 ISR 实际 3,000 次，FULL=0，CONTEXT=0，流量两级守恒 |
| 短时突发 | 旧数据不能证明实机 FULL | 1 次真实 FULL，随后已接受事件排空 |
| RT 字段 | 手算字偏移 | 当前 ELF DWARF + 反例自检 + seqlock 与 freshness 验证 |

旧 A2 Monitor 普通 Thread 调用了真正的 FromISR wrapper。以生产 App_IPC 和内核 queue.c 在 Host 复现返回 RTOS_ERR_CONTEXT。历史 574 次没有逐次返回码，无法事后逐条归因，旧的 Queue FULL 解释缺乏证据。

## READY 延迟及周期份额

| 窗口 | 任务 | slot/gen | prio base/effective | dispatch 增量 | DWT 周期增量 | 归属份额 | 最大观测 READY cycles / ticks |
|---|---|---|---|---:|---:|---:|---|
| normal | KEY | 0/1 | 5/5 | 0 | 0 | 0.000% | 29,754 / 0 |
| normal | CTRL | 1/1 | 4/4 | 6,200 | 25,604,996 | 0.574% | 24,711 / 0 |
| normal | LCD | 2/1 | 2/2 | 496 | 30,264,499 | 0.678% | 19,905 / 0 |
| normal | MON | 3/1 | 1/1 | 62 | 886,120 | 0.020% | 1,072,961 / 1 |
| normal | Idle | 8/1 | 0/0 | 6,180 | 4,407,244,369 | 98.729% | 0 / 0 |

normal：有效发布间隔实测样本中位 5.000 s，按 13 个有效样本计算。此处为外部采样选中的发布间隔；Monitor 内部可能在两次采样之间多次发布。

| stress | KEY | 0/1 | 5/5 | 3,000 | 12,103,407 | 0.055% | 29,752 / 0 |
| stress | CTRL | 1/1 | 4/4 | 30,300 | 134,637,898 | 0.617% | 24,708 / 0 |
| stress | LCD | 2/1 | 2/2 | 24,036 | 13,996,102,118 | 64.155% | 19,902 / 0 |
| stress | MON | 3/1 | 1/1 | 303 | 2,711,129 | 0.012% | 3,979,912 / 5 |
| stress | Idle | 8/1 | 0/0 | 15,286 | 7,670,452,084 | 35.160% | 0 / 0 |

stress：有效发布间隔实测样本中位 5.000 s，按 61 个有效样本计算。此处为外部采样选中的发布间隔；Monitor 内部可能在两次采样之间多次发布。

READY 最大值为本次启动至窗口末的观测高水位。DWT 归属周期含 IRQ 和切换边界开销，份额为近似观测值。

## 压力逐分钟边界

| 阈值 s | 首个满足阈值的样本 s | 累计 attempt 增量 | FULL |
|---:|---:|---:|---:|
| 60 | 60.733 | 607 | 0 |
| 120 | 121.449 | 1214 | 0 |
| 180 | 182.193 | 1821 | 0 |
| 240 | 242.947 | 2429 | 0 |
| 300 | 303.678 | 3000 | 0 |

## 软件回归和资源

- 应用 Host 9/9，含实际生产入口、坏上下文、FULL、乱序/重复/未知 ACK 及计数守恒。
- RT Host 9/9，含生产 App_IPC 和内核 FromISR 的旧 Thread 路径复现。内核 ctest 360/360。
- 两个 ARM ELF 通过 DWARF 布局与偏移负例。QEMU **NOT_TESTED**，未运行该板 fixture。
- 运行命令：bash scripts/test/run_host.sh、bash scripts/test/run_rt_host.sh、ctest --test-dir build-host-stage9_2 --output-on-failure；ARM 用 cmake --preset 与 cmake --build --preset。采样及烧录见同目录脚本。

| 构建 | Flash B | SRAM 含 MSP B | 剩余 SRAM B |
|---|---:|---:|---:|
| DISPLAY_RTOS_O2 | 46,408 | 44,528 | 21,008 |
| DISPLAY_RTOS_O2_Trace | 50,648 | 48,896 | 16,640 |
| DISPLAY_RTOS_O2_Stress | 50,732 | 48,912 | 16,624 |

明确占用以 C3 系列同样的栈预留口径计算。OFF→ON 增量为 Flash 4,240 B、SRAM 4,368 B。O2 Monitor 自身帧 392 B（.su）。逐函数栈与完整链接尺寸见 logs/resource-*.txt。

无 HardFault/Lockup、断言或 guard 异常被采样观察到。物理 LCD 观感待用户确认。未开展独立 WCET 或硬实时保证验收。
