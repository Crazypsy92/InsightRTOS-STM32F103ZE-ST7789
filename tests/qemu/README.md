# InsightRTOS QEMU Non-Hardware Validation Environment

## Overview

This directory provides configurations and documentation for ARM Cortex-M3 emulation under QEMU.
In Stage 9.2-H5, non-hardware validation of kernel primitives, deadlock detection, and scheduling trace is executed using the 360-test regression suite under `tests/host` and `build-host-stage9_2`.

When running on platforms without physical STM32F103 hardware or display panels, QEMU can emulate the ARM Cortex-M3 core (`-M mps2-an385 -cpu cortex-m3` or `-M lm3s6965evb`).

## Status in Stage 9.2-H5

- **Host Tests**: 8/8 suites (35 assertions) PASS.
- **Kernel Regression**: 360/360 tests PASS.
- **Physical Hardware (STM32F103ZET6 + ST7789)**: Primary deployment target.
- **QEMU Status**: Automated via `scripts/test/run_qemu.sh`.
