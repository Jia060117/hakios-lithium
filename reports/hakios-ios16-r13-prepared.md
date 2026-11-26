# R13 HFS / CPU diagnostic delivery

R12 raw storage is healthy: 4KB read **207 us**, write **146 us**, flush **312 us**, write+flush **465 us** average, zero errors or mismatches. Those are existing phone measurements, not new R13 measurements. They select the HFS/VFS/journal/wait investigation branch; concurrent UFS queueing still needs the per-operation data.

R13 has been fully built and checked on the host. **No phone query, boot or flash was issued in this revision.** The bottleneck and SpringBoard remain unverified. Complete HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2 and supplied iOS16.5/20F66/D22AP/arm64 remain the active base. Data stays on UFS with 2880MiB XNU RAM and one physical CPU reserved for UEFI storage service.

## Inputs and checks

- Preserved the R12 branch `archive/ios16-r12-before-r13-20261118` and external dirty-source archive; SHA256 `8c4eeed2fb5bf993bd4093e40693d9323430d644e650210d43030430dc8739f6`. Existing uncommitted R12 work is preserved.
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

Three CPUs: `artifacts/hakios/lithium-ios16/phase4-r13/lithium-hakios-ios16-r13.img` — 16261120 bytes.

SHA256: `f2594aa3ca007109d596b0429f798be3c54b001829db8f7c4e04bd2443326be1`

```text
bootcpu=2 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=600 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794939085 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

One CPU: `artifacts/hakios/lithium-ios16/phase4-r13-cpus1/lithium-hakios-ios16-r13.img` — 16261120 bytes.

SHA256: `73a08867268e31717b17f4ed86ac0d6e153a20629d895d9f4ac792ec6269ccb6`

```text
bootcpu=2 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=600 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794939085 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

These are exact frozen **payload arguments**. At runtime the loader adds its prefix and dynamically allocated shared-page address. The CPU test outputs the complete actual `LITHIUM BOOTARGS` line; include that line with each returned log. Do not treat the reserved all-zero address in a host command-budget record as a measured phone address.

Private frozen kernel: `artifacts/hakios/lithium-ios16/phase4-r13/hakios-kernel`, SHA256 `c9311ecc3a71157fa97af7e064c66a5a9c923788e0005abd6bd7d4e028a51266`. Images are Git-ignored and contain private supplied firmware/userland; keep them local.

## Manual run — user performs both boots

In one PowerShell window, start the verified existing receiver (it begins querying USB only when **you** run this command):

```powershell
Set-Location F:\IOS-MIX1
& 'E:\IOS-MIX1-artifacts\host-usb-venv\Scripts\python.exe' scripts/capture-hakios-usb.py --output reports/local/r13-manual-cpus3/console.log --status reports/local/r13-manual-cpus3/status.json --wait 180 --duration 900
```

With the phone in Fastboot, run the three-CPU image in a second window:

```powershell
& 'C:\platform-tools\fastboot.exe' boot 'F:\IOS-MIX1\artifacts\hakios\lithium-ios16\phase4-r13\lithium-hakios-ios16-r13.img'
```

After the normal 10-minute return and log drain, use the physical keys to return to Fastboot. Start another receiver using `reports/local/r13-manual-cpus1/console.log` and its corresponding status path, then run:

```powershell
& 'C:\platform-tools\fastboot.exe' boot 'F:\IOS-MIX1\artifacts\hakios\lithium-ios16\phase4-r13-cpus1\lithium-hakios-ios16-r13.img'
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
