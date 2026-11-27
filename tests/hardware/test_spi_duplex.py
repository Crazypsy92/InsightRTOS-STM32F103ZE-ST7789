#!/usr/bin/env python3
"""Cross-check the production fixture's C helpers and replay the captured frame."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import zlib

from read_spi_duplex import cases, decode, payload, validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frame", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        executable = Path(tmp) / "data"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(Path(__file__).with_name("spi_duplex_data_host.c")), "-o", str(executable)],
                       check=True)
        rows = subprocess.check_output([executable], text=True).splitlines()
        if len(rows) != 12:
            raise AssertionError("expected twelve native CRC cases")
        for row in rows:
            size, pattern, crc = row.split(",")
            assert zlib.crc32(payload(int(size), int(pattern))) == int(crc, 16)
    assert len(cases()) == 22
    assert decode(bytes.fromhex("01000000"), {"size": 4, "signed": False}) == 1
    print("PASS: 12 C buffer/CRC cases independently checked by Python")
    if args.frame:
        frame = json.loads(args.frame.read_text())["report"]
        validate(frame)
        for key in ("rx_crc", "first_mismatch", "bounds_ok"):
            malformed = deepcopy(frame)
            malformed["runs"][2]["data"][key] ^= 1
            try:
                validate(malformed)
            except (RuntimeError, AssertionError):
                continue
            raise AssertionError(f"validator accepted corrupted {key}")
        print("PASS: captured frame replay and three corrupted-result rejection cases")


if __name__ == "__main__":
    main()
