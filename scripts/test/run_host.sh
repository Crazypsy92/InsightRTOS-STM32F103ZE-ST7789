#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "=== [1/2] Building Stage 9.2-H5 Modular Host Test ==="
mkdir -p "${REPO_DIR}/build/tests"

gcc -Wall -Wextra -O2 \
    -I "${REPO_DIR}/App/Inc" \
    -I "${REPO_DIR}/BSP/Inc" \
    -I "${REPO_DIR}/Middlewares/InsightRTOS/include" \
    -I "${REPO_DIR}/Middlewares/InsightRTOS/config" \
    -I "${REPO_DIR}/tests/host" \
    "${REPO_DIR}/tests/host/test_app_modular.c" \
    "${REPO_DIR}/tests/host/mock_kernel.c" \
    "${REPO_DIR}/App/Src/app_events.c" \
    "${REPO_DIR}/App/Src/app_ipc.c" \
    "${REPO_DIR}/BSP/Src/lcd_cn16.c" \
    "${REPO_DIR}/BSP/Src/lcd_fonts.c" \
    "${REPO_DIR}/BSP/Src/multi_button.c" \
    -o "${REPO_DIR}/build/tests/test_app_modular"

echo "=== [2/2] Running Stage 9.2-H5 Modular Host Test ==="
"${REPO_DIR}/build/tests/test_app_modular"
