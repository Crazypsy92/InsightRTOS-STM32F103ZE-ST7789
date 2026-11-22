"""Capture free-running stack fill and target state after stability."""
from pathlib import Path
import json
import struct
import sys

sys.dont_write_bytecode = True
import runtime_monitor as monitor

evidence = Path(__file__).resolve().parent
hw = monitor.Tcl()
out = {"scope": "after completed 60 s, uninterrupted MEM-AP reads", "stacks": {}}
try:
    before = monitor.sample(hw)
    monitor.safe(before, True)
    for name, region in monitor.S["stacks"].items():
        path = evidence / (name + "-stack.bin")
        hw.cmd(f"dump_image {path} {region['address']:#x} {region['bytes']}")
        data = path.read_bytes()
        guard, *words = struct.unpack(f"<{region['bytes']//4}I", data)
        unused_words = next((i for i, word in enumerate(words) if word != 0xa5a5a5a5), len(words))
        if guard != 0xdeadc0de:
            raise RuntimeError(f"{name} stack guard mismatch")
        out["stacks"][name] = {"guard": hex(guard), "bytes": len(data),
                              "used_from_fill_bytes": len(data) - 4 - unused_words*4}
    out["sample"] = monitor.sample(hw)
    monitor.safe(out["sample"], True)
    assert out["sample"]["kernel_ticks"] > before["kernel_ticks"] or out["sample"]["tim2_ms"] > before["tim2_ms"]
    out["status"] = "PASS"
    out["MSP_watermark"] = "NOT MEASURED"
finally:
    hw.close()
    (evidence / "post-stability-stacks.json").write_text(json.dumps(out, indent=2)+"\n")
