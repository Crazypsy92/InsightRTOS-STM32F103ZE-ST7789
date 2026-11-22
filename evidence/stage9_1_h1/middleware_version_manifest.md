# Stage 9.1-H1 Middleware Version Manifest

## 一、概述 (Overview)

本清单记录 InsightRTOS Stage 9.1 经过 Host/QEMU 验收的 Explainable Scheduling 生产内核同步至 STM32F103ZET6 硬件工程 (`app/stm32f103ze_st7789/Middlewares/InsightRTOS`) 的版本基线与校验数据。

- **内核版本**: InsightRTOS v0.9.1 (Content-pinned snapshot)
- **同步日期**: 2026-11-22
- **目标工程**: `app/stm32f103ze_st7789`
- **构建环境**: `arm-none-eabi-gcc` 14.2.1 / C11
- **测试隔离**: 生产工程仅同步 `include/`, `kernel/`, `arch/arm/cortex_m3/` 生产源文件，零 Host mock / QEMU 测试脚手架泄漏，`RTOS_TESTING=0`。

---

## 二、同步文件校验列表 (File Verification Hashes)

所有 44 个同步文件与根目录生产代码 SHA256 严格一致：

| 模块 | 文件路径 | SHA256 校验码 | 状态 |
|---|---|---|---|
| **Trace & Diag** | `include/rtos_trace.h` | `403f86fc00ed2ee445e6535705a0755660a3358b8a764f2b39c91636dd2e75e8` | MATCH |
| **Trace & Diag** | `include/rtos_diag.h` | `983fa10d8ef3517945575205103cde3ccdd92d7320e4d747c9d674d55dd874d9` | MATCH |
| **Trace & Diag** | `kernel/trace.c` | `975c2c1cfe5b1935092744e95ae02bb5d57e343d8f0a0803584a1dc6e47ca1db` | MATCH |
| **Trace & Diag** | `kernel/trace_internal.h` | `b4c924899c94cba39c6d5453f4b758ae51af5ac7ff67c740ae974b3c1f16e200` | MATCH |
| **Core Kernel** | `kernel/task.c` | `43b16c3d1de5e5d6bba8ca5d581b23e0b6bf40c09e158fbcfac1f163a2700ee2` | MATCH |
| **Core Kernel** | `kernel/delay.c` | `1e723c25591de85e06b169f26bf8cb943d21c8f800047fed7d16a1f0e82fa851` | MATCH |
| **Core Kernel** | `kernel/scheduler.c` | `32a7bff8bbca16b062367b495ebcbabfa145e7c721e81997893ebc7e86a20515` | MATCH |
| **Core Kernel** | `kernel/critical.c` | `c2e047841cf823ad7ae2f89026bea21395bdad71437b1bbd5df7b62a57fc81ca` | MATCH |
| **Core Kernel** | `kernel/tick.c` | `f021b571a8d8c9d84d2eeb4b1f3fe6c697a7edb52819525adb6294c1a3d7726e` | MATCH |
| **Core Kernel** | `kernel/queue.c` | `1393c01533900f383e11a84e143e982d5554133ae8755e6f6e35addd8f8de805` | MATCH |
| **Core Kernel** | `kernel/ready_queue.c` | `84c22dab517bf6a1727c3b27503dacd6f296cae60800fc5ca83f32e18a7c9170` | MATCH |
| **Core Kernel** | `kernel/semaphore.c` | `04c4deb9f689559259c4119973111076204ea5de513854c054bb915f59139f55` | MATCH |
| **Core Kernel** | `kernel/mutex.c` | `a9d40c7c8205cf92574c44546ecd19466e96bbccfe9863ad23e0466d94d94d6e` | MATCH |
| **Core Kernel** | `kernel/priority.c` | `132ae2f49a0c37b40af897e426c142b4ed4fc2a9fd2f142d0ef6112231cefa4f` | MATCH |
| **Core Kernel** | `kernel/priority_graph.c` | `2584082d8ab503bd8d663e415fb51ff271e6e9e2afecd9ab87e40168fd04a8c1` | MATCH |
| **Core Kernel** | `kernel/wait.c` | `308df2258f7c992803939ab44b1c793e74540d98314f673a3ceab21b2d59ff7c` | MATCH |
| **Headers** | `include/rtos.h` | `73ce7996ec05a9186a293a9556e26b6f3d779dc57c36dd1f0726d6a982761e13` | MATCH |
| **Headers** | `include/rtos_task.h` | `8641b85e3a11fb53a29d4f3e7928883dc3ce30b18e17f21974baf2d3308e7ec9` | MATCH |
| **Headers** | `include/rtos_sched.h` | `a09a7fbc0db9e1290781044d84b1dfe4589901c720a825792fbb374da83c59d4` | MATCH |
| **Headers** | `include/rtos_tick.h` | `7cd3692831a1d10fa2f9883963b9bedf30def4999782ec61bd9f3b3485d5645f` | MATCH |
| **Headers** | `include/rtos_delay.h` | `38edb83fd7f291c0bcb510cb7f643cb518f5a3ec322ddd903a7c9ce040131b7a` | MATCH |
| **Headers** | `include/rtos_queue.h` | `c5e5354cd58afb90734966948f385d519cd63f59faa4df146a51ad98a4aed989` | MATCH |
| **Headers** | `include/rtos_semaphore.h` | `bb7c0943a3c0d9e4a306e1cc44dc0b8c59703170bae3f2480ebe8e9cb0fd06ca` | MATCH |
| **Headers** | `include/rtos_mutex.h` | `39d8fbd73260938985ce068aef9b94a1931e53bf6ad2b595727ceb4284ee385b` | MATCH |
| **Headers** | `include/rtos_critical.h` | `0bcff7b9843f57e8d3edf014722d309c7f184f63fc59cdf641e87a8617d16199` | MATCH |
| **Headers** | `include/rtos_assert.h` | `73cfc9f7b6ff0878313b60f01312c2a04ef99fa726f9b4a6f0cd826c40aa9c0c` | MATCH |
| **Headers** | `include/rtos_config.h` | `d65d1efe3c059523a6c5d80f500baf451761959253f78283e98b5ff5bbecd483` | MATCH |
| **Arch Cortex-M3** | `arch/arm/cortex_m3/port.c` | `9478f7e3d1d176378e90e755250cbe44a30559e3549ee14a060ea8d338ceebda` | MATCH |
| **Arch Cortex-M3** | `arch/arm/cortex_m3/port.h` | `0c598bcbf42e20556fc19dfceba5f616e0b721869804b4d67e5a88c7f9ea267d` | MATCH |
| **Arch Cortex-M3** | `arch/arm/cortex_m3/portasm.S` | `a902ba98460dd8d103ecf459ba40733802ba2c2626e25c8629227181cda41a94` | MATCH |
| **Arch Cortex-M3** | `arch/arm/cortex_m3/systick_config.c` | `10f277ca6b86e095dbbcab70034a78128eebe01e74ebf4ff7e79c6ce93839958` | MATCH |
| **App Port/Config** | `config/insight_project_config.cmake` | `39f37cbb11e51b32943261763f9ebba06d628da6fc353e6b72a44415fc4b3ae3` | APP OWNED |
| **App Port/Config** | `port/rtos_port_hardware.c` | `37803e7e224e731da29fc6ef56a31c5db67ec4310d540e1b69784361eb4d35cb` | APP OWNED |

---

## 三、不变性保证 (Invariance Guarantees)

1. **TCB 布局零变动**：TCB 结构体大小严格保持 72 字节，字段偏移 0 (`sp`)、4 (`id`)、8 (`name`)、20 (`state`)、21 (`base_priority`) 完全一致。
2. **汇编零修改**：`portasm.S` 原封不动，`SVC_Handler` 与 `PendSV_Handler` 汇编逻辑、寄存器入栈出栈顺序、`rtos_port_pendsv_c()` 调用约定完全冻结。
3. **SPI3/DMA 修复完全保留**：Stage 8.5-C3 中验证修复的动态 SCK 周期超时预算公式与双阶段轮询/中断恢复逻辑完全保留在 BSP 中。
