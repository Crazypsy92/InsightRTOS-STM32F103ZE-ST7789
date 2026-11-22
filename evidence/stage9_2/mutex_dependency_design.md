# Stage 9.2: Mutex 资源依赖快照机制设计文档 (Mutex Dependency Design)

## 1. 概述与设计目标
InsightRTOS Stage 9.2 实现了第二项内核诊断能力：**资源依赖关系快照与死锁诊断**。
该机制使得嵌入式系统在受限资源下，能够精确发现任务间的互斥量锁竞争、所有者归属、优先级继承状态，并构建资源依赖有向图。

## 2. 核心设计原则
1. **零动态内存分配**：所有数据结构使用调用者传入的专用工作空间（Workspace），内核不调用 `malloc`/`free`。
2. **极短关中断时间**：快照提取阶段（`rtos_diag_dl_capture`）在关中断临界区内仅执行浅表复制（遍历不超过 8 个任务槽位），执行时间小于 3 µs，不影响硬实时中断响应。
3. **图分析在普通任务上下文进行**：循环检测算法（`rtos_diag_dl_analyze`）与结果提取严格在任务上下文中执行，禁止在 ISR、SysTick 或 PendSV 中调用。
4. **确定性内存布局**：在 32-bit ARM Cortex-M 架构下，工作空间结构体 `rtos_diag_dl_workspace_t` 严格占用 **384 字节**，并通过 `_Static_assert` 在编译期强制校验。
5. **世代与生命周期感知**：通过任务插槽世代号（Slot Generation）和活动状态，检测任务退出（`TERMINATED`）、槽位复用和无效快照，避免出现悬垂指针或陈旧引用。

## 3. 数据结构与 ABI 设计
在 `include/rtos_diag.h` 中定义了公共快照与结果结构体：

### 3.1 快照头部 `rtos_diag_dl_header_t` (24 字节)
- `capture_tick` (uint32_t): 捕获时内核 Tick 计数值。
- `node_count` (uint16_t): 快照中的活动任务节点数（上限 8）。
- `edge_count` (uint16_t): 依赖有向边数量。
- `mutex_count` (uint16_t): 涉及的互斥量数量。
- `flags` (uint16_t): 快照状态标志（`ANOMALY`, `INCOMPLETE`）。
- `captured` (uint8_t): 捕获完成标志。
- `analyzed` (uint8_t): 分析完成标志。
- `reserved[10]`: 对齐填充。

### 3.2 依赖图节点 `rtos_diag_dl_node_t` (24 字节)
每个节点代表一个任务及其出边（指向它所等待互斥量的所有者）：
- `slot` (uint16_t): 任务插槽索引 (0 ~ 7)。
- `state` (uint16_t): 任务状态 (READY / RUNNING / BLOCKED 等)。
- `generation` (uint32_t): 任务生成世代号。
- `base_priority` (uint8_t): 静态基础优先级。
- `effective_priority` (uint8_t): 当前动态有效优先级（受 PI 提升）。
- `owner_slot` (uint16_t): 出边目标（等待的互斥量所有者插槽，无出边则为 `0xFFFF`）。
- `owner_generation` (uint32_t): 目标所有者任务捕获时的世代号。
- `resource_id` (uint16_t): 所等待互斥量的紧凑快照 ID (1 ~ 8)。
- `edge_flags` (uint16_t): 边属性（`FINITE` 超时等待, `PI_BOOST` 优先级提升）。
- `wake_tick` (uint32_t): 超时到期 Tick。

### 3.3 工作空间 `rtos_diag_dl_workspace_t` (384 字节)
```c
typedef struct {
    rtos_diag_dl_header_t header;                     /*  24 B */
    rtos_diag_dl_node_t   nodes[RTOS_DIAG_DL_MAX_NODES];/* 192 B (8 * 24 B) */
    uintptr_t             resource_keys[8];           /*  32 B (32-bit ARM: 8 * 4 B) */
    uint16_t              task_slots[8];              /*  16 B (对齐占 32 B) */
    uint8_t               scratch[24];                /*  24 B (visited 8B + path 8B + aux 8B) */
    rtos_diag_dl_result_t result;                     /*  48 B */
    uint8_t               reserved[32];               /*  32 B 缓冲保留 */
} rtos_diag_dl_workspace_t;
```

## 4. 快照提取流程 (`rtos_diag_dl_capture`)
1. **前置检查**：检查指针有效性，确保当前在线程模式且未关中断（`!rtos_port_in_handler() && !rtos_port_interrupts_masked()`）。
2. **进入极短临界区**：调用 `rtos_port_irq_save()`。
3. **第一阶段扫描**：调用 `rtos_task_snapshot_locked` 收集当前非 `UNUSED` 且非 `TERMINATED` 的活动任务，记录任务插槽号、插槽世代号、状态及基础/有效优先级。
4. **第二阶段关系构建**：遍历节点，检查处于 `RTOS_TASK_BLOCKED` 且 `wait_reason == RTOS_WAIT_MUTEX` 的任务：
   - 提取其等待的 `rtos_mutex_t`。
   - 检查 `mutex->owner`，并在节点列表中查找所有者对应索引。
   - 若所有者不在节点列表中，标记 `RTOS_DIAG_DL_FLAG_INCOMPLETE`。
   - 若出现自环（等待自己持有的锁），标记 `RTOS_DIAG_DL_FLAG_ANOMALY`。
   - 正常情况下记录 `owner_slot`、`owner_generation`、`resource_id`，并根据 `delay_queued` 标记 `RTOS_DIAG_DL_EDGE_FINITE` 及 `wake_tick`。
5. **退出临界区**：调用 `rtos_port_irq_restore()`，标记 `work->header.captured = 1`。
