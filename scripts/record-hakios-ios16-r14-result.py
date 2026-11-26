"""Finalize the consumed R14 run from saved files; never access the phone."""
from datetime import datetime, timezone, timedelta
from pathlib import Path
import re
import shutil

import hakios
from hakios_usb_audit import audit
from hakios_ustacks_log import parse


def fields(line):
    result = dict(re.findall(r"\b([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)", line))
    return {key: int(value) if re.fullmatch(r"-?\d+", value) else value
            for key, value in result.items()}


def main():
    root = hakios.ROOT
    report_path = root / 'reports/hakios-ios16-device-r14.json'
    device = hakios.read(report_path)
    local = root / device['local_log_path']
    raw = (local / 'console.frames').read_bytes()
    console = (local / 'console.log').read_bytes()
    transport = audit(raw, console, local / 'capture-host.py')
    hakios.require(transport['emitted_kernel_stream_through_timer_verified'],
                   'R14 saved emitted stream does not verify through timer/drain')
    text = console.decode(errors='replace')
    lines = text.splitlines()
    args = next(line.removeprefix('LITHIUM BOOTARGS').strip()
                for line in lines if line.startswith('LITHIUM BOOTARGS'))
    cpu = [fields(line) for line in lines if line.startswith('LITHIUM CPU BENCH test=')]
    sanity = [fields(line) for line in lines if line.startswith('LITHIUM CPU sanity ')]
    online = [fields(line) for line in lines if line.startswith('LITHIUM CPU online ')]
    hfs = [fields(line) for line in lines if line.startswith('LITHIUM HFS BENCH test=')]
    hfs_end = next(fields(line) for line in lines if line.startswith('LITHIUM HFS BENCH end '))
    hakios.require(len(cpu) == 6 and len(sanity) == 3 and len(online) == 3,
                   'Missing CPU benchmark/online evidence')
    hakios.require(len(hfs) == 4 and hfs_end['completed'] == 20 and
                   hfs_end['error'] == hfs_end['mismatches'] == hfs_end['cleanup_error'] == 0,
                   'Incomplete or failing HFS benchmark')
    slow = []
    for line in lines:
        if not line.startswith('LITHIUM HFS end '):
            continue
        entry = fields(line)
        ident = entry['id']
        entry['ufs_same_thread'] = next(fields(row) for row in lines
            if row.startswith(f'LITHIUM HFS ufs id={ident} scope=same_thread '))
        entry['ufs_global_window_not_exclusive'] = next(fields(row) for row in lines
            if row.startswith(f'LITHIUM HFS ufs id={ident} scope=global_window '))
        entry['bridge'] = next(fields(row) for row in lines
            if row.startswith(f'LITHIUM HFS bridge id={ident} '))
        entry['stages_inclusive_not_additive'] = [fields(row) for row in lines
            if row.startswith(f'LITHIUM HFS stage id={ident} ')]
        entry['stack_samples'] = [fields(row) for row in lines
            if row.startswith(f'LITHIUM HFS sample id={ident} ')]
        entry['wait_samples'] = [fields(row) for row in lines
            if row.startswith(f'LITHIUM HFS wait id={ident} ')]
        slow.append(entry)
    hakios.require([op['id'] for op in slow] == [1380, 1903, 1962], 'Unexpected slow-op set')
    snapshots = parse(text)
    sb = [fields(line) for line in lines if line.startswith('LITHIUM SB SAMPLE ')]
    ready_sb = [item for item in sb if item.get('stage') == 'constructor_ready']
    progress = {}
    for line in lines:
        if line.startswith('LITHIUM PROGRESS ') and ' op=' in line and ' name=' in line:
            item = fields(line)
            progress[f"{item['pid']}:{item['name']}"] = item
    writer_loss = int(re.findall(r'writer_contention_dropped_bytes=(\d+)', text)[-1])
    fs_loss = fields(next(line for line in reversed(lines) if line.startswith('LITHIUM FS snapshot ')))
    budget = fields(next(line for line in reversed(lines) if line.startswith('LITHIUM HFS budget ')))
    previous = hakios.read(root / 'reports/hakios-ios16-r12-final-diagnosis.json')
    raw_baseline = {name: previous['tests_us'][source] for name, source in
        [('read', 'read_wall'), ('write', 'write_wall'), ('flush', 'flush_wall'),
         ('write_flush', 'write_flush_wall')]}
    photo = Path('C:/Users/Jia33/Documents/xwechat_files/wxid_b5zmjhm8jlaq22_3c0d/temp/RWTemp/2026-11/9e20f478899dc29eb19741386f9343c8/649ee283da1e552176ee3f0b3a32c0f9.jpg')
    dest = local / 'r14-return-original.jpg'
    if dest.exists():
        hakios.require(hakios.sha(dest) == hakios.sha(photo), 'Saved R14 photo differs')
    else:
        shutil.copyfile(photo, dest)
    # The supplied photo was visually read in the conversation. USB byte count
    # and UEFI retained-log byte count describe different capture paths.
    screen = dict(path=dest.relative_to(root).as_posix(), sha256=hakios.sha(dest),
        source='User original photo plus explicit UEFI terminal-state confirmation',
        retained_raw_bytes=783988, return_timer_deadline_reached=True,
        no_panic_text_in_retained_log=True, drain=1, drain_target=652743,
        raw_count_is_not_usb_console_size=True)
    integer = {item['logical']: item for item in cpu if item['test'] == 'integer'}
    copies = {item['logical']: item for item in cpu if item['test'] == 'memcpy'}
    ratios = {str(i): dict(integer_vs_cpu2=integer[i]['avg_us']/integer[2]['avg_us'],
                          memcpy_time_vs_cpu2=copies[i]['avg_us']/copies[2]['avg_us'])
              for i in (0, 1)}
    recorded_at = datetime.now(timezone(timedelta(hours=8))).isoformat()
    next_step = ('Measure per-core actual clock/cache control state using verified MSM8996 sources; '
                 'attribute repeated buf_biowait calls to caller/buffer/B_DONE and CPU with bounded counters; '
                 'correct hfs_systemfile_lock return-mask reporting. Then select one measured correction, '
                 'build/freeze and report before another boot; retain UFS/journal/flush policy.')
    limitations = [
        'CPU MHz and an independent oscillator reference were not measured; CNTFRQ is timer frequency.',
        'PAR/MAIR evidence covers sampled RAM/stack attributes, not every mapping or actual SCTLR/cache state.',
        'Slow ops lack per-op CPU migration history; cannot assign each slow fsync to one measured slow CPU.',
        'All 23 slow-op stack attempts saw TH_RUN (running/runnable), not a safely parked stack; frames=0.',
        'No long lock sleep, timeout completion or missed wakeup demonstrated in these three completed ops; not ruled out globally.',
        'Instrumentation cost has no uninstrumented control and may be significant on the slow CPUs.',
        'Inclusive stage times overlap; global UFS windows include other threads and are not exclusive attribution.',
        'hfs.systemfile_lock errors=2 counts a nonzero acquired-lock bitmask, not an errno or failed lock.',
        'R11 20.549-second rename was not reproduced by the fresh prelaunch workload; no claimed speedup from the bound fix.',
        'Source-side loss and full diagnostic slots prevent a claim of every original log/operation captured.',
        'Single-CPU R14 package is built and checked but unbooted; no SMP A/B runtime conclusion.',
        'UEFI retained RAW BYTES is a separate loader-log stream; it must not be equated with saved USB byte count.',
    ]
    fixed = dict(outcome='normal_timer_return_cpu_asymmetry_and_cpu_heavy_fsync',
        observed_at=recorded_at, actual_boot_args=args, kernel_started=True, loader_started=True,
        launchd_started=True, cpu_online_verified=3, normal_timer_return=True,
        panic_text_observed=False, r13_cpu_index_panic_reproduced=False,
        screen_observation_pending=False, physical_screen_observed=True,
        screen_observation='UEFI normal timer return summary; user confirmed and supplied original photo',
        screen_photo=screen, usb_log_transfer_verified=True,
        usb_kernel_stream_through_timer_verified=True,
        usb_normal_timer_boundary_drain_hardware_verified=True,
        full_runtime_log_captured=False, hardware_verified=False,
        hardware_verification_scope='Kernel, three CPUs and diagnostic workload verified; full system/UI not passed',
        kernel_data_write_hardware_verified=True, data_readback_scope='Same-boot 20-file fixed workload; no cross-boot persistence proof',
        springboard_display_verified=False, physical_touch_verified=False,
        raw_storage_baseline=dict(revision=12, rerun_in_r14=False, tests_us=raw_baseline,
                                  scratch_restored=True, mismatches=0),
        cpu_benchmarks=cpu, cpu_sanity=sanity, cpu_online=online,
        cpu_relative_time_ratios=ratios,
        bootcpu_mapping=dict(argument=2, logical_to_phys={'0':'0x100','1':'0x101','2':'0x1'},
            changed_from_earlier_reported_mapping=False,
            argument_is_not_xnu_logical_id=True, firmware_storage_worker_mpidr='0x0'),
        cpu_mhz_measured=False, cpu_clock_cache_policy_changed=False,
        hfs_benchmarks=hfs, hfs_benchmark_end=hfs_end, slow_operations=slow,
        userland=dict(springboard_samples=len(sb), constructor_ready_samples=len(ready_sb),
            constructor_ready_first=ready_sb[0], constructor_ready_last=ready_sb[-1],
            progress_last=progress,
            ustacks=[dict(sequence=item['sequence'], ended=item['ended'],
                processes=[dict(name=p['name'], pid=p['pid'], threads=p['parsed_threads'],
                                count_matches=p['thread_count_matches']) for p in item['processes']])
                for item in snapshots], desktop_verified=False, ls_deadlock_proven=False),
        source_loss=dict(writer_contention_dropped_bytes=writer_loss,
            fs_snapshot=fs_loss, hfs_budget=budget),
        transport=transport, usb_final_status=hakios.read(local/'usb-status.json'),
        receiver_pipe_error_interpretation='After captured timer boundary and successful drain; not evidence of a panic',
        hfs_bottleneck_final_root_cause_known=False, limitations=limitations,
        repeated_boot=False, host_partition_flash_issued=False, userdata_reinstalled=False,
        journal_policy_changed=False, flush_policy_changed=False,
        single_cpu_r14_booted=False, phone_commands_during_final_analysis=False, next_step=next_step)
    device.update(fixed)
    hakios.write(report_path, device)
    hakios.write(local/'result.json', device)
    hakios.write(root/'reports/hakios-ios16-r14-final-diagnosis.json', dict(revision=14, **fixed))

    baseline_text = ['Historical R12 raw UFS baseline; NOT rerun in R14. Units: microseconds.']
    for name, item in raw_baseline.items():
        baseline_text.append(f"test={name} count={item['count']} errors={item['errors']} min_us={item['min']} avg_us={item['avg']} max_us={item['max']}")
    (local/'raw-storage-baseline-r12.txt').write_text('\n'.join(baseline_text)+'\n', encoding='utf-8')
    (local/'bootargs.txt').write_text(args+'\n', encoding='utf-8')
    (local/'console-first-150.log').write_text('\n'.join(lines[:150])+'\n', encoding='utf-8')
    (local/'console-last-150.log').write_text('\n'.join(lines[-150:])+'\n', encoding='utf-8')
    selected = [line for line in lines if line.startswith(('LITHIUM BOOTARGS', 'LITHIUM CPU ',
        'LITHIUM HFS ', 'LITHIUM LOG ', 'LITHIUM USB:', '--- loader_return_after'))]
    (local/'diagnostic-extract.log').write_text('\n'.join(baseline_text)+
        '\n\nR14 original diagnostic lines (workload labels retain revision=13):\n'+
        '\n'.join(selected)+'\n', encoding='utf-8')

    raw_rows = '\n'.join(f"| {name} | {v['min']} | {v['avg']} | {v['max']} |" for name,v in raw_baseline.items())
    cpu_rows = '\n'.join(f"| {i} | {sanity[i]['mpidr']} | {integer[i]['min_us']}/{integer[i]['avg_us']}/{integer[i]['max_us']} | "
        f"{copies[i]['min_us']}/{copies[i]['avg_us']}/{copies[i]['max_us']} | {copies[i]['MBps_avg']} |" for i in range(3))
    hfs_rows = '\n'.join(f"| {v['test']} | {v['count']} | {v['min_us']} | {v['avg_us']} | {v['max_us']} | {v['errors']} |" for v in hfs)
    slow_rows = '\n'.join(f"| {v['id']} | {v['pid']}/{v['tid']} | {v['elapsed_us']} | {v['cpu_us']} | "
        f"{v['ufs_same_thread']['bridge_us']} | {v['ufs_same_thread']['outside_bridge_us']} | "
        f"{next(s['count'] for s in v['stages_inclusive_not_additive'] if s['name']=='buf.biowait')} |" for v in slow)
    md = f'''# R14 最终诊断（2026-11-18）

## 裸存储结果先列：R12 已有基线

100×4KiB，单位微秒；这组数据来自 R12，R14 没有重复裸设备测试。全部零错误、零数据不一致，scratch 已恢复。

| 测试 | 最小 | 平均 | 最大 |
|---|---:|---:|---:|
{raw_rows}

## 本轮终态与修正范围

R14 仅修正 R13 新诊断的逻辑 CPU 遍历边界，使用 `ml_get_max_cpu_number()` 的包含上限；保留完整 HakiOS 8796.121.2、20F66/D22AP/arm64、UFS、2880MiB、三核及原诊断负载。没有调整时钟、journal、flush，也没有重新刷写 userdata。

临时启动一次，主机接受时间 `{device['started_at']}`；接收器结束 `{fixed['usb_final_status']['finished_at']}`。原始 launchd 启动，三个 XNU CPU 实际运行，正常 timer 返回且 USB `drain=1`。用户确认并提供 UEFI 结束摘要原图：`RETURN TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG`、目标652743。未通过 SpringBoard 桌面显示。本轮原图不能证明运行中曾显示苹果标志。

原图的 `RAW BYTES 783988` 是 UEFI 保留日志计数；USB 控制台是652793字节。它们是独立日志路径，不能直接将差值认作 USB 丢失。

## 实际 boot-args

```text
{args}
```

`bootcpu=2` 下本轮映射仍是 XNU逻辑0→物理0x100、1→0x101、2→0x1，与此前报告一致。不能把参数值2直接解释成 XNU逻辑CPU2。MPIDR0由UEFI存储worker占用。单核对照包已冻结且双包检查通过，但没有启动，因此没有实测 SMP A/B 结论。

## 明确的新异常：各核性能差异

同一整数循环100万次；memcpy每样本32×1MiB，各5个样本，错误0。表中耗时为最小/平均/最大微秒；MB/s 是原日志数值。

| XNU CPU | MPIDR | 整数循环 us | memcpy us | memcpy平均 MB/s |
|---|---|---:|---:|---:|
{cpu_rows}

CPU0/1的整数测试约为CPU2的66.1倍耗时，memcpy约32倍。CPU0/1 MIDR=`0x512f2051`；CPU2 MIDR=`0x512f2011`。这已超出“老芯片所以慢”的解释，但仍不能仅凭循环反推准确MHz、认定某个PLL设置错误或认定所有LS等待都由它导致。

CNTFRQ均为19200000Hz；XNU timebase=625/12ns。MAIR=`0xc08044f00bb44ff`，TCR=`0x22a51a251a`，采样RAM/栈PAR属性为0xff，无转换fault。它们不代表实际SCTLR缓存开关已逐核验证，也没有独立振荡器参考。后续先核实各核缓存控制与真实时钟来源/分频状态。

已只读查看上游 `drivers/power/hakios.cpufreq.c`：该实现明确针对POCO X5 Pro/redwood的EPSS地址。它不能作为MSM8996的现成调频设置直接启用。实际适配应查已验证的本机固件/源码与硬件接口。

## launchd前的固定HFS测试

20×4KiB文件，首个rename创建目标，其余替换；完成20/20，读取校验、清理均成功，整组5205102us。

| 测试 | 次数 | 最小 us | 平均 us | 最大 us | 错误 |
|---|---:|---:|---:|---:|---:|
{hfs_rows}

没有重现R11那次20.549秒rename。这是新临时目录、不同持久Data状态的工作负载，不能宣称修正CPU诊断边界就优化了文件系统。原日志中的 `revision=13` 和临时目录前缀是保留的工作负载标签；实际启动镜像是R14。

## 三个慢fsync：主要时间在UFS桥外

| op ID | PID/TID | 墙钟 us | 线程CPU us | UFS桥 us | 桥外 us | buf_biowait次数 |
|---|---|---:|---:|---:|---:|---:|
{slow_rows}

三个操作均成功返回；每个同线程仅1读、1写、0flush，固件耗时分别194/195/197us，桥接睡眠、记录到的唤醒和超时均为0。1380的全窗口也只有1读1写；1903和1962的全窗口另含其他线程226/468次读，不能全部归给该操作。

1380的4.05秒几乎全部记为线程CPU，UFS桥仅933us；cnode/truncate/global/journal锁阶段都是微秒级。`buf.biowait`却调用31845次、累计918456us；另两次是32453次/211315us和32800次/392679us。重点应转向其调用者、buffer身份、B_DONE与循环处理成本，并区分诊断锁/计时本身的开销。1380的CPU计数略高于墙钟34us，来自起止测量顺序/开销，不能解释为CPU利用率超过100%。

23次慢操作采样都见 `state=0x4`（TH_RUN，运行或可运行），没有安全停驻栈，因而显式跳过、`frames=0`；没有可供符号化的HFS内核栈。采样并未发现长锁睡眠/已武装超时等待，不能据此全局排除丢唤醒。慢op没有记录所在核及迁移，尚不能把某一次fsync直接归到某个慢核。

源码核对还发现诊断语义问题：`hfs_systemfile_lock()` 返回已获得的锁位掩码，非errno；因此该阶段的 `errors=2` 不是两次锁失败。下一修订修正计数解释，不改变锁行为。所有stage是inclusive计时，不能相加解释总耗时。

UFS完成路径为固件MPIDR0轮询mailbox；XNU先自旋至2ms，再按1ms睡眠检查。本轮三个慢fsync没有进入该睡眠分支，没有证据将这几秒归因于mailbox轮询超时。

## 用户空间和日志边界

SpringBoard共有18个样本，其中17个在constructor_ready，提交数0，初始化后的CPU累计不变，同步Mach/XPC栈不变；lsd仍有进展，末尾bundle begin/end=111/111、distinct=111、fail=1，容器调用318次，整体scan/gather/enumerate尚未返回。尚不能认定循环死锁；容器fail计数也不能直接替代逐对象errno诊断。最后free1163MiB、available2494MiB。

两轮用户栈均完整记录6个目标进程：SpringBoard、containermanagerd、backboardd、runningboardd、cfprefsd和lsd。屏幕上的 `denied attempt to escape CPU monitor` 对应lsd尝试调整CPU监控限制被拒绝，单凭它不能认定panic或启动停止。

USB原始762073字节、3415帧，重新解码与652793字节控制台逐字节一致；传输缺口、重复、残帧、device-lost均0，覆盖内核横幅至timer及成功排空。随后Pipe error属于返回后的连接结束。**源端仍丢1697字节，FS快照dropped=80/end_lost=7，HFS记录槽full=481，因此不是所有原始日志和操作无损。** 有界诊断也可能拖慢慢核，尚无关闭诊断后的性能对照。

## 下一修订的判定点

1. 逐核确认实际SCTLR/缓存状态、可用的时钟计数与MSM8996时钟来源/分频读数；先读证据，再决定唯一时钟或缓存修正，不套用redwood寄存器。
2. 对高频buf_biowait只做有界聚合，保留caller地址、buffer身份/标志、已完成比例、实际等待次数及执行核，补足这次所有busy栈跳过的证据缺口；修正systemfile_lock返回语义。
3. 在根因候选有依据后只改一项，保留本轮格式和原始UFS基线，完整构建/检查/冻结再汇报。当前不增加等待时长、不先关闭journal、不重刷userdata，也不机械追加同镜像/单核重跑。

准确MHz、逐op执行核、buf调用者以及诊断开销是下一步主要不确定性；桌面、触摸、GPU、全部6GB仍未通过。

完整日志、原始USB帧、原图、bootargs、前150行/后150行及诊断摘录保存在 `{local.relative_to(root).as_posix()}`。
镜像SHA256：`{device['image_sha256']}`。
'''
    for name in ('hakios-ios16-device-r14.md', 'hakios-ios16-r14-final-diagnosis.md'):
        (root/'reports'/name).write_text(md, encoding='utf-8')
    note = ('R14 最终诊断（2026-11-18）：用户原图确认正常timer返回UEFI、未见panic，drain=1；R13诊断CPU越界未复现。'
        '三核实测CPU0/1(MPIDR0x100/0x101)整数循环平均162097/162071us，CPU2(0x1)2451us，约66倍差异；准确MHz/逐核缓存开关未测。'
        '20轮HFS全部读写校验通过；首个fsync4.05秒中线程CPU约4.05秒、UFS桥933us，buf_biowait31845次；其余两次慢fsync同样主要在桥外。'
        '23次采样均TH_RUN且跳过，无停驻内核栈，最终根因未定。systemfile_lock返回锁掩码，errors=2不代表errno。'
        'USB652793字节/3415帧独立重解码一致、传输缺口0且timer/drain完整；源丢1697字节、FS80/7、HFS槽满481，非全量日志。'
        'SB无提交、lsd不同bundle完成111，桌面未通过。保持UFS/2880MiB/三核，不重试R14、不重刷userdata；'
        '下一修订先核实本机时钟/缓存及buf调用者/执行核，不能直接启用上游redwood EPSS地址。见reports/hakios-ios16-r14-final-diagnosis.md/json。')
    for name in ('AGENTS.md', 'PLAN.md', 'OPERATIONS.md'):
        path = root/name
        old = path.read_text(encoding='utf-8')
        if note not in old:
            path.write_text(note+'\n\n'+old, encoding='utf-8')
    state = hakios.read(root/'project.json')
    state['work_status'].update(status='ios16_phase4_r14_normal_return_cpu_asymmetry',
        latest_evidence_recorded_at=recorded_at, next_step=next_step)
    state['device_state'].update(current_physical_state='R14 normal timer return summary retained in UEFI',
        current_physical_state_source='User confirmation and original R14 return photo',
        last_screen_observation='UEFI normal timer return; no panic text in retained log',
        last_screen_observation_source=screen['path'], last_observation_baseline='ios16_r14_full_hakios',
        latest_device_test_report='reports/hakios-ios16-device-r14.json',
        userdata_hfs_mount_verified=True)
    state['active_kernel'].update(hardware_started=True, loader_started=True,
        platform_uefi_verified=True, latest_kernel_binary_hardware_verified=True,
        latest_kernel_binary_verification_scope='R14 kernel boot, three CPU diagnostics and HFS workload; not full hardware/UI',
        hardware_evidence_report='reports/hakios-ios16-device-r14.json',
        status='R14 normal timer return; per-core performance anomaly, CPU-heavy fsync; desktop not passed')
    state.update(stage='iOS16 phase4 R14 analyzed; clock/cache and buffer callers next',
        current_report='reports/hakios-ios16-r14-final-diagnosis.json',
        current_device_status='R14 normal return summary in UEFI; no further phone operations')
    hakios.write(root/'project.json', state)
    print('Recorded R14 normal return, original photo, per-core/HFS metrics and independent USB audit; no phone commands')


if __name__ == '__main__':
    main()
