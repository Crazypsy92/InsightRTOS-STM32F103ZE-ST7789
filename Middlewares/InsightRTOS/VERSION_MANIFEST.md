# InsightRTOS production snapshot

Upstream source project: InsightRTOS, CMake project VERSION 0.9.2.
Snapshot date: 2026-11-22 (Asia/Shanghai).
Stage: 9.2 (Explainable Scheduling + Mutex Deadlock Diagnostics).

Source directory mapping is preserved for kernel/, include/, arch/arm/cortex_m3/, lib/. No BSP/startup/linker/host tests/QEMU/test hooks are included in the production target. Test-conditioned code present in original production sources is byte-identical and compiled with RTOS_TESTING=0.

port/ and config/ are application-owned integration files, not upstream kernel modifications. Updating the snapshot requires review and SHA256 regeneration; no external absolute source paths or symlinks are required.

| File | SHA256 |
|---|---|
| `CMakeLists.txt` | `8815f04e35f13b4fdd978a8ef3bee6c9f8a339a10ce897310898fb27fa42b16c` |
| `arch/arm/cortex_m3/port.c` | `a55c746c73cba98fe9be555cc39905b5090798a9af2b401c51baf3f7f8dbfb6d` |
| `arch/arm/cortex_m3/port.h` | `2d1d5bdbdf4153ebd4286e36a14ece9abf5c974b72080c85baab8820ee522122` |
| `arch/arm/cortex_m3/portasm.S` | `bf86470b955425ce6bcea0a5985aaf13579eea4da4d09718a32ef80c8e352436` |
| `arch/arm/cortex_m3/systick.c` | `15de98d13af42807a709515f5e9b59f151b6e72bc4e68408e6d313d644093c18` |
| `arch/arm/cortex_m3/systick_config.c` | `28f24a3aa3d3acf63952bd3dee6152fd9209285bd53f0a4061b6a6005b51ff42` |
| `config/insight_project_config.cmake` | `18568a05832099d49f4733cd81f689b95cc3c280d91465ad36cc7f6359ec0a31` |
| `include/rtos.h` | `73ce7996ec05a9186a293a9556e26b6f3d779dc57c36dd1f0726d6a982761e13` |
| `include/rtos_assert.h` | `73cfc9f7b6ff0878313b60f01312c2a04ef99fa726f9b4a6f0cd826c40aa9c0c` |
| `include/rtos_config.h` | `d65d1efe3c059523a6c5d80f500baf451761959253f78283e98b5ff5bbecd483` |
| `include/rtos_critical.h` | `0bcff7b9843f57e8d3edf014722d309c7f184f63fc59cdf641e87a8617d16199` |
| `include/rtos_delay.h` | `38edb83fd7f291c0bcb510cb7f643cb518f5a3ec322ddd903a7c9ce040131b7a` |
| `include/rtos_diag.h` | `a50337e814fc45bfe2183757d0de3be43dcaaf655c3f57a0b406c234686bb122` |
| `include/rtos_mutex.h` | `39d8fbd73260938985ce068aef9b94a1931e53bf6ad2b595727ceb4284ee385b` |
| `include/rtos_queue.h` | `c5e5354cd58afb90734966948f385d519cd63f59faa4df146a51ad98a4aed989` |
| `include/rtos_sched.h` | `a09a7fbc0db9e1290781044d84b1dfe4589901c720a825792fbb374da83c59d4` |
| `include/rtos_semaphore.h` | `bb7c0943a3c0d9e4a306e1cc44dc0b8c59703170bae3f2480ebe8e9cb0fd06ca` |
| `include/rtos_task.h` | `8641b85e3a11fb53a29d4f3e7928883dc3ce30b18e17f21974baf2d3308e7ec9` |
| `include/rtos_tick.h` | `7cd3692831a1d10fa2f9883963b9bedf30def4999782ec61bd9f3b3485d5645f` |
| `include/rtos_trace.h` | `403f86fc00ed2ee445e6535705a0755660a3358b8a764f2b39c91636dd2e75e8` |
| `include/rtos_wait.h` | `cd55a435eb2a1f523dc64a2813a8d4581224156d3ff44e426acc1c71ecb2ddf3` |
| `kernel/critical.c` | `c2e047841cf823ad7ae2f89026bea21395bdad71437b1bbd5df7b62a57fc81ca` |
| `kernel/deadlock.c` | `92bcc82d05cc4cd01ffb7db01bf996ca8899765cd922cf3ebaaba4f002da6394` |
| `kernel/delay.c` | `1e723c25591de85e06b169f26bf8cb943d21c8f800047fed7d16a1f0e82fa851` |
| `kernel/delay_queue.c` | `85344b7d6c07bff1c19f08ba87ad43dfbe90132363d4969fcd9fd4ceeb9ec4dc` |
| `kernel/delay_queue.h` | `14e0df077f223c61ab91558ae9425cb36d4347ac17d2e8174de6937cc35e3fc8` |
| `kernel/kernel_internal.h` | `59c46df084abfcf668193b3072c904070fa70404677bd05e74e0f6e7553b652a` |
| `kernel/memory.h` | `971c4d5d4f46308918eeb10b08d2baba01d3a77aad667eb0f761381e73985f4c` |
| `kernel/mutex.c` | `a9d40c7c8205cf92574c44546ecd19466e96bbccfe9863ad23e0466d94d94d6e` |
| `kernel/mutex_internal.h` | `f1234f79f534d8b001bef302e877c2e0a798f581d1dc8055a2f8b7a7e9f00b93` |
| `kernel/priority.c` | `132ae2f49a0c37b40af897e426c142b4ed4fc2a9fd2f142d0ef6112231cefa4f` |
| `kernel/priority_graph.c` | `2584082d8ab503bd8d663e415fb51ff271e6e9e2afecd9ab87e40168fd04a8c1` |
| `kernel/priority_internal.h` | `93aecc8e18564f4e95885f0e316a23c631f66c5d25381db45403da2caf57ea87` |
| `kernel/queue.c` | `1393c01533900f383e11a84e143e982d5554133ae8755e6f6e35addd8f8de805` |
| `kernel/queue_internal.h` | `c1a5b2610263b62ea2a95324ef147a652157839bab79dc2380bc91e28e6c153e` |
| `kernel/ready_queue.c` | `84c22dab517bf6a1727c3b27503dacd6f296cae60800fc5ca83f32e18a7c9170` |
| `kernel/ready_queue.h` | `7526ed35d7def61d833fd0cbfc5b7de69d0d61ed53906a5bcdb6408791f7673c` |
| `kernel/scheduler.c` | `32a7bff8bbca16b062367b495ebcbabfa145e7c721e81997893ebc7e86a20515` |
| `kernel/semaphore.c` | `04c4deb9f689559259c4119973111076204ea5de513854c054bb915f59139f55` |
| `kernel/semaphore_internal.h` | `ad80746384910227550b095629a31c96e939f85eed954fd696c55fccbf9d2ad8` |
| `kernel/task.c` | `64c917fe6f8811936a3a3a37c77a5dea358595567b75ec86fb9f362c12de12ee` |
| `kernel/task_lifecycle_internal.h` | `ab096aff6cf5cbfdc6957f0471731f400c0389d662ab0d91dbd4a96c97ac57b0` |
| `kernel/tick.c` | `f021b571a8d8c9d84d2eeb4b1f3fe6c697a7edb52819525adb6294c1a3d7726e` |
| `kernel/trace.c` | `975c2c1cfe5b1935092744e95ae02bb5d57e343d8f0a0803584a1dc6e47ca1db` |
| `kernel/trace_internal.h` | `b4c924899c94cba39c6d5453f4b758ae51af5ac7ff67c740ae974b3c1f16e200` |
| `kernel/wait.c` | `308df2258f7c992803939ab44b1c793e74540d98314f673a3ceab21b2d59ff7c` |
| `kernel/wait_internal.h` | `5037374eb53f1d01a6b3f3b58ada26e504828442208b3ff7699ab3e1d4a9f9e1` |
| `lib/memcpy.c` | `f65eec6d68dd38459fcd72fda11f1a32b35711fb37e67e5cdbf5511643d3979e` |
| `port/insight_board_port.c` | `08de4696186e4a6b2a57d78c9b3af2568c453d9df4ba1b27975c47f71c09d3e0` |
| `port/insight_board_port.h` | `2a2c82f73b895c0cc391a8042dde72c7aee4d14bd7ff7de1d4e672d00e2f8892` |
| `port/insight_timebase.c` | `13dae28d90ebb23b7210dca56a8e5cd967dba841037321c681626d852c182398` |
