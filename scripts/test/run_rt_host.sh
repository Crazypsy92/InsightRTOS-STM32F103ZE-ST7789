#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "=== [1/2] Building Stage 9.3-A Real-Time Diagnostics Host Test ==="
mkdir -p "${REPO_DIR}/build/tests"

gcc -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
    -Wstrict-prototypes -Wmissing-prototypes -Wundef \
    -std=c11 -O2 \
    -DINSIGHT_DIAG_RT=1 \
    -DINSIGHT_TRACE_ENABLED=1 \
    -DINSIGHT_DIAG_EXPLAIN=1 \
    -DINSIGHT_DIAG_DL=1 \
    -DRTOS_TESTING=1 \
    -DRTOS_PREEMPTIVE=1 \
    -DRTOS_MUTEX_PI=1 \
    -DRTOS_TICK_HZ=100 \
    -DRTOS_TIME_SLICE_TICKS=1 \
    -I "${REPO_DIR}/Middlewares/InsightRTOS/include" \
    -I "${REPO_DIR}/Middlewares/InsightRTOS/kernel" \
    -I "${REPO_DIR}/Middlewares/InsightRTOS/arch/arm/cortex_m3" \
    -I "${REPO_DIR}/App/Inc" \
    -I "${REPO_DIR}/BSP/Inc" \
    -I "${REPO_DIR}/tests/host" \
    "${REPO_DIR}/App/Src/app_ipc.c" \
    "${REPO_DIR}/tests/host/test_diag_rt.c" \
    "${REPO_DIR}/tests/host/mock_port.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/diag_rt.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/task.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/scheduler.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/ready_queue.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/tick.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/delay.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/delay_queue.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/critical.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/semaphore.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/queue.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/mutex.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/priority.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/priority_graph.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/wait.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/trace.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/kernel/deadlock.c" \
    "${REPO_DIR}/Middlewares/InsightRTOS/arch/arm/cortex_m3/systick_config.c" \
    -o "${REPO_DIR}/build/tests/test_diag_rt"

echo "=== [2/2] Running Stage 9.3-A Real-Time Diagnostics Host Test ==="
"${REPO_DIR}/build/tests/test_diag_rt"
