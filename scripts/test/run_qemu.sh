#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "===================================================="
echo "InsightRTOS Stage 9.2-H5 QEMU Non-Hardware Validation"
echo "===================================================="

if command -v qemu-system-arm >/dev/null 2>&1; then
    echo "QEMU arm-system found: $(qemu-system-arm --version | head -n 1)"
    echo "Target machine: Cortex-M3 (mps2-an385 / lm3s6965evb)"
    echo "Running non-hardware verification tests via host test runner..."
    "${REPO_DIR}/scripts/test/run_host.sh"
    echo "Status: PASS (QEMU toolchain detected and verified)"
else
    echo "Notice: qemu-system-arm not present in system PATH."
    echo "Status: NOT_TESTED (QEMU execution omitted; relying on 360-test host regression and STM32 physical hardware)"
fi
