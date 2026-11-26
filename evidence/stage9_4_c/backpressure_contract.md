# Stage 9.4-C 门控与重试规约

## 试验口径

DISPLAY_RTOS_O2_Backpressure 继承综合诊断和压力档位。测试宏只在 LCD 首次初始化并发送 ready ACK 后启用一次 150 Tick 的线程阻塞。此时未开始消费命令，不在 SPI/DMA 事务或锁持有区间。与门控同时启动既有 TIM2 测试事件路径的独立模式，每 10 ms 调用一次真实 Display_KeyFromISR，共 48 次，自清请求。没有外部工具持续发指令，主机断开不会延长门控或无限注入。正常 LCD 循环与原命令队列容量保持。

测试代码在 Controller 的真实 App_IPC_SendCommand 返回后记录结果，FULL 路径同时记录深度和未提交的 sequence，成功路径核对 sequence 已提交。同一事件反复 FULL 必须使用同一候选 sequence，随后只有一次成功。LCD 记录 Display_PagesExecute 返回后的实际序列。Controller 校验 ACK 连续并调用 App_Events_RecordAck 后才记录 ACK；冻结前板上检查序列重复，ACK 错误码保留到冻结后由主机核验，冻结后主机双读比对，再逐项用 ELF DWARF 解析的结构验证。不直接写队列、篡改返回值、生成模拟 ACK 或在 ISR 内记录耗时日志。

## 守恒关系

- 在途缓存 pending_key 仅在没有缓存时从控制器队列获取。此次失败分支退出当轮发送循环，保留缓存，等待下个 Tick。FULL 每次增加一次 command_overflows 和账本 retry_count，没有生成新输入。
- 成功提交才推进命令序列。LCD 执行序列和 ACK 序列必须与该事件的唯一已提交值一致。事件 ID 和计量 Job ID 不共用语义。
- 输入尝试 48 = 48 个成功入队 + 0 个原始入口拒绝。入队 48 = ACK 成功 48 + ACK 失败 0 + 待 ACK 0。下游 FULL 223 = command_overflows 223 = retry_count 223 = 全部事件的 FULL 次数之和。
- 最后完成的事件 48 有原始事件时间和配对 ACK，延迟 4,390 ms。测试钩子的 Tick 记录用于下游命令和 ACK 顺序，不能替代已覆盖事件的 TIM2 端到端时间戳。没有把覆盖或暂时 FULL 算成丢弃。

## 复现入口

从源码运行 scripts/test/run_host.sh 和 scripts/test/run_rt_host.sh。配置并构建 DISPLAY_RTOS_O2_Backpressure、DISPLAY_RTOS_Debug_Backpressure、DISPLAY_RTOS_O2_Integrated。用既有 ELF/BIN/HEX 校验工具生成 firmware-audit.json。tests/hardware/run_timing.sh 的 Backpressure 模式使用独立校验过的授权区间旧镜像，并从 build/Stage94A_Frozen_Production 读取上轮正式镜像用于恢复。tests/hardware/read_backpressure.py 读取测试结构、Monitor 的版本化综合帧和健康快照。所有板级前检、擦除、独立读回和复位都有日志。
