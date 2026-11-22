#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "===================================================="
echo "InsightRTOS Stage 9.2-H5 Controlled Hardware Runner"
echo "===================================================="

echo ">>> [Phase 1/2] Controlled Flash Programming & Readback Verification..."
python3 "${REPO_DIR}/tests/hardware/flash_firmware.py"

echo ">>> [Phase 2/2] 60-Second Real-Time Stability Sampling..."
python3 "${REPO_DIR}/tests/hardware/sample_stability.py"

echo "===================================================="
echo "HARDWARE VERIFICATION COMPLETED"
echo "===================================================="
