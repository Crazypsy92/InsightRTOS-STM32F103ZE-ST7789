#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "===================================================="
echo "InsightRTOS Stage 9.3-A STM32 Hardware Verification"
echo "===================================================="

BIN_FILE="${REPO_DIR}/build/DISPLAY_RTOS_O2_Trace/insightrtos_display.bin"
if [ ! -f "${BIN_FILE}" ]; then
    echo "Error: Firmware binary ${BIN_FILE} not found. Please build it first."
    exit 1
fi

echo ">>> [1/2] Controlled Flash Programming (Pages 0..30)..."
python3 "${REPO_DIR}/tests/hardware/flash_firmware.py" "${BIN_FILE}"

echo ">>> [2/2] Running 60-Second In-Flight Stability Sampling..."
python3 "${REPO_DIR}/tests/hardware/sample_stability.py"

echo "===================================================="
echo "HARDWARE VERIFICATION SUCCESSFUL"
echo "===================================================="
