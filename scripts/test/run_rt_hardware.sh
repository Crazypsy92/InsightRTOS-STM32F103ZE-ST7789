#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "===================================================="
echo "InsightRTOS Stage 9.3-A2 STM32 Hardware Verification"
echo "===================================================="

BIN_FILE="${REPO_DIR}/build/DISPLAY_RTOS_O2_Trace/insightrtos_display.bin"
ELF_FILE="${REPO_DIR}/build/DISPLAY_RTOS_O2_Trace/insightrtos_display.elf"
MAP_FILE="${REPO_DIR}/build/DISPLAY_RTOS_O2_Trace/insightrtos_display.map"
LOG_DIR="${REPO_DIR}/evidence/stage9_3_a2/logs"

if [ ! -f "${BIN_FILE}" ]; then
    echo "Error: Firmware binary ${BIN_FILE} not found. Please build it first."
    exit 1
fi

echo ">>> [1/3] Controlled Flash Programming (Pages 0..30)..."
python3 "${REPO_DIR}/tests/hardware/flash_firmware.py" "${BIN_FILE}" "${LOG_DIR}"

echo ">>> [2/3] Running 60-Second Baseline Stability Sampling..."
python3 "${REPO_DIR}/tests/hardware/sample_stability.py" \
    --mode normal \
    --duration 60 \
    --interval 5.0 \
    --elf "${ELF_FILE}" \
    --bin "${BIN_FILE}" \
    --map "${MAP_FILE}" \
    --out-dir "${LOG_DIR}"

echo ">>> [3/3] Running 5-Minute (300s) Controlled Stress Sampling..."
python3 "${REPO_DIR}/tests/hardware/sample_stability.py" \
    --mode stress \
    --duration 300 \
    --interval 30.0 \
    --stress-rate 100 \
    --elf "${ELF_FILE}" \
    --bin "${BIN_FILE}" \
    --map "${MAP_FILE}" \
    --out-dir "${LOG_DIR}"

echo "===================================================="
echo "STAGE 9.3-A2 HARDWARE VERIFICATION SUCCESSFUL"
echo "===================================================="
