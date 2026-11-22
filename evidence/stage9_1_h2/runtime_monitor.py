#!/usr/bin/env python3
"""Read-only live SWD sampling for Stage 9.1-H2."""
from pathlib import Path
import socket, time, json, struct, sys, traceback

E = Path(__file__).resolve().parent
S = json.loads((E / 'runtime-schema.json').read_text())

class Tcl:
    def __init__(self):
        self.s = socket.create_connection(('127.0.0.1', 6669), 10)
        self.s.settimeout(20)

    def cmd(self, c):
        self.s.sendall(c.encode() + b'\x1a')
        b = b''
        while b'\x1a' not in b:
            v = self.s.recv(65536)
            if not v: raise RuntimeError('Tcl closed')
            b += v
        v = b.split(b'\x1a')[0].decode().strip()
        if any(x in v.lower() for x in ['error:', 'invalid command', 'failed to read']):
            raise RuntimeError(v)
        return v

    def rd(self, a, n=1):
        aligned = a & ~3
        vals = [int(v, 0) for v in self.cmd(f'read_memory 0x{aligned:x} 32 {n}').split()]
        return vals

    def word(self, a):
        aligned = a & ~3
        shift = (a & 3) * 8
        val = self.rd(aligned, 1)[0]
        if shift:
            val = (val >> shift)
            # if we read across a boundary we might need next word, but for 32-bit aligned symbols:
        return val

    def byte(self, a):
        aligned = a & ~3
        shift = (a & 3) * 8
        val = self.rd(aligned, 1)[0]
        return (val >> shift) & 0xFF

    def close(self):
        self.s.close()

def require(ok, message):
    if not ok: raise RuntimeError(message)

def sample(t):
    begin = time.monotonic()
    raw = b''.join(struct.pack('<I', n) for n in t.rd(S['statistics']['address'], S['statistics']['bytes'] // 4))
    d = {}
    for n, v in S['statistics']['fields'].items():
        data = raw[v['offset']:v['offset'] + v['bytes']]
        if n == 'snapshots':
            d[n] = []
            for i in range(4):
                item = data[i * S['snapshot_bytes']:(i + 1) * S['snapshot_bytes']]
                obj = {k: int.from_bytes(item[x['offset']:x['offset'] + x['bytes']], 'little')
                       for k, x in S['snapshot_layout'].items()}
                obj.update(slot=int.from_bytes(item[:4], 'little'),
                           generation=int.from_bytes(item[4:8], 'little'))
                d[n].append(obj)
        elif v['bytes'] == 4:
            d[n] = int.from_bytes(data, 'little', signed=n in ['last_lcd_error', 'll_last_error'])
        else:
            d[n] = list(struct.unpack('<' + 'I' * (len(data) // 4), data))

    d['kernel_ticks'] = t.word(S['symbols']['kernel_ticks'])
    d['tim2_ms'] = t.word(S['symbols']['g_tim2_ms_count'])
    d['current_tcb'] = t.word(S['symbols']['rtos_current_task'])
    d['assert_line'] = t.word(S['symbols']['g_rtos_assert_line'])
    faults = t.rd(0xe000ed28, 2)
    d['CFSR'], d['HFSR'] = faults
    d['guards'] = {n: t.word(v['address']) for n, v in S['stacks'].items()}
    d['ll_direct_recoveries'] = t.word(S['symbols']['g_ll_recover_count'])
    d['ll_direct_last_error'] = t.word(S['symbols']['g_ll_last_error'])

    # Trace diagnostics
    d['s_page'] = t.byte(S['symbols']['s_page'])
    d['trace_status'] = t.word(S['symbols']['trace_status'])
    d['trace_next_sequence_high'] = t.word(S['symbols']['trace_next_sequence_high'])
    d['trace_next_sequence'] = t.word(S['symbols']['trace_next_sequence'])
    d['trace_overwritten'] = t.word(S['symbols']['trace_overwritten'])
    d['trace_count'] = t.word(S['symbols']['trace_count'])
    d['trace_write_index'] = t.word(S['symbols']['trace_write_index'])

    # Compute 64-bit full sequence
    d['seq_64'] = (d['trace_next_sequence_high'] << 32) | d['trace_next_sequence']

    dhcsr = t.word(0xe000edf0)
    d['DHCSR'] = dhcsr
    d['state'] = 'halted' if dhcsr & (1 << 17) else 'lockup' if dhcsr & (1 << 19) else 'running'
    d['monotonic'] = time.monotonic()
    d['sample_read_seconds'] = d['monotonic'] - begin
    d['utc'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    return d

def safe(d, strict):
    require(d['state'] == 'running', 'Unexpected target halt')
    require(d['assert_line'] == 0 and d['CFSR'] == 0 and d['HFSR'] == 0, 'Observed assert or fault')
    require(d['display_fault'] == 0, 'LCD terminal initialization fault')
    if strict or d['ready'] != 0:
        require(all(v == 0xdeadc0de for v in d['guards'].values()), 'Stack guard damaged')

def main():
    label = sys.argv[1]
    duration = float(sys.argv[2])
    strict = len(sys.argv) > 3 and sys.argv[3] == 'strict'
    t = Tcl()
    out = {
        'label': label,
        'hardware': True,
        'status': 'RUNNING',
        'strict_uninterrupted_window': strict,
        'debug_halts_during_window': False,
        'samples': []
    }
    try:
        first = sample(t)
        safe(first, strict)
        out['samples'].append(first)
        begin = first['monotonic']
        deadline = begin + duration
        with (E / (label + '-samples.jsonl')).open('w') as f:
            f.write(json.dumps(first) + '\n')
            f.flush()
            while time.monotonic() < deadline:
                time.sleep(min(2, max(0.1, deadline - time.monotonic())))
                d = sample(t)
                out['samples'].append(d)
                f.write(json.dumps(d) + '\n')
                f.flush()
                safe(d, strict)
        out['status'] = 'PASS'
        out['total_samples'] = len(out['samples'])
    except Exception as ex:
        out['status'] = 'FAIL'
        out['error'] = str(ex)
        print(traceback.format_exc())
    finally:
        t.close()
        (E / (label + '-result.json')).write_text(json.dumps(out, indent=2) + '\n')
        print(f"MONITOR_{label}: {out['status']} samples={len(out['samples'])}")

if __name__ == '__main__':
    main()
