#!/usr/bin/env python3
"""Read and validate the completed SPI duplex fixture on ZET6."""
import argparse
import json
from pathlib import Path
import time
import zlib

from read_timing import layout, record
from sample_stability import Tcl, decode, require


def cases():
    return [(15, 0, 0), (257, 2, 0)] + [
        (length, pattern, repeat)
        for length in (16, 2048) for pattern in range(3) for repeat in range(3)
    ] + [(2048, 2, 0), (16, 2, 0)]


def payload(length, pattern):
    if pattern == 0:
        return bytes(i & 255 for i in range(length))
    if pattern == 1:
        return (b"\x55\xaa" * ((length + 1) // 2))[:length]
    result = bytearray()
    state = 0x98c0ffee
    for _ in range(length):
        state ^= (state << 13) & 0xffffffff
        state ^= state >> 17
        state ^= (state << 5) & 0xffffffff
        result.append(state & 255)
    return result


def clean(r):
    return (r["sr"] & 0xc3) == 2 and r["cr2"] & 3 == 0 and \
        (r["tx_ccr"] | r["rx_ccr"]) & 1 == 0 and r["isr"] & 255 == 0 and r["cs"] == 2048


def validate(frame):
    expected = cases()
    tx, rx = frame["tx"], frame["rx"]
    require(frame["state"] == 2 and frame["completed"] == len(expected) == len(frame["runs"]) and
            frame["hclk"] == 72_000_000 and frame["pclk"] == 36_000_000 and
            frame["dwt_ctrl"] & 1 and (frame["rcc_cfgr"] >> 2) & 3 == 2 and
            (frame["rcc_cfgr"] >> 4) & 15 == 0 and (frame["rcc_cfgr"] >> 8) & 7 == 4 and
            (tx | rx) & 3 == 0 and abs(tx - rx) >= 2048 and
            all(0x20000000 <= addr <= 0x20010000 - 2048 for addr in (tx, rx)), "fixture and SRAM")
    owner = frame["runs"][0]["acquired"]["owner"]
    require(owner != 0, "transaction owner")
    for i, (run, (length, pattern, repeat)) in enumerate(zip(frame["runs"], expected)):
        fault = i == 20
        rc = -2 if fault else 0
        a, t, r = (run[k] for k in ("acquired", "transferred", "released"))
        require((run["length"], run["pattern"], run["repeat"]) == (length, pattern, repeat) and
                (run["acquire"], run["transfer"], run["release"]) == (0, rc, 1) and run["elapsed"] > 0 and
                a["owner"] == t["owner"] == owner and a["depth"] == t["depth"] == 1 and
                (a["state"], t["state"], r["state"]) == (2, 3 if fault else 2, 3 if fault else 1) and
                a["error"] == r["owner"] == r["depth"] == 0 and r["error"] == t["error"] == rc and
                a["regs"]["cr1"] & 0x3b == 0 and a["regs"]["cs"] == 2048 and
                clean(t["regs"]) and clean(r["regs"]), f"bus state: {i}")
        require((a["recover"], t["recover"], r["recover"]) ==
                ((0, 0, 0) if i < 20 else (0, 1, 1) if fault else (2, 2, 2)), f"recover count: {i}")
        data = run["data"]
        tx_bytes = payload(length, pattern)
        require(data["bounds_ok"] and data["tx_crc"] == zlib.crc32(tx_bytes), f"data: {i}")
        if not fault:
            require(data["mismatch_count"] == 0 and data["first_mismatch"] == 0xffffffff and
                    data["tx_crc"] == data["rx_crc"], f"loopback bytes: {i}")
        if i == 0:
            require(run["chunks"] == 0, "15-byte polling")
            continue
        c = run["chunk"]
        initial, first, waited = (c[k] for k in ("initial", "rx_first", "waited"))
        require(run["chunks"] == 1 and initial["tx_cmar"] == tx and initial["rx_cmar"] == rx and
                initial["tx_cndtr"] == initial["rx_cndtr"] == length and
                first["tx_cndtr"] == first["rx_cndtr"] == length and
                initial["tx_ccr"] & 0xfd1 == 0x90 and initial["rx_ccr"] & 0xfd1 == 0x80 and
                first["tx_ccr"] & 1 == 0 and first["rx_ccr"] & 1 == 1 and first["cr2"] & 3 == 1 and
                initial["cs"] == first["cs"] == waited["cs"] == 2048 and
                waited["cr2"] & 3 == 3 and c["budget"] == 1_440_000 + 40 * length and c["status"] == rc,
                f"RX-first setup and budget: {i}")
        if fault:
            inj, stopped = c["injection"], c["stopped"]
            count = waited["rx_cndtr"]
            received = length - count
            partial = tx_bytes[:received] + bytes(0xa8 if v == 0xa7 else 0xa7 for v in tx_bytes[received:])
            require(inj["tx_ccr"] & inj["rx_ccr"] & 1 and inj["cr2"] & stopped["cr2"] & 3 == 3 and
                    0 < count == stopped["rx_cndtr"] <= inj["rx_cndtr"] < length and
                    0 < stopped["tx_cndtr"] <= inj["tx_cndtr"] < length and
                    stopped["rx_ccr"] & 1 == 0 and stopped["tx_ccr"] & 1 == 1 and
                    (inj["isr"] | stopped["isr"]) & 0xaa == 0 and waited["tx_cndtr"] == 0 and
                    waited["isr"] & 0xaa == 0x20 and waited["sr"] & 0xc3 == 0x43 and
                    c["elapsed"] >= c["budget"] and data["first_mismatch"] == received and
                    data["mismatch_count"] == count and data["rx_crc"] == zlib.crc32(partial),
                    "RX stopped, TX completed, OVR and natural timeout")
        else:
            drain = c["drained"]
            require(waited["tx_cndtr"] == waited["rx_cndtr"] == drain["tx_cndtr"] == drain["rx_cndtr"] == 0 and
                    waited["isr"] & 0xaa == drain["isr"] & 0xaa == 0x22 and
                    drain["sr"] & 0xc3 == 2 and drain["cs"] == 2048, f"DMA completed on both channels: {i}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    node = nodes["g_spi_duplex_fixture"]
    address = node["address"]
    target = Tcl(6673)
    try:
        deadline = time.monotonic() + 20
        with args.out.with_suffix(".jsonl").open("x", buffering=1) as log:
            while time.monotonic() < deadline:
                state = target.words(address)[0]
                debug = target.words(0xe000edf0)[0]
                guards = [target.words(nodes["display_stacks"]["address"] + i * 4096)[0]
                          for i in range(1)] + target.words(nodes["idle_stack"]["address"])
                health = {"state": state, "tick": record(target, nodes["kernel_ticks"]),
                          "tim2_ms": record(target, nodes["g_sys_tick"]),
                          "assert_line": record(target, nodes["g_rtos_assert_line"]),
                          "faults": target.words(0xe000ed28, 2), "guards": guards}
                log.write(json.dumps(health) + "\n")
                if health["assert_line"] or any(health["faults"]) or debug & ((1 << 17) | (1 << 19)):
                    raw = target.data(address, node["size"])
                    args.out.with_suffix(".raw.hex").write_text(raw.hex() + "\n")
                    args.out.write_text(json.dumps({"report": decode(raw, node), "health": health},
                                                   indent=2) + "\n")
                require(health["assert_line"] == 0 and health["faults"] == [0, 0] and
                        not debug & ((1 << 17) | (1 << 19)) and guards == [0xdeadc0de] * 2,
                        f"fixture halted or faulted: {health}")
                if state == 2:
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError("fixture did not finish before deadline")
        raw = target.data(address, node["size"])
        frame = decode(raw, node)
        require(raw == target.data(address, node["size"]), "unstable fixture export")
        args.out.with_suffix(".raw.hex").write_text(raw.hex() + "\n")
        result = {"report": frame, "health": health}
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        validate(frame)
        result["status"] = "PASS"
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        print("STM32 SPI duplex PASS: loopback, RX timeout and recovery")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
