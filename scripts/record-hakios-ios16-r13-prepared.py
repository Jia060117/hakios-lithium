"""Record host-only R13 delivery and preserve all preceding device evidence."""
from datetime import datetime
from pathlib import Path
import hakios

def main():
    root=hakios.ROOT;pair=hakios.read(root/'reports/hakios-ios16-r13-pair-checks.json')
    spec=hakios.read(hakios.MANIFEST);current=hakios.inputs(spec)
    hakios.require(pair['passed'] and pair['inputs']==current,'Current paired package check required')
    contracts=[]
    for suffix in ('','-cpus1'):
        c=hakios.read(root/('artifacts/hakios/lithium-ios16/phase4-r13'+suffix+'/contract.json'))
        hakios.require(c['inputs']==current and hakios.sha(root/c['image_path'])==c['image_sha256'],'Frozen R13 package changed')
        for p,d in c['evidence_sha256'].items():hakios.require(hakios.sha(root/p)==d,'Frozen evidence changed: '+p)
        contracts.append(c)
    diag=hakios.read(root/'ports/hakios-lithium/diagnostics/r13.json')
    raw=hakios.read(root/'reports/hakios-ios16-r12-final-diagnosis.json')
    checks=hakios.read(root/'reports/hakios-ios16-r13-checks.json')
    proof_files=['reports/hakios-ios16-r13-checks.json','reports/hakios-ios16-r13-pair-checks.json',
        'reports/hakios-kernel-build.json','reports/hakios-support-build.json','reports/hakios-ios16-phase4-userspace-build.json',
        'reports/hakios-ios16-phase4-build-checks.json','reports/hakios-ios16-package-host-checks.json',
        'scripts/symbolicate-hakios-ios16-r13.py','scripts/record-hakios-ios16-r13-prepared.py']
    result=dict(revision=13,prepared_at=datetime.now().astimezone().isoformat(),status='host_prepared_not_device_tested',
        passed=True,inputs=current,kernel_sha256=pair['kernel_sha256'],variants=pair['variants'],
        complete_hakios_baseline=hakios.PIN,xnu_version='8796.121.2',userspace='iOS16.5/20F66/D22AP/arm64',
        raw_storage_previous_run=dict(read_avg_us=207,write_avg_us=146,flush_avg_us=312,write_flush_avg_us=465,
            errors=0,mismatches=0,report='reports/hakios-ios16-r12-final-diagnosis.md'),
        bootcpu_mapping=dict(bootcpu=2,xnu_logical_bootcpu=0,mpidr_affinity='0x100',
            three_cpu_affinities=['0x100','0x101','0x1'],changed_vs_r11_r12=False,
            evidence_logs=['reports/local/20261117-211346-406347-ios16-r11/console.log',
                'reports/local/20261117-230916-917288-ios16-r12/console.log']),
        diagnostics=dict(threshold_us=500000,requested_sample_interval_us=250000,per_op_samples=24,
            total_samples=512,per_callback_samples=2,slow_summaries=128,active_records=64,frames=16,
            parked_thread_stacks_only=True,lock_owner_tid_only_for_record_pinned_threads=True,
            asynchronous_io_attribution='same-thread plus non-exclusive whole-window counters',
            journal_stages_inclusive_not_additive=True,prelaunch_cpu_samples=5,
            prelaunch_hfs_rounds=20,prelaunch_hfs_bytes=4096,hfs_stop_after_completed_op_seconds=120,
            hardware_timer_reference_independent=False,cpu_frequency_measured=False),
        backup=dict(branch=diag['backup_branch'],path=diag['backup_path'],sha256=diag['backup_sha256']),
        resources=hakios.resources(),kernel_memory_mib=2880,resident_uefi_cpus=1,
        original_bodies_and_lock_order_preserved=checks['original_bodies_and_lock_order_preserved'],
        hardware_verified=False,root_cause_fixed=False,springboard_verified=False,phone_query_performed=False,
        phone_boot_performed=False,phone_flash_performed=False,userdata_reinstall_required=False,
        journal_policy_changed=False,flush_policy_changed=False,clock_policy_changed=False,
        same_boot_hotplug=False,comparison_requires_separate_boots=True,private_images_shareable=False,
        evidence_sha256={p:hakios.sha(root/p) for p in proof_files})
    hakios.write(root/'reports/hakios-ios16-r13-prepared.json',result)
    one,three=contracts[1],contracts[0]
    md=f'''# R13 HFS / CPU diagnostic delivery

R12 raw storage is healthy: 4KB read **207 us**, write **146 us**, flush **312 us**, write+flush **465 us** average, zero errors or mismatches. Those are existing phone measurements, not new R13 measurements. They select the HFS/VFS/journal/wait investigation branch; concurrent UFS queueing still needs the per-operation data.

R13 has been fully built and checked on the host. **No phone query, boot or flash was issued in this revision.** The bottleneck and SpringBoard remain unverified. Complete HakiOS `{hakios.PIN}` / XNU8796.121.2 and supplied iOS16.5/20F66/D22AP/arm64 remain the active base. Data stays on UFS with 2880MiB XNU RAM and one physical CPU reserved for UEFI storage service.

## Inputs and checks

- Preserved the R12 branch `{diag['backup_branch']}` and external dirty-source archive; SHA256 `{diag['backup_sha256']}`. Existing uncommitted R12 work is preserved.
- Full upstream kernel, D22AP loader, Mu firmware and **413 launchd jobs** built. The actual binaries passed **46 instruction scans**, the frozen package passed **16 package checks**, and both CPU variants passed actual boot-container roundtrips.
- Production recording and stack-bound logic passed ASan/UBSan, including **8000 concurrent operations**, nested timing, thread references, UFS attribution and wake/timeout recording. The actual HFS workload also passed nominal content checks and injected write/fsync/rename/corruption/timeout failures with temporary-directory cleanup.
- Before/after reconstruction verified preserved HFS bodies, original UFS request logic and lock order. No journal, mount, flush, cache or CPU-clock policy was changed.
- Compiler integration fixes: match original HFS function types (`hfs_lock_truncate` is void; `journal_flush` uses its upstream option type; `buf_biowait` returns errno_t); place the sampler in the existing private XNU diagnostic module; use libmini's existing static-init syscalls rather than unavailable libSystem APIs. The file test uses gettimeofday at microsecond resolution; kernel timing remains monotonic.

## What the new log can establish

`LITHIUM HFS end` reports PID/TID/op, wall, thread CPU and non-CPU time. `ufs` and `bridge` report same-thread read/write/flush counts, request/firmware/sleep time, whole bridge time and time outside it. Global-window deltas include other threads and are explicitly non-exclusive.

`LITHIUM HFS stage` times cnode/truncate/global/systemfile locks, Catalog rename, HFS transactions, journal mutex/start/end/flush/I/O and buf_biowait/flushdirty. These nested inclusive times must **not** be added together. The original mutex/rw-lock and flushing semantics are retained.

After 500ms, a normal deferred observer requests samples at 250ms intervals, bounded to 24 per operation, 512 total and two operations per callback. It safely reads **parked, non-running threads only**. Busy/on-core or missing-stack cases emit `skipped` instead of a stale stack. Intervals are a target, not a guarantee under load; scheduling and console work can delay them. Operation totals still account for CPU-heavy cases. Paths are bounded component names, not full paths; correlate PID/TID/time with existing `LITHIUM FS` records.

The wait record contains the observed wait event, armed timer and soft deadline, the separately recorded sleep channel/message/deadline, and whether the events match. A lock-owner TID is resolved only when that thread is pinned by an active/completed diagnostic record. `owner_known=0` does not establish an owner or a deadlock. Wake/timeout counts and the CPU that executed the last unblock are recorded. Repeated timeouts are evidence to investigate, not proof of a missed wakeup.

UFS completion is already known from source: the physical MPIDR0 UEFI worker polls the mailbox and performs EFI BlockIO; XNU spins for 2ms then polls with IOSleep(1). It does not use an interrupt completion callback. The new record separates firmware service time from request wait and lock acquisition.

Before launchd, the CPU test binds the calling thread to each available online XNU CPU. It records actual logical CPU/MPIDR, CNTFRQ, XNU timebase, MAIR, TCR and AT/PAR attributes for the allocated RAM and stack. Five integer/memcpy samples produce min/avg/max, ns per iteration and MB/s. This reveals per-core differences and effective cache attributes; it **does not measure CPU MHz or independently validate the oscillator**. Compare with an independent Android/clock reference before asserting an absolute frequency defect.

The file workload runs after a successful Data HFS mount and before launchd: 20 verified 4KB write/fsync/rename/read rounds in a new temporary directory. The first rename creates the destination; 19 replace it. Only this temporary directory is removed. A 120s limit is checked after completed syscalls and cannot interrupt a hung syscall. The normal kernel timer still terminates the run at 600s.

## CPU mapping and A/B limits

Existing R11 and R12 logs both show `bootcpu=2`, XNU logical CPU0 → MPIDR affinity `0x100`, CPU1 → `0x101`, CPU2 → `0x1`. Raw MPIDRs include the high UP bit (`0x80000100`, `0x80000101`, `0x80000001`). Thus bootcpu=2 causes **no new mapping change versus those earlier runs**. The remaining affinity0 CPU stays in UEFI.

`cpus=` takes effect during boot, so this A/B requires **two separate boots**, not same-boot hotplug. Payload comparison proved all kernel/loader/firmware/userland bytes and all other arguments, including epoch, identical. Data persists and LaunchServices advances during each run; compare the fixed prelaunch workload first. Later LS throughput is not a controlled SMP result unless its initial database state is also matched.

## Images and exact payload arguments

Three CPUs: `{three['image_path']}` — {three['image_bytes']} bytes.

SHA256: `{three['image_sha256']}`

```text
{three['cmdline']}
```

One CPU: `{one['image_path']}` — {one['image_bytes']} bytes.

SHA256: `{one['image_sha256']}`

```text
{one['cmdline']}
```

These are exact frozen **payload arguments**. At runtime the loader adds its prefix and dynamically allocated shared-page address. The CPU test outputs the complete actual `LITHIUM BOOTARGS` line; include that line with each returned log. Do not treat the reserved all-zero address in a host command-budget record as a measured phone address.

Private frozen kernel: `artifacts/hakios/lithium-ios16/phase4-r13/hakios-kernel`, SHA256 `{three['kernel_sha256']}`. Images are Git-ignored and contain private supplied firmware/userland; keep them local.

## Manual run — user performs both boots

In one PowerShell window, start the verified existing receiver (it begins querying USB only when **you** run this command):

```powershell
Set-Location F:\\IOS-MIX1
& 'E:\\IOS-MIX1-artifacts\\host-usb-venv\\Scripts\\python.exe' scripts/capture-hakios-usb.py --output reports/local/r13-manual-cpus3/console.log --status reports/local/r13-manual-cpus3/status.json --wait 180 --duration 900
```

With the phone in Fastboot, run the three-CPU image in a second window:

```powershell
& 'C:\\platform-tools\\fastboot.exe' boot 'F:\\IOS-MIX1\\artifacts\\hakios\\lithium-ios16\\phase4-r13\\lithium-hakios-ios16-r13.img'
```

After the normal 10-minute return and log drain, use the physical keys to return to Fastboot. Start another receiver using `reports/local/r13-manual-cpus1/console.log` and its corresponding status path, then run:

```powershell
& 'C:\\platform-tools\\fastboot.exe' boot 'F:\\IOS-MIX1\\artifacts\\hakios\\lithium-ios16\\phase4-r13-cpus1\\lithium-hakios-ios16-r13.img'
```

These are temporary boot commands; neither writes a boot partition or reinstalls userdata. Data writes made by the running filesystem remain enabled. Receiver output creation is exclusive: use a new directory if a previous log exists. No screen watch is required. An eventual USB Pipe error after the normal timer/drain boundary is distinct from a kernel failure; validate the saved frames and source loss counters.

Extract the useful records after each run:

```powershell
Select-String -Path reports/local/r13-manual-cpus3/console.log -Pattern 'LITHIUM BOOTARGS|LITHIUM CPU|LITHIUM HFS|LITHIUM FS|LITHIUM PROGRESS|expired|drain|dropped|panic'
python scripts/symbolicate-hakios-ios16-r13.py --log reports/local/r13-manual-cpus3/console.log
```

The symbolizer reads only the frozen executable Mach-O symbols and the saved log. The loader uses fixed kernel virtual addresses; `--slide` means a **measured virtual KASLR slide**, not the loader's physical-minus-virtual translation offset. Host checks resolved `_hfs_vnop_renamex` and `_cat_rename` from this actual kernel.

## Decision after the logs

1. Compare raw R12 numbers, per-core CPU/cache/timer records, then the fixed HFS min/avg/max and completion/errors.
2. For a slow rename/fsync, correlate its `id`, wait samples and stage totals. Large bridge/request/lock time points to UFS serialization or polling under load; small bridge time with large buf/journal or HFS-lock time points above the device.
3. Large operation thread-CPU time with small non-CPU time favors computation/cache/clock speed. Small CPU time plus stable waits favors a completion/dependency/lock wait. A single snapshot or unknown owner cannot prove a circular deadlock.
4. Select one measured correction, preserving the same comparison format. SpringBoard, full 6GB exposure, touch and GPU remain separate unverified outcomes.
'''
    (root/'reports/hakios-ios16-r13-prepared.md').write_text(md,encoding='utf-8',newline='\n')
    note='R13 主机准备完成（2026-11-18）：完整 HakiOS/iOS16.5、UFS/2880MiB；冻结同内核 cpus=3/cpus=1 两包，只有CPU参数不同。500ms慢HFS操作有界取停驻线程栈、真实wait/timer、锁/事务/flush分段、同线程及非独占全窗口UFS计数；逐在线核CPU/cache/timebase诊断，launchd前20轮固定文件测试。完整内核/Mu/D22AP/413任务、ASan/UBSan及既有回归、46项指令/16项包检查/双包比较通过。本轮未查询、启动或刷写手机，根因/桌面未验证。busy线程跳过取栈、未知owner不推断、inclusive时间不可相加；cpus A/B需分两次启动且Data持久进度影响LS对比。未重刷userdata或改journal/flush策略。见reports/hakios-ios16-r13-prepared.md/json。'
    for name in ('AGENTS.md','PLAN.md','OPERATIONS.md'):
        p=root/name;text=p.read_text(encoding='utf-8')
        if not text.startswith('R13 主机准备完成'):p.write_text(note+'\n\n'+text,encoding='utf-8',newline='\n')
    state=hakios.read(root/'project.json')
    state['stage']='iOS16 R13 host diagnostics frozen; user-operated CPU A/B boots pending'
    state['active_kernel'].update(status='R13 complete HakiOS diagnostic build prepared; not device tested',
        kernel_sha256=pair['kernel_sha256'],latest_kernel_binary_hardware_verified=False)
    state['ios16_r13']=dict(host_prepared=True,prepared_report='reports/hakios-ios16-r13-prepared.json',
        phone_query_performed=False,boot_issued=False,flash_issued=False,variants=pair['variants'],
        kernel_memory_mib=2880,springboard_verified=False,root_cause_fixed=False)
    hakios.write(root/'project.json',state)
    print('Recorded R13 delivery, current host state and preserved device history')

if __name__=='__main__':main()
