#!/usr/bin/env python3
"""Finalize evidence only after real hardware window/reset/user checks complete."""
from pathlib import Path
import hashlib,json,sys
R=Path(__file__).resolve().parents[2];E=Path(__file__).resolve().parent
load=lambda n:json.loads((E/n).read_text())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pending_reset='--pending-reset' in sys.argv
startup=load('startup-context.json');runtime=load('stability30-result.json');post=load('post-stability.json');reset=load('final-reset-observation.json') if (E/'final-reset-observation.json').exists() else {'status':'NOT EXECUTED'};user=load('user-reset-confirmations.json');readback=load('readback_verification.json');key=load('key-verification.json')
assert startup['status']=='PASS' and runtime['status']=='PASS' and runtime['elapsed_s']>=1800
assert post['status']=='PASS'
if not pending_reset:assert reset['status']=='PASS' and user['final_reset']!='PENDING'
assert readback['status']=='PASS' and key['status']=='PASS'
for name,info in load('frozen-inputs.json').items():assert sha(R/name)==info['sha256'],name
samples=runtime['samples'];first,last=samples[0],samples[-1]
assert all(d['state']=='running' and d['assert_line']==0 and d['CFSR']==0 and d['HFSR']==0 and d['key_overflows']==0 and all(g==0xdeadc0de for g in d['guards'].values()) for d in samples)
assert all(d['transport_errors']==0 and d['initialization_errors']==0 and d['ll_direct_recoveries']==0 and d['ll_direct_last_error']==0 for d in samples)
assert (E/'option_bytes_before.bin').read_bytes()==(E/'option_bytes_after.bin').read_bytes()==(E/'option_bytes_final.bin').read_bytes()
assert (E/'readback_after_runtime.bin').read_bytes()==(R/'artifacts/stage8_5_c1/o2/insightrtos_zet6_c1_o2.bin').read_bytes()
def put(n,t):
 if pending_reset and n=='reset_test.md':t='# C2 板载RESET\n\n首次真实RESET启动与随后无断点RESET显示观察均PASS。30分钟后的最后一次板载RESET仍待用户操作/确认：NOT EXECUTED。不使用软件reset替代，不提前声明最终复位通过。\n\nUSER CONFIRMATION REQUIRED：最后一次板载RESET及运行末期显示观感待确认。\n'
 if n=='acceptance.md':
  t+='\n离线C1包验证日志中的HARDWARE NOT EXECUTED是原包验证器的固定范围说明，不表示C2没有执行硬件；C2实机证据以本目录原始记录为准。\n'
  if pending_reset:t=t.replace('完成后停止，不进入新UI开发、驱动修复或其他工程。','本阶段尚待用户最后一次板载RESET及目视确认；其余已执行项目如表，不提前宣布阶段全部完成。')
 (E/n).write_text(t.strip()+'\n')
