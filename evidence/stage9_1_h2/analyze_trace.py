#!/usr/bin/env python3
"""Capture and analyze explainable scheduling trace records from live target."""
import socket, struct, json
from pathlib import Path

E = Path(__file__).resolve().parent

def main():
    s = socket.create_connection(('127.0.0.1', 6669), 10)
    def cmd(c):
        s.sendall(c.encode() + b'\x1a')
        res = b''
        while b'\x1a' not in res:
            res += s.recv(65536)
        return res.split(b'\x1a')[0].decode().strip()

    # Read trace metadata
    seq = int(cmd('read_memory 0x20000f00 32 1'), 0)
    seq_hi = int(cmd('read_memory 0x20000ef8 32 1'), 0)
    ovr = int(cmd('read_memory 0x20000efc 32 1'), 0)
    cnt = int(cmd('read_memory 0x20000f04 32 1'), 0)
    w_idx = int(cmd('read_memory 0x20000f08 32 1'), 0)
    status = int(cmd('read_memory 0x20000ef4 32 1'), 0)
    full_seq = (seq_hi << 32) | seq

    # Read all 128 records (128 * 24 B = 3072 B = 768 words)
    words = [int(x, 0) for x in cmd('read_memory 0x20000f10 32 768').split()]
    raw = struct.pack('<' + 'I'*len(words), *words)
    s.close()

    records = []
    for i in range(128):
        chunk = raw[i*24:(i+1)*24]
        seq_i, tick, gen, arg0, arg1, slot, etype = struct.unpack('<IIIIIHH', chunk)
        records.append({
            'idx': i,
            'seq': seq_i,
            'tick': tick,
            'gen': gen,
            'arg0': arg0,
            'arg1': arg1,
            'slot': slot,
            'type': etype
        })

    # Sort records by seq
    valid = [r for r in records if r['type'] > 0]
    valid.sort(key=lambda r: r['seq'])

    event_names = {
        1: 'TASK_CREATE', 2: 'TASK_READY', 3: 'TASK_BLOCK', 4: 'SCHED_SELECT',
        5: 'WAIT_COMPLETE', 6: 'PRIO_CHANGE', 7: 'STOP_REQ', 8: 'TASK_TERM',
        9: 'TASK_RECLAIM', 10: 'DELAY_EXPIRED', 11: 'TASK_YIELD', 12: 'CONTEXT_SWITCH'
    }
    commit_classes = {
        0: 'OTHER', 1: 'HIGHER_PREEMPT', 2: 'YIELD', 3: 'SLICE_ROTATION',
        4: 'BLOCK_OR_WAKE_OTHER', 5: 'EXIT', 6: 'IDLE_TRANSITION', 7: 'MIXED'
    }
    tasks = {0: 'KEY', 1: 'Controller', 2: 'LCD', 3: 'Monitor', 65535: 'Idle'}

    decoded = []
    class_counts = {}
    event_counts = {}

    for r in valid:
        en = event_names.get(r['type'], f'TYPE_{r["type"]}')
        event_counts[en] = event_counts.get(en, 0) + 1
        primary = tasks.get(r['slot'], f'slot_{r["slot"]}')
        rel = ''
        cclass_name = ''
        if r['type'] == 12: # CONTEXT_SWITCH
            rel_slot = r['arg1'] & 0xffff
            prev_prio = (r['arg1'] >> 16) & 0x1f
            next_prio = (r['arg1'] >> 21) & 0x1f
            cclass = (r['arg1'] >> 26) & 0xf
            cclass_name = commit_classes.get(cclass, 'UNKNOWN')
            class_counts[cclass_name] = class_counts.get(cclass_name, 0) + 1
            rel = tasks.get(rel_slot, f'slot_{rel_slot}')
            desc = f'From={rel} (gen={r["arg0"]}) To={primary} (gen={r["gen"]}) Class={cclass_name} Prio({prev_prio}->{next_prio})'
        elif r['type'] == 10:
            desc = f'Task={primary} WakeTick={r["arg0"]}'
        elif r['type'] == 11:
            desc = f'Task={primary} Flags=0x{r["arg0"]:x}'
        elif r['type'] == 4:
            desc = f'Selected={primary} (gen={r["gen"]})'
        elif r['type'] == 2:
            desc = f'Task={primary} ReasonFlags=0x{r["arg0"]:x}'
        elif r['type'] == 3:
            desc = f'Task={primary} WaitReason=0x{r["arg0"]:x}'
        elif r['type'] == 5:
            desc = f'Task={primary} WaitFlags=0x{r["arg0"]:x}'
        else:
            desc = f'Task={primary}'

        decoded.append({
            'seq': r['seq'],
            'tick': r['tick'],
            'event': en,
            'task': primary,
            'related': rel,
            'commit_class': cclass_name,
            'desc': desc,
            'raw': r
        })

    out = {
        'metadata': {
            'seq_64': full_seq,
            'seq_low': seq,
            'seq_hi': seq_hi,
            'overwritten': ovr,
            'count': cnt,
            'write_index': w_idx,
            'status': status
        },
        'event_counts': event_counts,
        'class_counts': class_counts,
        'records': decoded
    }

    (E / 'trace_dump.json').write_text(json.dumps(out, indent=2) + '\n')
    print(f'Trace Dump complete: {len(decoded)} records saved to trace_dump.json')
    print(f'Event counts: {event_counts}')
    print(f'Class counts: {class_counts}')

if __name__ == '__main__':
    main()
