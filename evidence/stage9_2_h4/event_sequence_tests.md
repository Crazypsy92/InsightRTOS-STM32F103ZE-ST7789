# Stage 9.2-H4: 事件序列与闭环 ACK 关联测试报告

## 一、测试目标与设计规范

在 Stage 9.2-H3 中，`g_event_tracker` 为单一全局变量，各任务在处理不同按键事件时直接覆写 stage 字段，缺乏事件 ID 的跨队列透传与 ACK 闭环匹配。
在 Stage 9.2-H4 中，针对此问题进行了完整的闭环设计：
1. **唯一单调事件 ID**：
   - 在 `Display_KeyFromISR` 中通过单调自增生成唯一 `id`（从 1 开始）。
   - 事件结构 `display_key_t { id, event, at_ms }`、`display_command_t { sequence, id, event, at_ms }`、`display_ack_t { sequence, id, ... }` 形成全链路 ID 透传。
2. **有界历史事件表 (`DISPLAY_EVENT_HISTORY = 8U`)**：
   - 环形缓冲记录最近 8 个事件的完整状态生命周期：
     `EVENT_STAGE_IDENTIFIED` (ISR) → `EVENT_STAGE_KEY` (KEY) → `EVENT_STAGE_CTRL` (CTRL) → `EVENT_STAGE_LCD` (LCD) → `EVENT_STAGE_ACKED` (CTRL 验证)。
   - 仅当匹配指定 `id` 时状态推进，杜绝跨事件互相覆盖与阶段回退。
3. **闭环确认与错误判定**：
   - 仅当 Controller 收到匹配的 `ack.id` 且 `ack.error == 0` 时，才推进至 `EVENT_STAGE_ACKED` 并计入 `success_count`。
   - 若 `ack.error != 0`，推进至 `EVENT_STAGE_FAILED` 并计入 `failure_count`。
   - 周期性重绘 (`KEY_EVENT_NONE`, `sequence != 0`, `id == 0`) 不参与事件统计。
   - 队列满反压 (`status == RTOS_ERR_FULL`) 增加 `retry_count`，不计入丢弃；真实丢弃 (`key_overflows`) 增加 `drop_count`。

---

## 二、测试用例执行结果 (9/9 PASS)

测试套件源码：`tests/stage9_2_h4/test_stage9_2_h4.c`
执行脚本：`tests/stage9_2_h4/run_tests.sh`

```text
====================================================
InsightRTOS Stage 9.2-H4 Host Test Suite
====================================================
[TEST 1] Font glyph bits and layout verification... PASS (143 glyphs, all H4 codepoints verified)
[TEST 2] UTF-8 / ASCII mixed rendering and bounds... PASS (ASCII 8px advance, CJK 16px advance)
[TEST 3] 15-Row UI layout and 240x240 screen boundary (Zero Trace text)... PASS (All 15 rows <= 240px, NO Trace text present)
[TEST 4] MultiButton events, task cycling and detail toggle... PASS (Single click, task cycle, double click, detail toggle, long press)
[TEST 5] Event ID correlation & closed-loop ACK tracking... PASS (Matching ID, ACK error handling, periodic redraw separation, 2s retention)
[TEST 6] Bounded history (8 slots), queue backpressure & drop count... PASS (Ring buffer lookup, backpressure retry vs drop distinction)
[TEST 7] LED active-low polarity and toggle... PASS (PC13 active-low polarity & toggle verified)
[TEST 8] Deadlock diagnostics display states... PASS (无 / 存在 / 异常 verified)
[TEST 9] Kernel Trace APIs & data structures 100% preserved... PASS (rtos_diag_trace_info_t intact, record size 24 B)
====================================================
ALL STAGE 9.2-H4 HOST TESTS PASSED (9/9)
====================================================
```

---

## 三、各测试用例详述

