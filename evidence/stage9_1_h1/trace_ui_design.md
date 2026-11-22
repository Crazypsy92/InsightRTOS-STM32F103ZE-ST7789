# Stage 9.1-H1 ST7789 调度诊断界面设计说明 (SCHED TRACE UI Design)

## 一、页面概述与定位 (Overview)

在保留 STM32F103ZET6 + ST7789 四任务五页面硬件应用的基础上，新增第 6 个 LCD 页面：
**`SCHED TRACE`（调度解释与事件诊断）**

该页面通过读取内核 Explainable Scheduling 模块的真实 Trace 环形缓冲区快照，将底层的任务就绪、阻塞、抢占、时间片轮转与 PendSV C 移交事件转化为直观的因果解释呈现给开发人员。

---

## 二、屏幕布局与视觉规约 (Screen Layout)

ST7789 分辨率为 240 × 240 像素（RGB565 色深）。采用 ASCII_Font20（标题）与 ASCII_Font12（数据行，每行 6×12 像素，支持 37 字符列）：

```text
+---------------------------------------+  y = 0
| SCHED TRACE                           |  y = 0..26 (DARKBLUE 顶栏, Font20 白色)
+---------------------------------------+  y = 27
| DIAG: ACTIVE (128 REC)                |  Row 0, y = 34  (绿色/灰色)
| Seq:1240 Ovr:1112 Lost:0              |  Row 1, y = 54  (黄色，统计单调序列、覆盖数)
| Event: C_SWITCH @42150                |  Row 2, y = 74  (白色，事件名与内核 Tick)
| Class: HIGHER_PREEMPT                 |  Row 3, y = 94  (青色/专属因果分类色彩)
| From: Controller [s1 g10]             |  Row 4, y = 114 (白色，源任务名及代次，过期带?)
| To  : KEY [s0 g10]                    |  Row 5, y = 134 (青色，目标任务名及代次)
| Cause: 0x00000001                     |  Row 6, y = 154 (黄色，内核调度原因位码与辅助值)
| * C-COMMIT ONLY (PSP PEND)            |  Row 7, y = 174 (黄色警示，明确 C 提交边界)
|   (NOT ARM REG RESTORE PROOF)         |  Row 8, y = 194 (灰色警示，明确非 ARM 寄存器证明)
| KEY1 -> Next Page [6/6]               |  Row 9, y = 214 (白色导航提示，标示总页数)
+---------------------------------------+  y = 240
```

---

## 三、行定义与数据语义 (Row Specification)

| 行号 | 字段 | 典型输出 | 颜色编码 | 语义说明 |
|---|---|---|---|---|
| **顶栏** | 页面标题 | `SCHED TRACE` | 蓝底白字 | 标识第 6 页诊断功能 |
| **Row 0** | 诊断启用状态 | `DIAG: ACTIVE (128 REC)` | 绿色 (ACTIVE) / 灰色 (DISABLED) | 标识内核是否开启 Trace 及可解释模块 |
| **Row 1** | 序列与统计 | `Seq:1240 Ovr:1112 Lost:0` | 黄色 | `Seq`: 64 位单调事件计数；`Ovr`: 环形覆盖条数；`Lost`: 丢弃计数 |
| **Row 2** | 事件与时间 | `Event: C_SWITCH @42150` | 白色 | 最新事件类型名称及其发生的系统内核 Tick |
| **Row 3** | 移交分类 | `Class: HIGHER_PREEMPT` | 动态分类色彩 | 调度决策因果分类（详见第四节色彩规约） |
| **Row 4** | 切出任务 | `From: Controller [s1 g10]` | 白色 | 移交前运行任务（槽位+代次）；代次不匹配时标记 `?` |
| **Row 5** | 切入任务 | `To : KEY [s0 g10]` | 青色 | 移交后接管任务（槽位+代次）；代次不匹配时标记 `?` |
| **Row 6** | 原始原因码 | `Cause: 0x00000001` | 黄色 | 调度器原因位图（HIGHER=0x01, YIELD=0x02, SLICE=0x04 等） |
| **Row 7** | 提交边界声明 | `* C-COMMIT ONLY (PSP PEND)` | 黄色警示 | **强制要求**：明确此事件仅代表内核 C 逻辑确认上下文移交并触发 PendSV |
| **Row 8** | 汇编隔离免责 | ` (NOT ARM REG RESTORE PROOF)` | 灰色警示 | **强制要求**：明确并不代表底层汇编已完成 R4-R11 与 PSP 寄存器物理恢复 |
| **Row 9** | 翻页引导 | `KEY1 -> Next Page [6/6]` | 白色 | 提示用户按 KEY1 循环切换至页面 0，总页数为 6 |

---

## 四、因果提交分类色彩规约 (Commit Class Color Palette)

为使用户在百毫秒内一眼识别调度行为模式，分类采用高对比度色彩：

- **`HIGHER_PREEMPT` (青色 CYAN)**: 高优先级任务抢占，常见于 Controller/KEY 唤醒打断低优先级任务。
- **`YIELD` (黄色 YELLOW)**: 任务主动让出 CPU (`rtos_yield`)。
- **`SLICE_ROTATION` (白色 WHITE)**: 同优先级时间片轮转。
- **`BLOCK_OR_WAKE` (洋红 MAGENTA)**: 任务阻塞等待 IPC/延时或唤醒事件。
- **`IDLE_TRANSITION` (绿色 GREEN)**: 进出空闲任务 (`Idle`)。
- **`EXIT` (红色 RED)**: 任务主动退出或终止。
- **`UNKNOWN` (灰色 GRAY)**: 无法归类的复合或未知调度事件，严格遵循不瞎推断原则。

---

## 五、刷新协同与内存/时间开销保障 (Performance Guarantees)

1. **刷新周期控制**：
   - 处于第 6 页时，刷新周期为 **250 ms**。
   - `Display_UpdateDue` 严格把关，避免高频 SPI3/DMA 占用总线。
2. **字符级增量渲染**：
   - 维护 `previous_trace[10][38]` 行缓冲。
   - 只有当字符串内容或颜色改变时才调用 `LCD_ShowString` 写入显存，极大地降低 SPI3 传输字节数。
3. **严格线程上下文执行**：
   - 所有快照提取、结构体解析、字符串转换只在 LCD Task（优先级 2）执行。
   - 严禁在 SysTick、PendSV 或硬件中断中绘图或格式化。
4. **有界快照读取**：
   - 每次只拉取最近 8 条记录 (`rtos_diag_trace_read_latest(recs, 8, &batch)`)，临界区耗时小于 2 µs，界面格式化时间小于 400 µs。
5. **零动态内存**：
   - 无 `malloc`/`free`，所有缓冲均为局部静态数组或栈上标量。
