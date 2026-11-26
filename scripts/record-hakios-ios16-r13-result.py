"""Record the consumed R13 run from saved USB and the user's panic photo."""
from datetime import datetime
from pathlib import Path
import shutil
import re
import hakios
from hakios_usb_audit import audit

def main():
    report_path=hakios.ROOT/'reports/hakios-ios16-device-r13.json'
    d=hakios.read(report_path);local=hakios.ROOT/d['local_log_path']
    raw=(local/'console.frames').read_bytes();console=(local/'console.log').read_bytes()
    transport=audit(raw,console,local/'capture-host.py')
    photo=Path('C:/Users/Jia33/Documents/xwechat_files/wxid_b5zmjhm8jlaq22_3c0d/temp/RWTemp/2026-11/9e20f478899dc29eb19741386f9343c8/47a609dc15298e9aa9356e22e2e77374.jpg')
    dest=local/'r13-panic-original.jpg'
    if dest.exists():hakios.require(hakios.sha(dest)==hakios.sha(photo),'Saved photo differs')
    else:shutil.copyfile(photo,dest)
    text=console.decode(errors='replace');lines=text.splitlines()
    args=next(l.removeprefix('LITHIUM BOOTARGS').strip() for l in lines if l.startswith('LITHIUM BOOTARGS'))
    bench=[l for l in lines if l.startswith('LITHIUM CPU BENCH test=')]
    sanity=[l for l in lines if l.startswith('LITHIUM CPU sanity ')]
    fixed=dict(outcome='panic_returned_to_uefi',observed_at=datetime.now().astimezone().isoformat(),
        actual_boot_args=args,usb_log_transfer_verified=True,full_runtime_log_captured=False,
        hardware_verified=False,kernel_started=True,loader_started=True,cpu_online_verified=3,
        screen_observation_pending=False,screen_observation='User photo: UEFI retained panic summary',
        normal_timer_return=False,panic_verified=True,launchd_started=False,hfs_benchmark_started=False,
        springboard_display_verified=False,physical_touch_verified=False,
        diagnostic_failure=dict(file='osfmk/arm/cpu_common.c',line=82,
            assertion='cpu <= ml_get_max_cpu_number()',panic_cpu=2,pid=1,process='init',tid=168,
            observed_retained_raw_bytes=43054,exact_full_panic_backtrace_recovered=False,
            cause='R13 CPU diagnostic queries cpu_to_processor(3) outside three-CPU logical range 0..2',
            evidence='Source plus frozen _lithium_cpu_sanity/_cpu_to_processor disassembly; matching photographed assertion',
            hfs_bottleneck_root_cause=False,one_cpu_variant_has_same_bug=True),
        cpu_benchmark_lines=bench,cpu_sanity_lines=sanity,transport=transport,
        usb_final_status=hakios.read(local/'usb-status.json'),
        panic_photo=dict(path=dest.relative_to(hakios.ROOT).as_posix(),sha256=hakios.sha(dest)),
        repeated_boot=False,host_partition_flash_issued=False,journal_policy_changed=False,
        flush_policy_changed=False,next_step='R14: bound diagnostic iteration by XNU logical CPU maximum; retain original HFS/CPU workload and three-CPU policy')
    d.update(fixed);hakios.write(report_path,d);hakios.write(local/'result.json',d)
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r13-final-diagnosis.json',dict(revision=13,**fixed))
    md=f'''# R13 device result: CPU diagnostic boundary panic

The user photo confirms panic return to UEFI, not the normal 600-second timer. The assertion is **`cpu <= ml_get_max_cpu_number()` at `osfmk/arm/cpu_common.c:82`**, panic CPU2, PID1/init, TID168. The photo reports 43054 retained raw bytes; the complete panic body is not in USB capture. The short UEFI backtrace does not recover a full diagnostic call chain.

R13 diagnostic source enumerates `i=0..3`, calling `cpu_to_processor(i)` before checking the returned pointer. Three online XNU CPUs permit logical IDs **0..2**. The API calls `cpu_datap`, which asserts the inclusive maximum before it can return a pointer. Frozen executable disassembly confirms the loop limit4 and call, and the assertion branch with source line82. The remaining physical CPU is a UEFI storage worker, not an XNU CPU. The unbooted one-CPU R13 variant would also query outside its logical range; do not run it unchanged.

The defect is in the new diagnostic, not proof of an iOS16/HFS/UFS failure. No launchd start or HFS benchmark start was captured. Fix only the diagnostic logical bound for R14, keeping CPU count, clocks, RAM, HFS journal and flush semantics.

## Raw storage first (previous R12)

R12 100×4KB: read min196/avg207/max720us; write min134/avg146/max385us; flush min246/avg312/max552us; write+flush min387/avg465/max728us. Zero errors/mismatches, scratch restored. No new R13 raw-device test was run.

## Actual R13 boot arguments

```text
{args}
```

## Captured CPU results

```text
{chr(10).join(bench)}
```

All three actual logical IDs/MPIDRs were captured: 0→0x80000100, 1→0x80000101, 2→0x80000001. CPU2 benchmark completion was not captured. CNTFRQ is19200000Hz; XNU timebase625/12ns. Sampled allocated RAM/stack PAR attribute0xff is Normal cacheable with no translation fault. CPU MHz, instruction-cache enablement and an independent oscillator reference were not measured. The low observed memcpy throughput warrants investigation after the diagnostic finishes, but cannot establish the frequency defect by itself.

## Log scope and retained evidence

USB {len(console)}bytes/{transport['frames']}frames independently redecoded byte-for-byte; gap/duplicate/partial/device-lost counters all0. This verifies **only the captured prefix**, not the complete run. Receiver ended at {fixed['usb_final_status']['finished_at']} with Pipe error; no normal timer/drain record. UEFI photo establishes panic terminal state independently.

Raw frames, console, original photo and command records are retained in `{d['local_log_path']}`. Image SHA256 `{d['image_sha256']}`. No unchanged-image retry, userdata reflash, clock policy or filesystem strategy change was made.
'''
    for name in ('hakios-ios16-device-r13.md','hakios-ios16-r13-final-diagnosis.md'):
        (hakios.ROOT/'reports'/name).write_text(md,encoding='utf-8')
    note=('R13 真机故障（2026-11-18）：用户原图确认panic返回UEFI，cpu_common.c:82断言cpu <= ml_get_max_cpu_number()，CPU2/PID1 init。新增CPU诊断固定查询0..3，但三核XNU仅0..2；源码及冻结反汇编确认越界契约缺陷，单核R13同样不可运行。USB9952字节/44帧独立重解码一致，缺口/重复/残帧/device lost均0，仅前缀完整，未捕获panic全文/timer/drain；尚未进入launchd/HFS测试。保持UFS/2880MiB/三核，不重试R13、不重刷userdata。下一修订仅修正诊断CPU遍历边界；HFS瓶颈根因未定。见reports/hakios-ios16-r13-final-diagnosis.md/json。')
    for name in ('AGENTS.md','PLAN.md','OPERATIONS.md'):
        p=hakios.ROOT/name;t=p.read_text(encoding='utf-8')
        if note not in t:p.write_text(note+'\n\n'+t,encoding='utf-8')
    state=hakios.read(hakios.ROOT/'project.json')
    state['work_status'].update(status='ios16_phase4_r13_panic_diagnostic_cpu_index',
        next_step=fixed['next_step'])
    state['device_state'].update(current_physical_state='R13 panic summary retained in UEFI',
        current_physical_state_source='User supplied original photo',latest_device_test_report='reports/hakios-ios16-device-r13.json')
    state['active_kernel'].update(hardware_started=True,loader_started=True,
        latest_kernel_binary_hardware_verified=False,status='R13 CPU diagnostic boundary panic; HFS/launchd not reached')
    state.update(stage='iOS16 phase4 R13 panic diagnosed',current_report='reports/hakios-ios16-r13-final-diagnosis.json',
        current_device_status='R13 returned to UEFI after diagnostic CPU-index panic')
    hakios.write(hakios.ROOT/'project.json',state)
    print('Recorded R13 panic, prefix-only USB audit and user original photo; no phone commands')

if __name__=='__main__':main()
