# Stage 9.3-B1 测试和资源记录

## Host

| 项目 | 入口 | 结果 |
|---|---|---|
| A3 合入门禁 | A3 SHA256 校验、应用与 RT Host、历史 CTest | 证据完整，应用 9/9，RT 9/9，内核 360/360 |
| 应用 | `bash scripts/test/run_host.sh` | 9/9，45 项历史断言 |
| RT 与 Job | `bash scripts/test/run_rt_host.sh` | 历史 9/9，另有 25 + 15 + 17 + 20 项 Job 检查 |
| B1 内核回归 | `ctest --test-dir <临时目录>/build --output-on-failure -j6` | 360/360 |
| QEMU | `bash scripts/test/run_rt_qemu.sh` | NOT_TESTED |

B1 的 360 项在内核项目临时副本中启用 Host、Trace、Explain、DL、RT、Stage 9.1 和 Stage 9.2。测试构建使用 B1 的 RT 头文件、实现、内部头和 task.c，并将 diag_rt.c 加入三个历史测试目标的源清单。测试 ID、断言和期望无修改。初次配置遗漏链接源，修正临时构建清单后全量重新构建并通过，保留两次原始日志。

新增测试使用生产 Job API、真实调度器、任务退出和代数回收路径，使用假 Tick/DWT 驱动边界。覆盖未配置、单项和完整配置，抢占/阻塞/醒来分段计费，首调度与完成延迟，Deadline 和预算的等值与超限，错误上下文、重复/越权完成、重叠发布、非法配置和 ID 饱和，以及两种时钟回绕、时钟不可用或不确定、显式和隐式 Abort、代数复用。ID 饱和使用 Host 专用注入入口。

## ARM 和固件

用 `cmake --preset <配置>` 和 `cmake --build --preset <配置>` 完成严格构建。ELF 可加载 Flash 字节与 BIN 一致，独立解析 HEX 校验完整段内容与地址。Job BIN 包含 4 B 由 HEX 省略的段间零对齐，因此本次板上使用 BIN。MAP/ELF 校验向量、入口、段范围、72 B TCB、静态栈与 heap=0；符号检查确认 RT OFF 镜像移除 RT Job 函数、Hook 和静态状态，普通 Debug/O2 镜像不含 JobTest 或内部测试入口。使用统一源路径前缀比较合入前后的 RT OFF 镜像时，两者均为 46,068 B，二进制仍有差异。可见差异包括 assert 源码行号和常量布局，因此 RT OFF 的结论依据功能回归与符号检查。

| 配置 | BIN / B | SRAM 含 MSP / B | SRAM 余量 / B |
|---|---:|---:|---:|
| Debug Trace | 72,564 | 49,336 | 16,200 |
| O2 Trace | 51,160 | 49,344 | 16,192 |
| O2 无诊断模块 | 46,408 | 44,528 | 21,008 |
| O2 Job | 54,700 | 49,600 | 15,936 |
| O2 Trace，仅 RT OFF | 48,896 | 48,016 | 17,520 |
| O2 Trace，强制保留完整 Job API | 52,712 | 49,352 | 16,184 |

全部配置保留 8 × 4,096 B 用户栈、512 B Idle、4,096 B MSP及 0 B heap。JobTest 使用第五个预留栈，fixture 自身 O2 帧为 128 B。RT 常驻结构为 960 B 任务表、24 B Idle、32 B 全局和 8 B 统计。应用导出帧、链接填充与测试记录体另计。

Flash ON/OFF 对照保持 Trace、Explain、DL 和编译选项一致。第二次链接在同一 O2 Trace 配置上用 GNU ld `-u` 保留 `configure`、`release`、`complete`、`abort`、`snapshot`，峰值增量为 3,816 B。MAP 中 diag_rt.c 直接贡献 3,332 B，另有应用导出增量 264 B，port、scheduler、task 合计 216 B，以及 4 B 对齐。JobTest 整包大小单列。

栈对比使用各函数 .su 帧与反汇编核对的可达子调用，按同一用例的 A3→B1 RT 子链计算。O2 switch 最大 48 → 64 B，tick 24 → 64 B；Debug switch 156 → 184 B，tick 68 → 120 B。Debug 新 Job snapshot 公共入口自身帧 144 B，历史 RT snapshot 入口自身 80 B。不会同时将所有兄弟调用帧相加。结果是静态路径估计，未测 MSP 水位，亦未覆盖任意 IRQ 嵌套下的实机峰值。

## 实机

- OpenOCD 核验已记录 ZET6 的 IDCODE、CPUID、UID、512 KiB 容量及保护状态；写入前页 0–30 整个范围与 A3 独立读回一致。
- 擦除前核对 2 KiB 页几何和地址，只擦除页 0–30。写前空白检查通过。独立读回、镜像 SHA256、尾部填充值和 Option Bytes 对照通过。
- 通过 SYSRESETREQ 运行 Job profile。GDB 解析 ELF DWARF 来定位字段，运行样本用 OpenOCD MEM-AP 读取；连续窗口内无主动暂停，以一致性序号及新鲜度核验读数。Job 数据另核验发布序号和 JobTest guard。
- 续跑采样为 62.205 s，测得约 100.008 Hz SysTick 和 1,000.016 Hz TIM2。Controller/LCD/Monitor/Idle dispatch 分别增加 6,200/496/62/6,180。未人工触发 KEY，因此 KEY 初始化后保持等待，Queue 无活动也无错误。
- 在本地工程保存六套 ELF/BIN/HEX/MAP 和 SHA256。原始读回二进制、保护字节快照和完整串行工具日志保留在本地证据目录。
