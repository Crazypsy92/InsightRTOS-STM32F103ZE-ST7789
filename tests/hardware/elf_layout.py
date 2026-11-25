"""Run inside gdb-multiarch -batch with the target ELF loaded."""
import gdb
import json

def layout(t):
    t = t.strip_typedefs()
    out = {"size": t.sizeof}
    if t.code == gdb.TYPE_CODE_STRUCT:
        out["fields"] = {f.name: {"offset": f.bitpos // 8, **layout(f.type)}
                         for f in t.fields() if f.name is not None}
    elif t.code == gdb.TYPE_CODE_ARRAY:
        lo, hi = t.range()
        assert lo == 0
        out.update(element=layout(t.target()), count=hi + 1)
    else:
        out["signed"] = t.code == gdb.TYPE_CODE_INT and int(gdb.Value(-1).cast(t)) < 0
    return out

names = """g_display_statistics g_event_tracker g_diag_rt_export g_display_task_refs
            g_sys_tick kernel_ticks g_rtos_assert_line display_stacks idle_stack
            g_stress_request g_stress_remaining g_stress_generated g_job_fixture g_timing_bench g_msp_bench""".split()
out = {}
for name in names:
    try:
        val = gdb.parse_and_eval(name)
    except gdb.error:
        if name.startswith("g_stress_") or name in {"g_job_fixture", "g_timing_bench", "g_msp_bench", "g_diag_rt_export"}:
            continue
        raise
    if name in {"display_stacks", "idle_stack"}:
        out[name] = {"address": int(val.address), "size": val.type.sizeof}
    else:
        out[name] = {"address": int(val.address), **layout(val.type)}
print("A3_LAYOUT " + json.dumps(out))
