# 软件构建和回归

唯一生产入口app/stm32f103ze_st7789，DISPLAY_RTOS_Debug/O2生成insightrtos_display，新导出名insightrtos_zet6_c1_{debug,o2}。原Debug/O2裸机继续四页，两种最终BIN与历史冻结裸机逐字节一致。RTOS_Debug/O2双任务入口未改。B1/B1.3/B1.4包、证据和公共内核/TCB/ARM移植保持SHA，400个baseline保护文件核验通过，原四绘制函数和动画更新逐函数字节一致。

## 构建命令（仓库根）

```sh
cmake --preset DISPLAY_RTOS_Debug -S app/stm32f103ze_st7789 -B /tmp/c1-debug
cmake --build /tmp/c1-debug --clean-first
cmake --preset DISPLAY_RTOS_O2 -S app/stm32f103ze_st7789 -B /tmp/c1-o2
cmake --build /tmp/c1-o2 --clean-first
cmake -S tests/stage8_5_c1 -B /tmp/c1-host-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/c1-host-debug
ctest --test-dir /tmp/c1-host-debug -V
cmake -S tests/stage8_5_c1 -B /tmp/c1-host-o2 -DCMAKE_BUILD_TYPE=RelWithDebInfo '-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -g'
cmake --build /tmp/c1-host-o2
ctest --test-dir /tmp/c1-host-o2 -V
python3 tests/stage8_5_c1/dma_budget_audit.py evidence/stage8_5_c1/dma-budget.csv
python3 scripts/verify_stage8_5_c1.py
```

严格-Wall/-Wextra/-Werror、Cortex-M3/Thumb/soft ABI，内核额外严格选项保持；Debug -O0 -g3、O2 -O2 -g -DNDEBUG。RTOS_ASSERT不受NDEBUG影响，Host测试用明确CHECK而非被移除的标准assert。原始日志保留早期测试配置失败及修正过程，末尾两配置均7/7通过，未删用例或降低断言。最终ARM两种均clean-first构建通过。

## 原完整入口

重新运行未修改scripts/test_stage8_4a.sh，使用/tmp单独解包的QEMU10.2.1及所需共享库，没有系统安装/硬件操作。旧CMake cache工具路径以/tmp工具链接恢复，未改测试源码/ID/期望。六组497/288/288/504/289/289，总2155次执行；原128项负面链接检查通过。final-regression.log记录退出0，regression-ids.json保留全ID，regression-evidence.tar.gz保留本次完整原始产物；旧evidence/stage8_4a在归档后原样恢复，工具恢复过程未改变公共内核。

## 应用7项×Debug/O2

五页+欢迎+100短按2长按；初始化失败2次后成功；永久初始化故障最多3次；慢LCD队满背压无损；关键溢出显式失败；UI数据真实性/状态变化/局部刷新/错误缓存/布局；原LCD协议+实际总线状态机100故障位置释放恢复。真实C任务/内核IPC/原MultiButton与渲染函数参与，硬件以fake GPIO/Port/ucontext替代。模拟绘制期间高优先级切换后继续正确执行；不能替代STM32抢占或LCD实测。

新增ARM负面链接每配置6项：SRAM重叠、向量origin偏移、MSP缩小、重复SVC、禁止malloc、弱上下文落空，共12项预期拒绝。fixture、精确命令及诊断在static-*/negative。收集脚本collect_stage8_5_c1.py需要/tmp/c1-*构建目录；verify脚本只读冻结包，无临时构建依赖。

完整工具版本见tool_versions.txt；每个C/ASM源实际参数见static-*/compile_commands.json。独立ELF PHDR/HEX解析与BIN逐字节一致，不仅依赖objcopy退出0；无未解析/重复异常/semihost/动态分配/测试生产入口。
