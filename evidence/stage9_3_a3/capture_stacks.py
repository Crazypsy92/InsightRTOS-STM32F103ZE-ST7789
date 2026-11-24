"""Optional post-window, non-halting watermark read."""
from pathlib import Path
import json
import os
import struct
import sys

work, out = map(Path, sys.argv[1:])
sys.path.insert(0, str(work / "tests/hardware"))
from sample_stability import Tcl, require

layout = json.loads((out/"stress/layout.json").read_text())
t = Tcl(6673)
report = {}
try:
    for i, name in enumerate(("KEY","CTRL","LCD","MON","IDLE")):
        loc = layout["idle_stack"] if i == 4 else layout["display_stacks"]
        base = loc["address"] if i == 4 else loc["address"] + 4096*i
        size = loc["size"] if i == 4 else 4096
        path = (out/f"stack-{name}.bin").resolve()
        t.cmd(f"dump_image {path} {base:#x} {size}")
        data = path.read_bytes()
        require(len(data) == size, "short stack read")
        guard, *words = struct.unpack(f"<{size//4}I", data)
        require(guard == 0xdeadc0de, "stack guard mismatch")
        first = next((j for j, v in enumerate(words) if v != 0xa5a5a5a5), len(words))
        report[name] = {"origin": hex(base), "bytes": size, "guard": hex(guard),
                        "watermark_bytes": size - 4 - 4*first}
    addr = layout["g_stress_request"]["address"]
    require(t.words(addr)[0] == 0, "stress request not zero")
    report["MSP"] = "NOT MEASURED: no prefill established"
    (out/"stack-watermarks.json").write_text(json.dumps(report,indent=2)+"\n")
finally:
    t.socket.close()
