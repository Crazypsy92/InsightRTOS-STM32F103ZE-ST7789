#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
if [[ $# != 4 ]]; then
    echo "Usage: $0 normal|stress|burst TARGET.elf TARGET.bin OUTPUT_DIRECTORY" >&2
    echo "Requires the already read-back-verified image and the verified local MEM-AP session." >&2
    exit 2
fi
exec python3 -u "$repo/tests/hardware/sample_stability.py" --mode "$1" --elf "$2" --bin "$3" --out-dir "$4"
