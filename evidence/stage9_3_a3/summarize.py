"""Build A3 Markdown tables from completed raw observations."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

evidence = Path(__file__).resolve().parent
root = evidence.parents[1]
logs = evidence / "logs"
build = root / "build/stage9_3_a3_verified"
read = lambda p: json.loads(p.read_text())
rows = {name: read(logs/name/"summary.json") for name in ("normal","stress","burst")}
for label, v in rows.items():
    assert v["status"] == "PASS", label
    assert (label == "normal" or v["stress_disabled"]), label

data = ["# Stage 9.3-A3 实测结果", "",
        "| 实验 | 耗时 s | 有效/无效快照 | Attempt | Accepted | FULL | Context | 错误 ACK | 成功 ACK | Pending 起→终 |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|"]
for name, v in rows.items():
    f = v["flow"]
    data.append(f"| {name} | {v['elapsed_s']:.3f} | {v['valid']}/{v['incomplete']} | {f['key_attempts']} | "
                f"{f['key_sent_ok']} | {f['key_queue_full']} | {f['key_wrong_context']} | {f['failure_count']} | "
                f"{f['success_count']} | {v['start']['tracker']['pending_count']}→{v['end']['tracker']['pending_count']} |")
data += ["", "## 修订前后", "",
         "| 项目 | A2 原报告 | A3 实际证据 |", "|---|---|---|",
         f"| 常态 | 60 s、13 样本 | {rows['normal']['elapsed_s']:.3f} s、{rows['normal']['valid']} 有效样本，Tick {rows['normal']['tick_hz']:.3f} Hz、TIM2 {rows['normal']['tim2_hz']:.3f} Hz |",
         f"| 300 s 压力 | 574 次被称受控丢弃，未保存返回码 | TIM2 ISR 实际 3,000 次，FULL={rows['stress']['flow']['key_queue_full']}，CONTEXT=0，流量两级守恒 |",
         f"| 短时突发 | 旧数据不能证明实机 FULL | {rows['burst']['flow']['key_queue_full']} 次真实 FULL，随后已接受事件排空 |",
         "| RT 字段 | 手算字偏移 | 当前 ELF DWARF + 反例自检 + seqlock 与 freshness 验证 |",
         "",
         "旧 A2 Monitor 普通 Thread 调用了真正的 FromISR wrapper。以生产 App_IPC 和内核 queue.c 在 Host 复现返回 RTOS_ERR_CONTEXT。历史 574 次没有逐次返回码，无法事后逐条归因，旧的 Queue FULL 解释缺乏证据。",
         "", "## READY 延迟及周期份额", "",
         "| 窗口 | 任务 | slot/gen | prio base/effective | dispatch 增量 | DWT 周期增量 | 归属份额 | 最大观测 READY cycles / ticks |",
         "|---|---|---|---|---:|---:|---:|---|"]
for name in ("normal","stress"):
    v = rows[name]
    a,b = [v[k]["rt"]["task_snapshots"]+[v[k]["rt"]["idle_snapshot"]] for k in ("start","end")]
    total = sum(y["charged_cycles"]-x["charged_cycles"] for x,y in zip(a,b))
    for label,x,y in zip(("KEY","CTRL","LCD","MON","Idle"),a,b):
        delta=y["charged_cycles"]-x["charged_cycles"]
        data.append(f"| {name} | {label} | {y['ref']['slot']}/{y['ref']['generation']} | {y['base_priority']}/{y['effective_priority']} | "
                    f"{y['dispatch_count']-x['dispatch_count']:,} | {delta:,} | {delta/total:.3%} | {y['max_ready_latency_cycles']:,} / {y['max_ready_latency_ticks']} |")
    s=[json.loads(line) for line in (logs/name/"samples.jsonl").read_text().splitlines()]
    s=[q for q in s if q["quality"]=="VALID"]
    gaps=[(y["rt"]["published_tick"]-x["rt"]["published_tick"])/100 for x,y in zip(s,s[1:])]
    data += ["", f"{name}：有效发布间隔实测样本中位 {statistics.median(gaps):.3f} s，按 {len(s)} 个有效样本计算。此处为外部采样选中的发布间隔；Monitor 内部可能在两次采样之间多次发布。", ""]
data += ["READY 最大值为本次启动至窗口末的观测高水位。DWT 归属周期含 IRQ 和切换边界开销，份额为近似观测值。",
         "", "## 压力逐分钟边界", "",
         "| 阈值 s | 首个满足阈值的样本 s | 累计 attempt 增量 | FULL |",
         "|---:|---:|---:|---:|"]
stress = rows["stress"]
points = [json.loads(x) for x in (logs/"stress/samples.jsonl").read_text().splitlines()]
for sec in (60,120,180,240,300):
    x = next(p for p in points if p["quality"]=="VALID" and p["monotonic"]-stress["start"]["monotonic"]>=sec)
    data.append(f"| {sec} | {x['monotonic']-stress['start']['monotonic']:.3f} | "
                f"{x['stats']['key_attempts']-stress['start']['stats']['key_attempts']} | "
                f"{x['stats']['key_queue_full']-stress['start']['stats']['key_queue_full']} |")
data += ["", "## 软件回归和资源", "",
         "- 应用 Host 9/9，含实际生产入口、坏上下文、FULL、乱序/重复/未知 ACK 及计数守恒。",
         "- RT Host 9/9，含生产 App_IPC 和内核 FromISR 的旧 Thread 路径复现。内核 ctest 360/360。",
         "- 两个 ARM ELF 通过 DWARF 布局与偏移负例。QEMU **NOT_TESTED**，未运行该板 fixture。",
         "- 运行命令：bash scripts/test/run_host.sh、bash scripts/test/run_rt_host.sh、ctest --test-dir build-host-stage9_2 --output-on-failure；ARM 用 cmake --preset 与 cmake --build --preset。采样及烧录见同目录脚本。",
         "", "| 构建 | Flash B | SRAM 含 MSP B | 剩余 SRAM B |", "|---|---:|---:|---:|"]
prefixes = (("DISPLAY_RTOS_O2","OFF"),("DISPLAY_RTOS_O2_Trace","ON"),("DISPLAY_RTOS_O2_Stress","ON + stress"))
manifest = {"source_base":"04ddcf93a2f0e595fa13389e176ffdbd5685f82f",
            "source_commit":"bb6c0f6ba1ad14bcc5920edd7646892ae2a88cf8",
            "hardware":"STM32F103ZET6 UID 31ffd8053031533628791143",
            "flash_pages":[0,30],"configurations":{}}
for preset, flags in prefixes:
    stem=build/preset/"insightrtos_display"
    nm=subprocess.check_output(["arm-none-eabi-nm",str(stem.with_suffix(".elf"))],text=True)
    ram=int(next(x.split()[0] for x in nm.splitlines() if x.endswith(" __static_ram_end__")),16)-0x20000000+4096
    files={}
    for suffix in (".elf",".bin",".map"):
        p=stem.with_suffix(suffix)
        files[suffix[1:]]={"path":str(p.relative_to(root)), "bytes":p.stat().st_size,
                           "sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
    flash=files["bin"]["bytes"]
    manifest["configurations"][preset]={"diagnostics":flags,"files":files,"flash_bytes":flash,
        "sram_with_msp":ram,"sram_remaining":65536-ram,"msp_bytes":4096,"user_stacks_bytes":32768,"heap_bytes":0}
    data.append(f"| {preset} | {flash:,} | {ram:,} | {65536-ram:,} |")
data += ["", "明确占用以 C3 系列同样的栈预留口径计算。OFF→ON 增量为 Flash 4,240 B、SRAM 4,368 B。O2 Monitor 自身帧 392 B（.su）。逐函数栈与完整链接尺寸见 logs/resource-*.txt。",
         "", "无 HardFault/Lockup、断言或 guard 异常被采样观察到。物理 LCD 观感待用户确认。未开展独立 WCET 或硬实时保证验收。", ""]
(evidence/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
(evidence/"test_results.md").write_text("\n".join(data))
