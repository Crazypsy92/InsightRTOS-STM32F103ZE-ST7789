#!/usr/bin/env python3
"""Offline checks against the actual ARM ELF, including invalid layout fixtures."""
import copy
import sys
from pathlib import Path

from sample_stability import decode, field, read_layout, verify_layout

for elf in map(Path, sys.argv[1:]):
    nodes = read_layout(elf)
    tracker = nodes["g_event_tracker"]
    export = nodes["g_diag_rt_export"]
    task = field(export, "task_snapshots")["element"]
    assert field(tracker, "latest_id")["offset"] == 192
    assert task["size"] == 56 and field(task, "flags")["offset"] == 32
    assert field(export, "seq_end")["offset"] == 304
    assert field(export, "published_tick")["offset"] == 308
    for key in ("tracker", "rt"):
        broken = copy.deepcopy(nodes)
        if key == "tracker":
            broken["g_event_tracker"]["fields"]["latest_id"]["offset"] = 160
        else:
            broken["g_diag_rt_export"]["fields"]["task_snapshots"]["element"]["fields"]["flags"]["offset"] = 28
        try:
            verify_layout(broken)
        except RuntimeError:
            pass
        else:
            raise AssertionError(f"old {key} offset accepted")
    try:
        decode(b"\0" * (tracker["size"]-4), tracker)
    except RuntimeError:
        pass
    else:
        raise AssertionError("short read accepted")
    print(f"{elf}: ELF DWARF ABI and unsupported layout/short read fixtures PASS")
