# C3 五页面、输入及 LED

**PASS（用户目视确认）**。首次板载 RESET 后，用 GDB 捕获入口并验证调度，随后释放目标。用户按欢迎页、颜色、图形、文字、小球动画、Monitor 动态数据、短按、长按和 LED 清单观察，回复「正常」。

板上采样见欢迎计数从 0 到 1，自动页变更计数到 3，动画页刷新推进；重连时 Monitor 页已出现并累计刷新 312 次。用户未逐项复述最初操作顺序。重连时板上累计 6 次短按、1 次长按，ISR、KEY、Controller、LCD 各累计 7 个事件。随后 60 秒窗口内独立记录另一次长按，LCD 仍在 Monitor，LED 对应 GPIO 电平翻转，用户回复「正常」。

早期 SWD 短暂失联期间的单次过渡没有完整调试采样。用户观察记录及重连后计数分别见 `user-display-confirmation.json`、`resumed-visual-check-result.json` 和 `stability60-result.json`。
