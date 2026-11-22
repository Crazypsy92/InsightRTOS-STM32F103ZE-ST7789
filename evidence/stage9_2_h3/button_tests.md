# Stage 9.2-H3: 按键状态机、消抖与事件交互测试报告 (Button Tests)

## 一、MultiButton 状态机与事件互斥矩阵

测试套件：`tests/stage9_2_h3/test_stage9_2_h3.c`（结合 Host 自动化与硬件 RAM 采样）。

| 测试用例 ID | 触发动作 | 时序特征 | 产生事件 | 互斥性检验 | 结果 |
| :---: | :--- | :--- | :--- | :--- | :---: |
| **BTN-01** | 单击短按 | 按下 50ms，释放 > 350ms | `BTN_SINGLE_CLICK` | `s_double_clicks == 0`, `s_long_starts == 0` | **PASS** |
| **BTN-02** | 双击快速连按 | 按下 50ms, 释放 50ms, 按下 50ms, 释放 > 350ms | `BTN_DOUBLE_CLICK` | `s_single_clicks == 0`, `s_long_starts == 0` | **PASS** |
| **BTN-03** | 长按触发 | 按下 > 1000ms（240 节拍） | `BTN_LONG_PRESS_START` | `s_single_clicks == 0`, `s_double_clicks == 0` | **PASS** |
| **BTN-04** | 长按释放 | 长按后释放引脚回到高电平 | 无重复触发 | `s_long_starts == 1`（严格触发一次） | **PASS** |
| **BTN-05** | 高频毛刺抖动 | 20 次 1-tick (5ms) 快速电平毛刺 | 无事件产生 | `s_single_clicks == 0`（3节拍消抖滤除） | **PASS** |

---

## 二、功能交互与状态机响应

### 1. 单击切换选中任务
- 任务序列：`KEY (0)` → `CTRL (1)` → `LCD (2)` → `MON (3)` → `KEY (0)`。
- Row 04～07 行前标 `> ` 高亮跟随，Row 08 实时更新对应任务职责说明：
  - `选中: KEY 接收按键事件`
  - `选中: CTRL 处理事件/控制LED`
  - `选中: LCD 更新中文屏幕`
  - `选中: MON 状态错误监测`
- 自动化测试与硬件显存验证均符合预期。

### 2. 双击切换任务详情模式
- 详情模式 `s_detail_mode`:
  - 初始状态：`0`（Row 12 为空白黑行）。
  - 双击后：`1`（Row 12 显示 `详情: 运行:<runs> 代数:<gen>`，高亮黄色）。
  - 再次双击：`0`（Row 12 恢复空白黑行）。

### 3. 长按翻转 PC13 LED 输出
- 物理极性：PC13 为推挽输出，低电平点亮（`ODR=0 -> 开`），高电平熄灭（`ODR=1 -> 关`）。
- 控制流程：长按由 Controller 任务截获并执行 `KEY_ToggleLED()`，无独立“LED Task”。
- 实机屏幕 Row 01 动态显示 `LED: 开` 或 `LED: 关`，与物理 `GPIOC->ODR` 寄存器状态完全一致。

---

## 三、物理确认状态（遵守安全与诚信契约）

由于当前为远程自动化测试环境，物理硬件未连接机械按压治具与肉眼摄像机：
- 软件与 DAP RAM 级验证：**PASS**（代码、逻辑、状态机、显存比对全部通过）。
- 人手真实触觉与人眼直接目视：**USER CONFIRMATION PENDING**（标记待用户实物体验确认，不捏造已目测）。
