# InsightRTOS production snapshot

Upstream source project: InsightRTOS, CMake project VERSION 0.7.5.
Snapshot date: 2026-11-19 (Asia/Shanghai).
Git commit: UNAVAILABLE — the supplied source tree has an empty/nonfunctional .git view; git rev-parse HEAD fails. No commit is fabricated. This is a content-pinned source snapshot, not a Git submodule.

Source directory mapping is preserved for kernel/, include/, arch/arm/cortex_m3/, lib/. No BSP/startup/linker/host tests/QEMU/test hooks are included in the production target. Test-conditioned code present in original production sources is byte-identical and compiled with RTOS_TESTING=0.

port/ and config/ are application-owned integration files, not upstream kernel modifications. Updating the snapshot requires review and SHA256 regeneration; no external absolute source paths or symlinks are required.

| File | SHA256 |
|---|---|
| `kernel/critical.c` | `c2e047841cf823ad7ae2f89026bea21395bdad71437b1bbd5df7b62a57fc81ca` |
| `kernel/delay.c` | `569830b7422b1f8e82782c67e137903f2a12f9bb9a756b13b5a5afd49abc54c3` |
| `kernel/delay_queue.c` | `85344b7d6c07bff1c19f08ba87ad43dfbe90132363d4969fcd9fd4ceeb9ec4dc` |
| `kernel/delay_queue.h` | `14e0df077f223c61ab91558ae9425cb36d4347ac17d2e8174de6937cc35e3fc8` |
| `kernel/kernel_internal.h` | `6fdf1bd2dfe75e79b824e43b9b7e5f129ab17175eb9851986c7a9702d59d0e09` |
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
| `kernel/scheduler.c` | `03c2431c26bbeb3c53d72e25acfce7ebc24bba4fde00a7a9e7dc38cf50135cdb` |
| `kernel/semaphore.c` | `04c4deb9f689559259c4119973111076204ea5de513854c054bb915f59139f55` |
| `kernel/semaphore_internal.h` | `ad80746384910227550b095629a31c96e939f85eed954fd696c55fccbf9d2ad8` |
| `kernel/task.c` | `5ed6a475e7683af5d09611c6600e75d2047fa0b0ec64e647d7e6708113151ba4` |
| `kernel/task_lifecycle_internal.h` | `ab096aff6cf5cbfdc6957f0471731f400c0389d662ab0d91dbd4a96c97ac57b0` |
| `kernel/tick.c` | `f021b571a8d8c9d84d2eeb4b1f3fe6c697a7edb52819525adb6294c1a3d7726e` |
| `kernel/trace.c` | `f6ddf46fad33ba3dcf7076f087de34095b65c5f74700130e7d913b27b727aa9e` |
| `kernel/trace_internal.h` | `134670b2d53e286e7fca0a3ed9feaa7888c5a2fa4ff6b4295c832d7849584212` |
| `kernel/wait.c` | `308df2258f7c992803939ab44b1c793e74540d98314f673a3ceab21b2d59ff7c` |
| `kernel/wait_internal.h` | `5037374eb53f1d01a6b3f3b58ada26e504828442208b3ff7699ab3e1d4a9f9e1` |
| `include/rtos.h` | `73ce7996ec05a9186a293a9556e26b6f3d779dc57c36dd1f0726d6a982761e13` |
| `include/rtos_assert.h` | `73cfc9f7b6ff0878313b60f01312c2a04ef99fa726f9b4a6f0cd826c40aa9c0c` |
| `include/rtos_config.h` | `d65d1efe3c059523a6c5d80f500baf451761959253f78283e98b5ff5bbecd483` |
| `include/rtos_critical.h` | `0bcff7b9843f57e8d3edf014722d309c7f184f63fc59cdf641e87a8617d16199` |
| `include/rtos_delay.h` | `38edb83fd7f291c0bcb510cb7f643cb518f5a3ec322ddd903a7c9ce040131b7a` |
| `include/rtos_mutex.h` | `39d8fbd73260938985ce068aef9b94a1931e53bf6ad2b595727ceb4284ee385b` |
| `include/rtos_queue.h` | `c5e5354cd58afb90734966948f385d519cd63f59faa4df146a51ad98a4aed989` |
| `include/rtos_sched.h` | `a09a7fbc0db9e1290781044d84b1dfe4589901c720a825792fbb374da83c59d4` |
| `include/rtos_semaphore.h` | `bb7c0943a3c0d9e4a306e1cc44dc0b8c59703170bae3f2480ebe8e9cb0fd06ca` |
| `include/rtos_task.h` | `8641b85e3a11fb53a29d4f3e7928883dc3ce30b18e17f21974baf2d3308e7ec9` |
| `include/rtos_tick.h` | `7cd3692831a1d10fa2f9883963b9bedf30def4999782ec61bd9f3b3485d5645f` |
| `include/rtos_trace.h` | `7b7842a987f95904fde07db421d1521593f3c0c57ef1b57b29c43876b5a6a891` |
| `include/rtos_wait.h` | `cd55a435eb2a1f523dc64a2813a8d4581224156d3ff44e426acc1c71ecb2ddf3` |
| `arch/arm/cortex_m3/port.c` | `5a1d2abc04acfdd2b10b96a24e6fb5fb29bfaf4259a927d8898e238726335e80` |
| `arch/arm/cortex_m3/port.h` | `bf1df4f6f2bc0b96caf9d548111ab8411ea5e7606c7663fd1e15d47007f7dd38` |
| `arch/arm/cortex_m3/portasm.S` | `bf86470b955425ce6bcea0a5985aaf13579eea4da4d09718a32ef80c8e352436` |
| `arch/arm/cortex_m3/systick.c` | `15de98d13af42807a709515f5e9b59f151b6e72bc4e68408e6d313d644093c18` |
| `arch/arm/cortex_m3/systick_config.c` | `28f24a3aa3d3acf63952bd3dee6152fd9209285bd53f0a4061b6a6005b51ff42` |
| `lib/memcpy.c` | `f65eec6d68dd38459fcd72fda11f1a32b35711fb37e67e5cdbf5511643d3979e` |
