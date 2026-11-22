# C2 SPI/DMA 运行

PASS（本次五页实际路径未观察错误），长块风险仍OPEN。连续1805.013秒/362次实际SWD样本：初始化错误、应用传输错误、LL恢复次数、LL last_error均为0。末点逻辑页面刷新=48275，状态页更新=1593；期间包含状态页和动画/人工翻页，并非单一页面测试。

SPI3 BR=/2、PCLK1=36MHz得到配置目标18MHz，实际示波器SCK频率NOT MEASURED（peripheral-runtime.json）。DMA2原TX_CH2/RX_CH1、同步等待和错误恢复均未修改，未关闭超时、忽略错误或绕过状态机；没有新烧录/内核修复。

本轮没有65535B长块压力测试，C1已确认的PCLK/CPU预算缺陷仍OPEN；不能从当前<=480B行/字模路径正常推断全部DMA路径通过。原RXNE drain永久异常风险也未修复。
