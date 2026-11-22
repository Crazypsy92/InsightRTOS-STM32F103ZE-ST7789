# C2 实物按键验收

PASS（用户确认+实际计数）。用户执行状态页短按返回颜色、长按不翻页并翻转LED、4次间隔至少0.6秒的连续短按回状态页；回复“对的，你说的我都测试了，没有问题”。实际新增5次短按、2次长按，用户进一步确认“长按了两次”，不是诊断脚本擅自忽略重复事件。

测试后ISR/KEY/Controller/LCD累计事件均13，5个Queue深度均0、关键溢出0、最终page=4。LED及无花屏属于用户目视结果，非代理自行目视。对快于MultiButton双击窗口的输入没有新增业务语义，不把未绑定DOUBLE_CLICK说成丢键；本轮未做硬件抖动波形注入。

USER CONFIRMATION REQUIRED：已完成上述人工确认；源回复及计数在user-key-confirmation.json/key-verification.json/pre-key/post-key-sample.json。

窗口约第7分钟又观测到4次短按（4→0→1→2→3），用户确认“是，我又按了4次”，记录为额外人工翻页，不记为误触发。期间未RESET，连续窗口未重启。
