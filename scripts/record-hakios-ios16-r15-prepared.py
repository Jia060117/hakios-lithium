"""Record the frozen Run A delivery without changing package evidence."""
from datetime import datetime
import hakios


def main():
    root = hakios.ROOT
    ready = hakios.read(root / 'reports/hakios-ios16-boot-ready.json')
    hakios.require(ready['revision'] == 15 and ready['passed'], 'Frozen R15 required')
    hakios.require(ready['inputs'] == hakios.inputs(hakios.read(hakios.MANIFEST)), 'Inputs changed')
    hakios.require(hakios.read(root / ready['frozen_contract_path']) == ready, 'Contract changed')
    hakios.require(hakios.sha(root / ready['image_path']) == ready['image_sha256'], 'Image changed')
    for path, digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(root / path) == digest, 'Evidence changed: ' + path)
    proofs = [
        'reports/hakios-ios16-r15-checks.json',
        'reports/hakios-ios16-r15-package-checks.json',
        'reports/hakios-ios16-phase4-build-checks.json',
        'reports/hakios-ios16-phase4-userspace-build.json',
        'reports/hakios-ios16-boot-userspace-r15.json',
        'reports/hakios-kernel-build.json',
        'reports/hakios-support-build.json',
        'reports/hakios-ios16-package-host-checks.json',
    ]
    for path in proofs:
        data = hakios.read(root / path)
        hakios.require(data.get('passed', True), 'Failed proof: ' + path)
    diag = hakios.read(root / 'ports/hakios-lithium/diagnostics/r15.json')
    commands = ['hakios-r15-prepare', 'hakios-deps', 'hakios-r15-review',
                'hakios-r15-regressions', 'hakios-kernel', 'hakios-support',
                'hakios-r15-userspace', 'hakios-r15-build-check',
                'hakios-r15-freeze', 'hakios-r15-package', 'hakios-r15-package-checks']
    result = dict(revision=15, prepared_at=datetime.now().astimezone().isoformat(),
        status='host_prepared_not_device_tested', passed=True,
        image_path=ready['image_path'], image_sha256=ready['image_sha256'],
        image_bytes=ready['image_bytes'], kernel_sha256=ready['kernel_sha256'],
        baseline=hakios.PIN, xnu='8796.121.2', userspace='16.5/20F66/D22AP/arm64',
        payload_boot_args=ready['cmdline'], observation_seconds=ready['observation_seconds'],
        kernel_return_seconds=10800, kernel_memory_mib=2880,
        cpu_selection=dict(bootcpu=1, cpus=1, expected_logical=0, expected_affinity='0x1',
                           numbering_preserved=True, user_approved=True, hardware_verified=False),
        backup={k:v for k,v in diag['backup'].items() if k != 'prior_inputs'},
        build_tasks=commands, resources=hakios.resources(),
        evidence_sha256={path:hakios.sha(root/path) for path in proofs},
        phone_query_performed=False, phone_boot_performed=False, phone_flash_performed=False,
        userdata_reinstall_required=False, journal_policy_changed=False, flush_policy_changed=False,
        hardware_verified=False, database_ready_verified=False, data_sync_verified=False,
        cross_reboot_persistence_verified=False, springboard_verified=False)
    hakios.write(root/'reports/hakios-ios16-r15-prepared.json', result)
    task_text = '\n'.join('.\\scripts\\run-wsl-limited.ps1 -Task '+t for t in commands)
    md = f'''# R15 Run A delivery

Complete HakiOS `{hakios.PIN}` / XNU 8796.121.2, supplied iOS 16.5 / 20F66 / D22AP / arm64. Full kernel, Mu, D22AP loader and 413 launchd jobs built and checked. Production-function ASan/UBSan regressions, 46 instruction checks, 16 package checks and independent R15 payload readback passed. This report records host preparation only; phone results belong to the subsequent R15 device report.

The user approved `bootcpu=1 cpus=1`: source numbering maps bootcpu 1 to affinity 0x1, which was the faster R14 CPU. Expected logical CPU 0 / affinity 0x1 must be confirmed by the actual `LITHIUM RUN A` and CPU sanity records. No PLL/cache/clock policy change is included. UFS and 2880 MiB stay in use, with affinity 0 reserved for firmware storage service.

## Frozen image and parameters

Private image: `{ready['image_path']}` ({ready['image_bytes']} bytes).
SHA256: `{ready['image_sha256']}`.
Kernel SHA256: `{ready['kernel_sha256']}`.

```text
{ready['cmdline']}
```

Only bootcpu, cpus and loader_return_after differ from R14's frozen payload arguments. The loader additionally appends debug/ramdisk/kernel-version parameters and a dynamically allocated UFS shared address; use the runtime `LITHIUM BOOTARGS` for the actual full line. Kernel timer is 10800 seconds; host capture is 12600 seconds. No first-frame extension. Filesystem calls cannot be forcibly timed out, so final sync may extend the return time.

## Changes

- `ports/hakios-lithium/diagnostics/lsdb-finalize.inc:140`: print actual CPU selection before init. At line 152, inventory relevant LaunchServices files with bounded no-follow scans on the verified RW HFS Data mount, then call real `VFS_SYNC(MNT_WAIT)` and device synchronize-cache, logging each result.
- `ports/hakios-lithium/patches/0015-lithium-run-a-finalize.patch:1`: integrate CPU check and finalizer into the complete upstream tree, before the existing save/stop/drain sequence.
- `ports/hakios-lithium/diagnostics/service-progress.inc:91`: retain the existing cumulative LITHIUM format at 30-second intervals for up to 10800 seconds / 361 rounds.
- `ports/hakios-lithium/diagnostics/springboard-observer.inc:8`: extend samples to 360 / 10800 seconds. Existing eight bounded multithread stack rounds stay bounded.
- `scripts/hakios_r15.py:1` and R15 prepare/build/check integration: preserve source hash chains, CPU numbering and payload budget. Compiler fix uses XNU's existing bounded `strnstr` instead of unavailable `strstr`; no feature removed.
- `scripts/extract-hakios-ios16-run-a.py:1`: extract per-snapshot bundle/container counts, method latency averages, fsync, CPU mapping, database inventory, sync and SpringBoard evidence from saved logs.

Backup branch: `{result['backup']['branch']}`; external dirty-source archive SHA256 `{result['backup']['sha256']}`. Earlier work is preserved. Apple binaries and private images remain excluded from Git and shareable material.

## Completed build commands

```powershell
{task_text}
wsl -d Ubuntu -u jia --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-r15-package.py
```

Authorized next action: `python scripts/boot-hakios-ios16.py --boot`, once. The guarded entry verifies frozen evidence and device identity and starts USB capture before temporary boot. No userdata reinstallation.

## Evidence to retain

`LITHIUM BOOTARGS`, `LITHIUM RUN A`, `LITHIUM CPU` sanity/benchmark/online; every `LITHIUM PROGRESS` snapshot and bundle/container/writeData/replace totals; `LITHIUM HFS` fsync totals and BENCH min/avg/max; `LITHIUM FS stat syscall=95`; `LITHIUM LSDB` file/path/inventory-end; `LITHIUM DATA SYNC` begin/filesystem-end/end; `LITHIUM SB SAMPLE` commits; normal timer, USB drain and loss counters. Extract using `python scripts/extract-hakios-ios16-run-a.py <console.log> --out <directory>`.

R12 raw-storage results remain previous measurements: read 207 us, write 146 us, flush 312 us, write+flush 465 us averages, no errors or mismatches. This run does not repeat raw storage. R14 reference: fast CPU integer loop 2451 us / memcpy 9641 us; HFS write 5487 us, fsync 210815 us, replace 24968 us averages.

Unverified: actual CPU mapping and speed, database ready, sync/flush success, desktop, and persistence across reboot. Active writers are not quiesced; sync success alone cannot prove database readiness or cross-boot persistence. Inventory is bounded and reports missing roots/errors/truncation; nested diagnostic times are inclusive. Persisted Data progress and changed observation duration limit causal comparisons with prior runs. Full 6 GB exposure, GPU and physical touch remain unverified.
'''
    (root/'reports/hakios-ios16-r15-prepared.md').write_text(md,encoding='utf-8',newline='\n')
    note = ('R15 Run A 主机准备完成（2026-11-18）：用户已批准 bootcpu=1 cpus=1，保持编号含义，'
            '目标为R14实测较快的affinity0x1；该明确授权覆盖旧记录的保持三核要求。'
            '完整HakiOS/iOS16.5/Mu/D22AP/413任务、回归/ASan/UBSan、46项指令/16项包及R15实际payload检查通过。'
            '冻结镜像SHA256 '+ready['image_sha256']+'；10800秒内核观察、12600秒USB接收。'
            '返回前有界列出LS数据库路径/大小/mtime并执行Data VFS_SYNC和设备flush，结果均待真机核验。'
            'UFS/2880MiB不变，不重刷userdata，不改journal/flush策略；未查询/启动手机，桌面和跨重启持久化未验证。'
            '用户已确认Fastboot连接，可按受保护入口临时启动一次。见reports/hakios-ios16-r15-prepared.md/json。')
    for name in ('AGENTS.md','PLAN.md','OPERATIONS.md'):
        p=root/name
        old=p.read_text(encoding='utf-8')
        if not old.startswith('R15 Run A 主机准备完成'):
            p.write_text(note+'\n\n'+old,encoding='utf-8',newline='\n')
    state=hakios.read(root/'project.json')
    state.update(stage='iOS16 phase4 R15 Run A frozen; approved fast-core temporary boot pending',
                 current_report='reports/hakios-ios16-r15-prepared.json')
    state['active_kernel'].update(kernel_sha256=ready['kernel_sha256'],
        latest_kernel_binary_hardware_verified=False,status='R15 full build frozen; Run A not yet device tested')
    state['work_status'].update(status='ios16_phase4_r15_prepared',
        next_step='Boot R15 once, verify affinity 0x1, collect three-hour progress, LSDB inventory and Data sync')
    state['ios16_r15']=result
    hakios.write(root/'project.json',state)
    print('R15 prepared report recorded; frozen evidence verified and untouched; phone not queried')


if __name__ == '__main__':
    main()
