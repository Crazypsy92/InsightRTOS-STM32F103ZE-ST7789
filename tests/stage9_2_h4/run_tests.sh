#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "=== Building Stage 9.2-H4 Host Test ==="
gcc -Wall -Wextra -O2 \
    -I"${REPO_DIR}/BSP/Inc" \
    -I"${REPO_DIR}/Middlewares/InsightRTOS/include" \
    -I"${REPO_DIR}/Middlewares/InsightRTOS/config" \
    -I"${REPO_DIR}/Core" \
    "${SCRIPT_DIR}/test_stage9_2_h4.c" \
    "${REPO_DIR}/BSP/Src/lcd_cn16.c" \
    "${REPO_DIR}/BSP/Src/lcd_fonts.c" \
    "${REPO_DIR}/BSP/Src/multi_button.c" \
    -o "${SCRIPT_DIR}/test_stage9_2_h4"

echo "=== Running Stage 9.2-H4 Host Test ==="
"${SCRIPT_DIR}/test_stage9_2_h4"