### 1. [TEST 1] 字模与编码验证
- 验证字模总数为 143。
- 验证 H4 新增中文字符 `'成'` (0x6210)、`'功'` (0x529F)、`'失'` (0x5931)、`'败'` (0x8D25)、`'重'` (0x91CD)、`'试'` (0x8BD5)、`'次'` (0x6B21) 及全部既有 136 个字符均可正常解码并返回 `LCD_OK`。

### 2. [TEST 2] 中英文混排与步进
- 验证 ASCII 字符宽度为 8px，中文字符宽度为 16px。
- 验证标题行、按键行、结果行各混排字符串步进计算完全正确。

### 3. [TEST 3] 15 行布局边界与无 Trace 验证
- 穷举 4 组完整状态组合（正常运行、异常死锁、详情展开、长按LED等）。
- 断言所有 15 行字符串像素宽度严格 `<= 240 px`，高度 `<= 240 px`。
- 逐行校验断言 `strstr(row, "Trace:") == NULL`，确认屏幕上无任何 Trace 残留。

### 4. [TEST 4] 按键事件与任务切换
- 模拟按键单击（50ms 低电平，400ms 高电平），触发 `BTN_SINGLE_CLICK`，选中任务循环切换（KEY(0) → CTRL(1) → LCD(2) → MON(3) → KEY(0)）。
- 模拟双击，触发 `BTN_DOUBLE_CLICK`，翻转详情显示模式。
- 模拟长按（>1000ms），触发 `BTN_LONG_PRESS_START`，翻转 LED 输出极性。

### 5. [TEST 5] 事件 ID 透传与 ACK 闭环追踪
- **正常事件流**：
  1. ISR 产生事件 ID 1，写入槽位 `1 % 8`，标记 `EVENT_STAGE_IDENTIFIED`。
  2. KEY Task 校验 `key.id == 1`，将槽位状态推进为 `EVENT_STAGE_KEY`。
  3. Controller Task 校验 `pending_key.id == 1`，将槽位状态推进为 `EVENT_STAGE_CTRL`，并打包发送包含 `id == 1` 的 `display_command_t`。
  4. LCD Task 执行渲染，将槽位状态推进为 `EVENT_STAGE_LCD`，并返回包含 `id == 1` 的 `display_ack_t`。
  5. Controller Task 收到匹配 ACK，校验 `ack.error == 0`，推进至 `EVENT_STAGE_ACKED`，更新 `confirmed_id = 1`，`success_count` 自增为 1。
- **异常事件流**：
  - 模拟 LCD 传输错误（`ack.error = -1`），校验槽位状态进入 `EVENT_STAGE_FAILED`，`failure_count` 自增为 1，`success_count` 不自增。
- **周期重绘隔离**：
  - 发送周期性定时刷新命令（`KEY_EVENT_NONE`, `sequence = 3`, `id = 0`），收到 ACK 后确认 `success_count` 与 `failure_count` 均不变化。
- **2 秒留痕窗口**：
  - 完成时刻 `< 2000 ms` 显示 `链路: 识别>KEY>CTRL>LCD`；
  - 超出 `>= 2000 ms` 平滑恢复为 `链路: 待机`。

### 6. [TEST 6] 有界历史环形缓冲与反压区分
- 连续注入 10 个事件 ID（1～10）。
- 校验槽位 1 与 2 被 ID 9 与 10 正常覆盖重用，查找 ID 1 与 2 返回 NULL。
- 校验 ID 3～10 均可正确查询且状态完整。
- 校验队列满反压仅自增 `retry_count`，不计入 `drop_count`。

### 7. [TEST 7] PC13 LED 极性测试
- 校验 PC13 低电平点亮（`0 = 开`），高电平熄灭（`1 = 关`）。

### 8. [TEST 8] 死锁诊断状态格式化
- 校验正常时显示 `无`，检出死锁时显示 `存在`，发生异常时显示 `异常`。

### 9. [TEST 9] 内核 Trace 结构与 API 完整性
- 校验 `rtos_diag_trace_info_t` 数据结构与字段完好。
- 校验 `sizeof(rtos_trace_record_t) == 24U` 保持严格 ABI。
