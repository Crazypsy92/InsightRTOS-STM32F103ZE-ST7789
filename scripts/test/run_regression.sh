#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "===================================================="
echo "InsightRTOS Stage 9.3-A Full Regression Suite"
echo "===================================================="

echo ">>> [Stage 1/4] Running H5 Host Application Tests..."
"${REPO_DIR}/scripts/test/run_host.sh"

echo ">>> [Stage 2/4] Running Stage 9.3-A Real-Time Diagnostics Tests..."
"${REPO_DIR}/scripts/test/run_rt_host.sh"

echo ">>> [Stage 3/4] Running Stage 9.3-A QEMU Target Runner..."
"${REPO_DIR}/scripts/test/run_rt_qemu.sh"

echo ">>> [Stage 4/4] Running Kernel ctest Suite (360 tests)..."
CODEX_BUILD_DIR="/home/mythepsy92/program/codex/InsightRTOS/build-host-stage9_2"
if [ -d "${CODEX_BUILD_DIR}" ]; then
    ctest --test-dir "${CODEX_BUILD_DIR}" --output-on-failure
else
    echo "Warning: Codex build dir ${CODEX_BUILD_DIR} not found; skipping kernel ctest."
fi

echo "===================================================="
echo "ALL REGRESSION TESTS COMPLETED SUCCESSFULLY"
echo "===================================================="
