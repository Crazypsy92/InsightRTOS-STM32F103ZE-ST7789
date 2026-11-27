#!/usr/bin/env python3
"""Read and validate the completed SPI DMA fixture on ZET6."""
import argparse
import json
from pathlib import Path
import time

from read_timing import layout, record
from sample_stability import Tcl, decode, require


def clean(regs):
    return (regs["sr"] & 0xc3) == 2 and regs["cr2"] & 3 == 0 and \
        (regs["tx_ccr"] | regs["rx_ccr"]) & 1 == 0 and regs["isr"] & 255 == 0 and regs["cs"] == 2048


def validate(frame):
    cfgr = frame["rcc_cfgr"]
    require(frame["state"] == 2 and frame["completed"] == 8 and frame["hclk"] == 72_000_000 and
            frame["pclk"] == 36_000_000 and (cfgr >> 2) & 3 == 2 and
            (cfgr >> 4) & 15 == 0 and (cfgr >> 8) & 7 == 4 and frame["dwt_ctrl"] & 1,
            "PLL, APB1, DWT and fixture completion")
    splits = [[16], [], [16], [65535], [65535, 1], [65535, 65535], [8192], [16]]
    owner = frame["runs"][0]["acquired"]["owner"]
    require(bool(owner), "non-null transaction owner")
    for i, (run, lengths, length) in enumerate(zip(frame["runs"], splits,
                                                   (16, 15, 16, 65535, 65536, 131070, 8192, 16))):
        fault = i == 6
        expected_result = -2 if fault else 0
        acquired, sent, released = (run[k] for k in ("acquired", "transferred", "released"))
        require(run["length"] == length and run["source"] == 0x08010000 and run["cycles"] > 0
                and run["chunks"] == len(lengths) and
                (run["acquire"], run["transfer"], run["release"]) == (0, expected_result, 1),
                f"SPI transaction {i}")
        require((acquired["state"], sent["state"], released["state"]) ==
                (2, 3 if fault else 2, 3 if fault else 1) and
                acquired["owner"] == sent["owner"] == owner and
                acquired["depth"] == sent["depth"] == 1 and released["owner"] == released["depth"] == 0 and
                acquired["error"] == 0 and sent["error"] == released["error"] == expected_result and
                (acquired["recover"], sent["recover"], released["recover"]) ==
                [(0, 0, 0), (0, 1, 1), (2, 2, 2)][0 if i < 6 else 1 if fault else 2],
                f"SPI state/owner/recover {i}")
        require(acquired["regs"]["cr1"] & 0x3b == 0 and
                clean(sent["regs"]) and clean(released["regs"]), f"SPI cleanup or BR {i}")
        offset = 0
        for j, chunk_len in enumerate(lengths):
            c = run["chunk"][j]
            initial, waited = c["initial"], c["waited"]
            require((c["offset"], c["length"], initial["cmar"], initial["cndtr"]) ==
                    (offset, chunk_len, run["source"] + offset, chunk_len) and
                    initial["cs"] == waited["cs"] == 2048 and
                    (initial["tx_ccr"] | initial["rx_ccr"]) & 1 == 0 and
                    waited["cr2"] & 3 == 2 and waited["rx_ccr"] & 1 == 0 and
                    c["budget"] == 1_440_000 + 40 * chunk_len and c["status"] == expected_result,
                    f"DMA arm and budget {i}/{j}")
            if fault:
                inj, stopped = c["injection"], c["stopped"]
                require(inj["tx_ccr"] & 1 and stopped["tx_ccr"] & 1 == 0 and
                        stopped["cndtr"] == waited["cndtr"] and 0 < waited["cndtr"] <= inj["cndtr"] < chunk_len and
                        (inj["isr"] | stopped["isr"] | waited["isr"]) & 0xa0 == 0 and
                        c["elapsed"] >= c["budget"], "real bounded DMA timeout")
            else:
                drained = c["drained"]
                require(waited["cndtr"] == drained["cndtr"] == 0 and
                        waited["isr"] & 0xa0 == drained["isr"] & 0xa0 == 0x20 and
                        drained["sr"] & 0x82 == 2 and drained["cs"] == 2048,
                        f"DMA completion, TXE and BSY {i}/{j}")
            offset += chunk_len


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    nodes = layout(args.elf)
    node = nodes["g_spi_dma_fixture"]
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
        print("STM32 SPI DMA PASS: threshold, long blocks, timeout and recovery")
    finally:
        target.socket.close()


if __name__ == "__main__":
    main()
