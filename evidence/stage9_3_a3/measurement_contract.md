# Stage 9.3-A3 测量契约

- 固件来自隔离工作树，已核对基线 04ddcf93a2f0e595fa13389e176ffdbd5685f82f。板上会话固定 ST-Link 190057000D0000393333574E、UID 31ffd8053031533628791143，单 bank 256×2 KiB。仅按 0–30 页擦写。分别连接读回并按冻结 BIN 逐字节及 SHA256 核验，且检查擦除范围尾部和 Option Bytes。
- 正式采样由非停机 MEM-AP 读内存。起始读回指定固件数据并校验 SHA。GDB 离线获取本次 ARM ELF 的 DWARF，生成字段偏移、大小、符号地址与数组跨度。缺字段、短读、类型不符均抛错；不存在默认零替代。原 A2 数据和原代理尝试均保留。
- 历史每项 24 B，8 项后计数从偏移 192 开始。RT 快照 56 B，ref 从偏移 24 开始占 8 B，flags 在 32。clock_cap 含 5 个 32 位字段。RT 导出 312 B，4 个用户快照 24–247，Idle 248–303，seq_end 304，published_tick 308。统计结构按同一个 ELF 的声明解析，偏移在每次保存的 layout.json。负例修改旧偏移和模拟短读均被拒绝。
- Monitor 每次先取一组 API 值，有失败就不发布。成功发布先标奇数，DMB 后更新字段，写结束序号，DMB 后提交偶数。读端用前后读与结构两端的序号判同一发布；奇数、变更或初始序号按 INCOMPLETE，过龄按 STALE。五个任务条目在同一次 Monitor 发布中分别采集，时间戳记录发布结尾；这些逐任务接口不提供全局同一瞬间视图。应用统计、IPC、tracker 与 RT 取样各自有读出时间，只有起终点在无压力与 pending=0 时作守恒核验。
- stress 镜像测试专用，默认 mailbox 为 0。命令 1 启动 TIM2 ISR 每 100 个有效毫秒节拍一次，固定 3,000 次后自停；命令 2 在一次真实 TIM2 ISR 连续发送有界的 34 次以验证 FULL。关闭、余量为 0 必须读回确认，生产配置无测试 mailbox。300 秒内禁止手工输入，以免混入物理事件。
- 入口只把 RTOS_ERR_FULL 计为 FULL。实际返回按类别记账。每个键的已分配 ID 在有界 pending 表持有一项，独立于 8 槽 UI 历史；在接受前紧邻入队事务登记，拒绝时撤销。真实乱序或历史已覆盖的 ACK 按 ID 只结算一次。未知、撤销及过旧 ACK 单独拒绝，有限的已完成 ID 表识别近期重复。ID 到 32 位上限或活跃索引冲突时拒绝新数据，避免回绕/覆盖旧 in-flight。看板可覆盖历史槽，统计账本不因此丢失。
- 每个已排空的区间核验 attempts = accepted + full + wrong_context + invalid + other、accepted = completed + failed + Δpending、drop = full + wrong_context + invalid + other。中途快照供趋势检查，不能把不同读时的 pending、accepted 和 completed 当作原子交易。ACK 命令重试保持原 ID，不产生新的入队尝试。
- RT 只接受 clock/task flags=VALID，保留真实 ref/gen、Idle、有效优先级、Tick、序号、dispatch_count 和 READY latency。charged_cycles 是归属到已选线程的 DWT 周期，包含 IRQ 等处理。百分比用各任务增量除五者之和，边界跨多次 API 调用，故为近似份额。Ready 最大值是观测上界样本，DWT 两端同时暂停及漏样无法给绝对 WCET。无 deadline/budget 功能或推断。
- ACK ID 校验范围为本次启动同一应用实例。Controller 仍检查 LCD ACK 的 FIFO sequence，只有 LCD 是 ACK 生产者。账本的 pending ID 用于防止未知或重复结算，未将 ACK 信道设计为跨复位持久或为不可信生产者提供认证。
