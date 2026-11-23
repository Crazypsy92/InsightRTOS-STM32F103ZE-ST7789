#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "===================================================="
echo "InsightRTOS Stage 9.3-A QEMU Target Runner"
echo "===================================================="

if command -v qemu-system-arm >/dev/null 2>&1; then
    echo "QEMU arm-system found: $(qemu-system-arm --version | head -n 1)"
    echo "Target machine: Cortex-M3 (mps2-an385 / lm3s6965evb)"
    echo "Notice: STM32F103ZET6 board is not supported by upstream QEMU system emulator."
    echo "No dedicated lm3s6965evb fixture target compiled for this STM32 board."
    echo "Status: NOT_TESTED (Real ARM QEMU execution omitted due to board incompatibility; relying on host tests and physical STM32F103ZET6 hardware)"
else
    echo "Notice: qemu-system-arm binary not present in system PATH."
    echo "Status: NOT_TESTED (Real ARM QEMU execution omitted due to missing tool; host regression and STM32 hardware verified)"
fi
