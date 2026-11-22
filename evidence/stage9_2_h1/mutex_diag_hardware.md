# Stage 9.2-H1: 死锁诊断硬件实机验证报告 (Mutex Diag Hardware)

## 一、验证目标与安全准则

验证 Stage 9.2 实现的 Mutex 资源依赖快照（`rtos_diag_dl_capture`）与死锁分析（`rtos_diag_dl_analyze`）在真实 STM32F103ZET6 + ST7789 硬件上的执行行为：
1. **真实 API 调用**：诊断数据全部由内核诊断接口实时生成，禁止使用静态模拟假数据。
2. **非阻塞性**：快照关中断时间严格受限（< 3 µs），图搜索算法在任务上下文中执行，不阻碍 LCD、KEY、Controller 或 Monitor 任务调度。
3. **安全准则**：在正常生产应用中，不得故意制造永久死锁破坏硬件运行；物理死锁注入标记为 `NOT TESTED`，保留 Host/QEMU 的既有测试通过结论。

---

## 二、实机诊断数据结构与指标核验

通过 OpenOCD 实时读取处于 RAM 中的 `dl_work.0` 结构体（地址 `0x20000bb0`）：

### 1. 快照头部 (`rtos_diag_dl_header_t`)
- `capture_tick`: 实时内核 Tick（动态递增）。
- `node_count`: **4**（系统识别出 4 个活动业务任务：KEY, Controller, LCD, Monitor）。
- `edge_count`: **0**（正常生产状态下各任务无 Mutex 阻塞等待出边）。
- `mutex_count`: **0**。
- `flags`: **`0x0000`**（无 `ANOMALY`，无 `INCOMPLETE`）。
- `captured`: **1**（快照成功标志）。
- `analyzed`: **1**（图分析算法完成标志）。

### 2. 分析结果 (`rtos_diag_dl_result_t`)
- `deadlock_detected`: **`false` (0)**。
- `cycle_length`: **0**。
- `finite_edge_count`: **0**。
- `flags`: **`0x0000`**。
- 诊断状态行渲染：`Status: NO DEADLOCK`，字体呈现绿色。

---

## 三、多任务并发与系统健康度核验
1. **任务运行计数**：在诊断运行期间，Controller 任务处理事件计数正常增加，LCD 任务按 250 ms 节拍刷新，Monitor 任务每秒输出心跳，KEY 任务响应就绪。
2. **栈保护与水位**：四任务 PSP 均在为各任务分配的 1024 字节独立栈空间范围内波动，`dl_work` 占用的 384 字节位于静态 BSS 段，不额外占用任务运行时栈空间。
3. **零异常发生**：`g_rtos_assert_expression == 0`，未触发任何断言失败、未进入 HardFault、未触发总线错误（BusFault）。