def table(cols,rows):return '| '+' | '.join(cols)+' |\n|'+'|'.join(['---']*len(cols))+'|\n'+''.join('| '+' | '.join(map(str,x))+' |\n' for x in rows)
rows=[[n,first['task_runs'][i],last['task_runs'][i],runtime['task_run_deltas'][i]] for i,n in enumerate(['KEY','Controller','LCD','Monitor'])]
put('task_runtime.md','''# C2 真机任务与上下文

PASS，源为指定ZET6的实际GDB/SWD证据。首次板载RESET捕获正常Reset_Handler/MSP，原SystemInit/main进入rtos_start/SVC；KEY5、Controller4、LCD2、Monitor1真实入口均捕获，四PSP落在各4096B栈内、CONTROL.SPSEL合法。Idle真实入口及其独立512B PSP通过，另有自由运行current TCB=Idle的直接观测（idle-free-run.json），不把抽样比例称CPU占用率。

三次PendSV对真实R4–R11逐字验证保存和恢复，至少一次实际TCB切换；SVC/PendSV EXC_RETURN=0xFFFFFFFD。SVC0/SysTick8/TIM2 10/PendSV15。VTOR=0按之前批准的Flash别名口径，与0x08000000向量逐字一致。

连续窗口计数：

'''+table(['任务','起点','末点','增量'],rows)+f'''\n实际窗口{runtime['elapsed_s']:.3f}秒。SysTick平均{runtime['tick_hz']:.5f}Hz，TIM2平均{runtime['TIM2_hz']:.5f}Hz；寄存器分别LOAD719999、PSC71/ARR999，HSE/PLL/HCLK72MHz配置通过。KEY在无人输入时阻塞，不把执行次数不增加误判为饥饿；人工输入事件已逐级完整递交。\n\n没有新增固件上下文切换总计数或HardFault累计计数接口，报告的是已捕获切换与期间未观察到故障，不编造全局次数。\n''')
put('spi_dma_runtime.md',f'''# C2 SPI/DMA 运行\n\nPASS（本次五页实际路径未观察错误），长块风险仍OPEN。连续{runtime['elapsed_s']:.3f}秒/{len(samples)}次实际SWD样本：初始化错误、应用传输错误、LL恢复次数、LL last_error均为0。末点逻辑页面刷新={last['lcd_refreshes']}，状态页更新={last['monitor_refreshes']}；期间包含状态页和动画/人工翻页，并非单一页面测试。\n\nSPI3 BR=/2、PCLK1=36MHz得到配置目标18MHz，实际示波器SCK频率NOT MEASURED（peripheral-runtime.json）。DMA2原TX_CH2/RX_CH1、同步等待和错误恢复均未修改，未关闭超时、忽略错误或绕过状态机；没有新烧录/内核修复。\n\n本轮没有65535B长块压力测试，C1已确认的PCLK/CPU预算缺陷仍OPEN；不能从当前<=480B行/字模路径正常推断全部DMA路径通过。原RXNE drain永久异常风险也未修复。\n''')
put('stability_30min.md',f'''# C2 连续30分钟真机验收\n\nPASS。严格窗口{runtime['elapsed_s']:.3f}秒（至少1800秒），起点UTC {first['utc']}，末点UTC {last['utc']}，{len(samples)}个约5秒间隔实际样本。见stability30-samples.jsonl及stability30-result.json。\n\n窗口内没有调试halt/reset/load/寄存器注入，仅在运行状态SWD读取数据；用户进行了已确认的短按、长按及额外翻页，不重启窗口。Controller/LCD/Monitor持续推进，KEY输入得到处理，Idle有独立自由运行直接证据。Tick增量{runtime['deltas']['kernel_ticks']}、TIM2增量{runtime['deltas']['tim2_ms']}，实测平均{runtime['tick_hz']:.5f}Hz/{runtime['TIM2_hz']:.5f}Hz。\n\n全部样本guard完整、关键溢出0、SPI/DMA错误及恢复0、assert_line/CFSR/HFSR为0。未观察到异常停机或计数停滞；无生产HardFault总计数接口，不能把未观察到故障伪称完整硬件故障累计计数。\n\nLCD观感来自用户在起点和末尾的目视确认，未拍摄全程视频；不能声称代理全程自行目视。30分钟后才halt进行栈水位快照，见post-stability.json，随后resume并进行用户板载RESET。\n''')
stackrows=[[n,hex(v['base']),v['bytes'],v['guard_ok'],v['observed_used_from_fill_bytes'],v['untouched_pattern_bytes_excluding_guard']] for n,v in post['stacks'].items()]
put('stack_watermark.md','''# C2 实际栈快照

30分钟完成之后暂停一次，读取实际SRAM并保存各栈BIN。所有活动栈guard均0xDEADC0DE，结果PASS。

'''+table(['栈','base','预留B','guard完整','由填充估计已使用B','未触及填充B(不含guard)'],stackrows)+f'''\nMSP实际值{hex(post['registers']['msp'])}在0x2000F000–0x20010000预算内。冻结固件未预填充MSP，MSP高水位NOT MEASURED；不能将原始RAM内容伪称MSP guard。填充估计是观测值，不是所有未来分支的严格最坏深度证明。\n''')
put('reset_test.md','''# C2 板载RESET

PASS（实际计数复位/重新推进 + USER CONFIRMATION）。首次用户RESET由硬件断点捕获Reset_Handler，验证MSP与启动链；随后第二次无RESET捕获/无GDB停顿的板载RESET用于欢迎和五页观察。

30分钟和栈快照完成后解除halt、GDB断开，用户再次按板载RESET。observe_final_reset.py只读观察，未发送reset/halt/resume或写入；记录kernel_tick/TIM2从旧计数归零并再次增长、LCD ready恢复和guard有效。用户目视结果见user-reset-confirmations.json。OpenOCD保留只读观测连接，未宣称完全断电冷启动或拔除ST-Link启动；该两项NOT EXECUTED。

USER CONFIRMATION REQUIRED：已取得最终复位/屏幕确认，不将GDB continue称为物理RESET。
''')
statuses={'SOFTWARE PACKAGE':'PASS','HARDWARE IDENTITY':'PASS','FLASH ERASE':'PASS pages0–28 only','FLASH PROGRAMMING':'PASS C1 O2 57988B','FLASH READBACK':'PASS independent reconnect + byte/SHA equal','OPTION BYTES':'PASS unchanged','RTOS STARTUP':'PASS hardware','LCD DISPLAY':'PASS USER CONFIRMED','FIVE PAGES':'PASS USER CONFIRMED + live counters','KEY INPUT':'PASS USER CONFIRMED + FIFO counts','SPI/DMA':'PASS observed UI paths; long-block risk OPEN/NOT TESTED','TASK SCHEDULING':'PASS four task entries + Idle + real context switching','STABILITY':'PASS >=30min actual free run','NRST RESET':'PASS physical RESET + user confirmation','SPI3 PHYSICAL SCK':'NOT MEASURED','MSP HIGH WATERMARK':'NOT MEASURED'}
if pending_reset:statuses['NRST RESET']='NOT EXECUTED final reset pending; startup physical resets PASS'
put('acceptance.md','''# InsightRTOS Stage8.5-C2 实机验收

仅使用C1 O2冻结包，长度57988B、SHA256 526890ff96594c65533c69e129a1a889ec2368768b4b4fca3a3c7052eac72e35。指定ST-Link 190057000D0000393333574E及ZET6身份/保护状态与B1.3一致。新增页20–28已只读确认全FF，实际29页×2KiB几何通过，逐页擦除0–28，无Mass Erase/解锁/Option Bytes修改。

'''+table(['项目','最终结果'],list(statuses.items()))+f'''\n独立读回57988B与BIN逐字节及SHA一致，0x0800E284–0x0800E7FF的1404B全FF，Option Bytes操作前/后/最终一致；运行后再次读取Flash仍与冻结BIN一致。仅保存计划范围快照，不伪称512KiB完整备份。\n\n实际连续窗口{runtime['elapsed_s']:.3f}秒，SysTick{runtime['tick_hz']:.5f}Hz、TIM2{runtime['TIM2_hz']:.5f}Hz；全部采样无已观察assert/HardFault/BusFault状态、guard损坏或SPI/DMA错误。LCD五页/欢迎/LED结果为用户确认，代理未自行取得视频/照片。详细计数、栈观测及原始日志见各报告。\n\n首次诊断脚本Thumb指针比较误报已纠正，保留attempt1原始记录；固件、检查目标、内核/驱动均未改动，也未为误报重烧。原长块DMA预算问题仍OPEN，本轮当前UI路径正常不代表所有DMA路径通过。没有CPU使用率、全局切换或HardFault累计计数接口，不伪造数据。\n\n完成后停止，不进入新UI开发、驱动修复或其他工程。\n''')
put('final-status.json',json.dumps({'statuses':statuses,'hardware':True,'stability_seconds':runtime['elapsed_s'],'tick_hz':runtime['tick_hz'],'tim2_hz':runtime['TIM2_hz'],'source_frozen_inputs_preserved':True,'waiting_for_user_final_reset':pending_reset},indent=2))
# Exclude only Python bytecode generated by diagnostic imports.
for p in (E/'__pycache__').glob('*.pyc'):p.unlink()
try:(E/'__pycache__').rmdir()
except OSError:pass
if not pending_reset:(E/'SHA256SUMS').write_text(''.join(sha(p)+'  '+str(p.relative_to(E))+'\n' for p in sorted(E.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
print('C2 completed measurements documented; final physical RESET/user confirmation pending' if pending_reset else 'C2 hardware evidence finalized and sealed')
