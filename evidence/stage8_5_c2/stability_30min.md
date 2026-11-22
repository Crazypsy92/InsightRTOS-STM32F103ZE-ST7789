# C2 连续30分钟真机验收

PASS。严格窗口1805.013秒（至少1800秒），起点UTC 2026-11-20T14:36:31Z，末点UTC 2026-11-20T15:06:36Z，362个约5秒间隔实际样本。见stability30-samples.jsonl及stability30-result.json。

窗口内没有调试halt/reset/load/寄存器注入，仅在运行状态SWD读取数据；用户进行了已确认的短按、长按及额外翻页，不重启窗口。Controller/LCD/Monitor持续推进，KEY输入得到处理，Idle有独立自由运行直接证据。Tick增量180506、TIM2增量1805057，实测平均100.00262Hz/1000.02451Hz。

全部样本guard完整、关键溢出0、SPI/DMA错误及恢复0、assert_line/CFSR/HFSR为0。未观察到异常停机或计数停滞；无生产HardFault总计数接口，不能把未观察到故障伪称完整硬件故障累计计数。

LCD观感来自用户在起点和末尾的目视确认，未拍摄全程视频；不能声称代理全程自行目视。30分钟后才halt进行栈水位快照，见post-stability.json，随后resume并进行用户板载RESET。
