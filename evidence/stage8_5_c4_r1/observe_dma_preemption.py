"""Try to witness an LCD DMA wait saved in PSP while a higher task runs."""
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
import runtime_monitor as monitor

e = Path(__file__).resolve().parent
startup = json.loads((e / "startup-context.json").read_text())
lcd = next(t["tcb"] for t in startup["task_entries"] if t["task"] == "lcd_task")
higher = {t["tcb"]: t["task"] for t in startup["task_entries"] if t["base_priority"] > 2}
current_address = monitor.S["symbols"]["rtos_current_task"]
base = monitor.S["stacks"]["LCD"]["address"]
# C3 O2 disassembly: DMA TC/TE poll loop within ll_transfer.
poll_start, poll_end = 0x08001b82, 0x08001b9a
source = monitor.Tcl()
started = time.monotonic()
out = {"status": "NOT TESTED", "method": "non-halting bracketed TCB/PSP and SPI bus reads",
       "poll_range": [hex(poll_start), hex(poll_end)], "samples": 0}
try:
    while time.monotonic() - started < 5:
        current = source.word(current_address)
        out["samples"] += 1
        if current not in higher:
            continue
        saved_sp = source.word(lcd)
        if not base + 4 <= saved_sp <= base + 4096 - 64:
            continue
        pc, xpsr = source.rd(saved_sp + 56, 2)
        if not poll_start <= pc < poll_end or xpsr & 0x1ff or not xpsr & (1 << 24):
            continue
        owner, packed = source.rd(0x200002a8, 2)
        if not owner or packed & 0xffff == 0 or (packed >> 16) & 0xff != 2:
            continue
        cr2 = source.word(0x40003c04)
        if source.word(lcd) == saved_sp and source.word(current_address) == current:
            out.update(status="PASS", interrupted_pc=hex(pc), saved_psp=hex(saved_sp),
                       xpsr=hex(xpsr), current_higher_task=higher[current], bus_owner=hex(owner),
                       spi_cr2=hex(cr2))
            break
finally:
    source.close()
    out["elapsed_s"] = time.monotonic() - started
    (e / "dma-preemption-observation.json").write_text(json.dumps(out, indent=2) + "\n")
print(json.dumps(out))
