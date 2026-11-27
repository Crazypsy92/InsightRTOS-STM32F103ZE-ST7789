#!/usr/bin/env python3
"""Exercise panel accounting and the DMA/CS decoder, optionally replay board data."""
import argparse
from copy import deepcopy
import json
from pathlib import Path

from read_lcd_panel import validate_key, validate_panel, validate_series


def sample():
    active = dict(cmar=0x20000000, cndtr=480, ccr=0x90, rx_ccr=0, isr=0,
                  cr1=0, cr2=0, sr=2, pins=0x1000, owner=1, depth=1, state=2,
                  recover=0, last_error=0)
    returned = {**active, "cndtr": 0}
    waited = {**returned, "isr": 0x20, "cr2": 2, "sr": 0x43}
    idle = {**returned, "owner": 0, "depth": 0, "state": 1, "pins": 0x1800}
    row = dict(length=480, armed=active, waited=waited, drained=waited.copy(),
               returned=returned, released=idle)
    bus = dict(invariants=0, failures=0, selects=3, releases=3, returns=3,
               dma=1, poll=2, command_writes=1, data_writes=2,
               dma_buckets=[0, 0, 0, 0, 1, 0], maximum_transaction_cycles=10000,
               row=row, last_release=idle.copy())
    return dict(state=1, lcd_error=0, preinit_pins=0x800, hclk=72000000,
                rcc_cfgr=0x1d040a, bus=bus, full_frames=1, local_updates=1,
                pattern_counts=[1, 0, 0, 0, 0, 0, 0])


def rejection(base, component, field, value):
    changed = deepcopy(base)
    node = changed["bus"]
    for key in component:
        node = node[key]
    node[field] = value
    try:
        validate_panel(changed)
    except RuntimeError:
        return
    raise AssertionError(f"accepted bad {'/'.join(component)} {field}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path)
    args = parser.parse_args()
    fixture = sample()
    validate_panel(fixture)
    rejection(fixture, ("row", "waited"), "isr", 0)
    rejection(fixture, ("row", "armed"), "pins", 0x1800)
    rejection(fixture, ("row", "returned"), "sr", 0x43)
    rejection(fixture, ("last_release",), "owner", 7)
    rejection(fixture, (), "releases", 2)
    print("PASS: DMA/CS/DC and transaction accounting, five negative cases")
    if args.capture:
        result = json.loads(args.capture.read_text())
        for row in result["samples"]:
            validate_panel(row["panel"])
        seconds = validate_series(result["samples"])
        if abs(seconds - result["elapsed_s"]) > 0.000001:
            raise AssertionError("capture duration changed")
        validate_key(result["samples"][-1], result["key"])
        print("PASS: full hardware capture and physical KEY event replay")


if __name__ == "__main__":
    main()
