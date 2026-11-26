# R13 device result: CPU diagnostic boundary panic

The user photo confirms panic return to UEFI, not the normal 600-second timer. The assertion is **`cpu <= ml_get_max_cpu_number()` at `osfmk/arm/cpu_common.c:82`**, panic CPU2, PID1/init, TID168. The photo reports 43054 retained raw bytes; the complete panic body is not in USB capture. The short UEFI backtrace does not recover a full diagnostic call chain.

R13 diagnostic source enumerates `i=0..3`, calling `cpu_to_processor(i)` before checking the returned pointer. Three online XNU CPUs permit logical IDs **0..2**. The API calls `cpu_datap`, which asserts the inclusive maximum before it can return a pointer. Frozen executable disassembly confirms the loop limit4 and call, and the assertion branch with source line82. The remaining physical CPU is a UEFI storage worker, not an XNU CPU. The unbooted one-CPU R13 variant would also query outside its logical range; do not run it unchanged.

The defect is in the new diagnostic, not proof of an iOS16/HFS/UFS failure. No launchd start or HFS benchmark start was captured. Fix only the diagnostic logical bound for R14, keeping CPU count, clocks, RAM, HFS journal and flush semantics.

## Raw storage first (previous R12)

R12 100×4KB: read min196/avg207/max720us; write min134/avg146/max385us; flush min246/avg312/max552us; write+flush min387/avg465/max728us. Zero errors/mismatches, scratch restored. No new R13 raw-device test was run.

## Actual R13 boot arguments

```text
debug=0x8 rd=md0 ufs_shared_pa=0x0000000040854000 hakios_kver=2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty bootcpu=2 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=600 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794939085 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

## Captured CPU results

```text
LITHIUM CPU BENCH test=integer logical=0 count=5 errors=0 min_us=161656 avg_us=162222 max_us=163149 ns_per_iteration=162 cntvct_min=3103647 cntvct_avg=3114484 cntvct_max=3132313 checksum=0x1ccc1ea74481e979
LITHIUM CPU BENCH test=memcpy logical=0 count=5 errors=0 min_us=298128 avg_us=299364 max_us=301041 MBps_min=111 MBps_avg=112 MBps_max=112 warm_cache=1
LITHIUM CPU BENCH test=integer logical=1 count=5 errors=0 min_us=161735 avg_us=161905 max_us=162147 ns_per_iteration=161 cntvct_min=3105110 cntvct_avg=3108390 cntvct_max=3113083 checksum=0x1ccc1ea74481e979
LITHIUM CPU BENCH test=memcpy logical=1 count=5 errors=0 min_us=297317 avg_us=303184 max_us=308193 MBps_min=108 MBps_avg=110 MBps_max=112 warm_cache=1
```

All three actual logical IDs/MPIDRs were captured: 0→0x80000100, 1→0x80000101, 2→0x80000001. CPU2 benchmark completion was not captured. CNTFRQ is19200000Hz; XNU timebase625/12ns. Sampled allocated RAM/stack PAR attribute0xff is Normal cacheable with no translation fault. CPU MHz, instruction-cache enablement and an independent oscillator reference were not measured. The low observed memcpy throughput warrants investigation after the diagnostic finishes, but cannot establish the frequency defect by itself.

## Log scope and retained evidence

USB 9952bytes/44frames independently redecoded byte-for-byte; gap/duplicate/partial/device-lost counters all0. This verifies **only the captured prefix**, not the complete run. Receiver ended at 2026-11-18T02:21:41.389658+08:00 with Pipe error; no normal timer/drain record. UEFI photo establishes panic terminal state independently.

Raw frames, console, original photo and command records are retained in `reports\local\20261118-022052-840977-ios16-r13`. Image SHA256 `f2594aa3ca007109d596b0429f798be3c54b001829db8f7c4e04bd2443326be1`. No unchanged-image retry, userdata reflash, clock policy or filesystem strategy change was made.
