# R15 Run A runtime

Updated 2026-11-18T03:50:04.135718+08:00. Temporary boot accepted once at 2026-11-18T03:41:09.232762+08:00. Latest user screen observation: **已出现锁屏或桌面**. No partition flash was issued. Full HakiOS / iOS16.5 / UFS / 2880MiB remains active. Private image SHA256 `ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`.

Status: **capture_terminal_requires_review**. Fast physical-core selection verified: **True**. launchd PID1 observed: **True**. USB saved 2894624 bytes / 13311 frames at the observation timestamp; live counters are provisional.

## Actual complete boot arguments

```text
debug=0x8 rd=md0 ufs_shared_pa=0x0000000040854000 hakios_kver=2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

## Actual CPU selection and benchmarks

```text
LITHIUM RUN A logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 bootcpu=1 cpus=1 expected_affinity=0x1 match=1
LITHIUM CPU sanity logical=0 actual_logical=0 mpidr=0x80000001 cntfrq=19200000 mair=0xc08044f00bb44ff tcr=0x22a51a251a ram_va=0xfffffff8bac94000 ram_par=0xff00000109b6cb00 ram_attr=0xff ram_fault=0 stack_par=0xff00000109307b00
LITHIUM CPU BENCH test=integer logical=0 count=5 errors=0 min_us=2441 avg_us=2477 max_us=2560 ns_per_iteration=2 cntvct_min=46874 cntvct_avg=47561 cntvct_max=49162 checksum=0x1ccc1ea74481e979
LITHIUM CPU BENCH test=memcpy logical=0 count=5 errors=0 min_us=9472 avg_us=9559 max_us=9690 MBps_min=3462 MBps_avg=3510 MBps_max=3542 warm_cache=1
```

## Pre-launch HFS benchmark

```text
LITHIUM HFS BENCH test=write count=20 errors=0 min_us=109 avg_us=140 max_us=323
LITHIUM HFS BENCH test=fsync count=20 errors=0 min_us=270 avg_us=317 max_us=632
LITHIUM HFS BENCH test=rename_replace count=20 errors=0 min_us=238 avg_us=481 max_us=565
LITHIUM HFS BENCH test=read count=20 errors=0 min_us=33 avg_us=44 max_us=183
LITHIUM HFS BENCH end completed=20 expected=20 error=0 mismatches=0 cleanup_error=0 elapsed_us=26728 prelaunch=1 first_rename_creates=1 later_renames_replace=1
```

R14 reference averages: write 5487 us, fsync 210815 us, rename/replace 24968 us; integer loop on affinity 0x100/0x101 was about 162 ms, while affinity 0x1 was 2451 us. These are prior-run measurements. Persisted Data state and the different kernel revision prevent treating all before/after differences as a controlled CPU-only result.

## Database gate and visible system UI

Database readiness gate released: **True**. SpringBoard main queue entered: **True**. User-confirmed lock screen or home screen together with SpringBoard runtime evidence: **True**. The user's combined answer does not distinguish lock screen from home screen, and physical touch is unverified. These milestones establish progress beyond the old LaunchServices wait; they do not prove exhaustive database integrity or persisted state.

```text
LITHIUM DISPLAY: swap 1 layer_elapsed_us 13732 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM DISPLAY: swap 2 layer_elapsed_us 15829 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM DISPLAY: swap 3 layer_elapsed_us 12202 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM DISPLAY: swap 4 layer_elapsed_us 14294 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM DISPLAY: swap 5 layer_elapsed_us 0 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM DISPLAY: swap 6 layer_elapsed_us 0 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM PROGRESS pid=26 group=0x0 call=0 kind=set-seeding-complete-return count=1 value=1 tid=0x11a last_us=521853718
LITHIUM PROGRESS pid=26 group=0x0 call=0 kind=blocking-fetch-method-return count=16 value=0 tid=0x159 last_us=521857671
LITHIUM PROGRESS pid=26 group=0x10186c530 call=0x181cc498c kind=group-wait-return count=16 value=0 tid=0x159 last_us=521857659
LITHIUM PROGRESS pid=26 group=0x10186c530 call=0x181d07bf8 kind=group-wait-return count=1 value=0 tid=0x128 last_us=523233697
LITHIUM PROGRESS pid=26 group=0x10186c530 call=0x181c0436c kind=group-wait-return count=1 value=0 tid=0x38a last_us=541395460
LITHIUM DISPLAY: swap 7 layer_elapsed_us 14518 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM SB LOOP first=entry elapsed_ms=191249; not desktop evidence
LITHIUM SB MARK main_queue_enter elapsed_ms=192190
LITHIUM SB MARK main_queue_callback_done elapsed_ms=192191
LITHIUM DISPLAY: swap 8 layer_elapsed_us 12553 panel 1080x2040 pixel_bytes 3; includes scheduling
LITHIUM SB LOOP first=before_CA_commit elapsed_ms=197805; not desktop evidence
LITHIUM SB LOOP first=after_CA_commit elapsed_ms=197810; not desktop evidence
LITHIUM SB LOOP first=after_waiting elapsed_ms=197842; not desktop evidence
LITHIUM PROGRESS pid=26 group=0x10186c530 call=0x181c0436c kind=group-wait-return count=2 value=0 tid=0x4b4 last_us=592871724
```

## Latest cumulative progress

```json
{
  "8:SBRecord": {
    "line": 18532,
    "snapshot": 14,
    "us": 828684964,
    "pid": 8,
    "op": 8,
    "name": "SBRecord",
    "begin": 756,
    "end": 756,
    "fail": 47,
    "distinct": 226,
    "repeat": 530,
    "total_us": 144542341,
    "max_us": 143994594,
    "avg_us": 191193.57275132276,
    "snapshot_end_seen": true
  },
  "14:writeData": {
    "line": 18301,
    "snapshot": 16,
    "us": 812304943,
    "pid": 14,
    "op": 4,
    "name": "writeData",
    "begin": 140,
    "end": 140,
    "fail": 0,
    "distinct": 107,
    "repeat": 33,
    "total_us": 2363449,
    "max_us": 82274,
    "avg_us": 16881.77857142857,
    "snapshot_end_seen": true
  },
  "26:scan": {
    "line": 18595,
    "snapshot": 16,
    "us": 829435388,
    "pid": 26,
    "op": 2,
    "name": "scan",
    "begin": 7,
    "end": 7,
    "fail": 0,
    "distinct": 7,
    "repeat": 0,
    "total_us": 82159652,
    "max_us": 76986010,
    "avg_us": 11737093.142857144,
    "snapshot_end_seen": true
  },
  "26:gather": {
    "line": 18602,
    "snapshot": 16,
    "us": 829435388,
    "pid": 26,
    "op": 3,
    "name": "gather",
    "begin": 2,
    "end": 2,
    "fail": 0,
    "distinct": 0,
    "repeat": 0,
    "total_us": 125030494,
    "max_us": 125020108,
    "avg_us": 62515247.0,
    "snapshot_end_seen": true
  },
  "26:enumerate": {
    "line": 18607,
    "snapshot": 16,
    "us": 829435388,
    "pid": 26,
    "op": 9,
    "name": "enumerate",
    "begin": 1,
    "end": 1,
    "fail": 0,
    "distinct": 0,
    "repeat": 0,
    "total_us": 143154985,
    "max_us": 143154985,
    "avg_us": 143154985.0,
    "snapshot_end_seen": true
  },
  "26:bundle": {
    "line": 18575,
    "snapshot": 16,
    "us": 829435388,
    "pid": 26,
    "op": 0,
    "name": "bundle",
    "begin": 408,
    "end": 408,
    "fail": 3,
    "distinct": 408,
    "repeat": 0,
    "total_us": 10263789,
    "max_us": 958045,
    "avg_us": 25156.345588235294,
    "snapshot_end_seen": true
  },
  "26:container": {
    "line": 18585,
    "snapshot": 16,
    "us": 829435388,
    "pid": 26,
    "op": 1,
    "name": "container",
    "begin": 863,
    "end": 863,
    "fail": 190,
    "distinct": 863,
    "repeat": 0,
    "total_us": 14293064,
    "max_us": 465833,
    "avg_us": 16562.067207415992,
    "snapshot_end_seen": true
  },
  "14:metadata": {
    "line": 18310,
    "snapshot": 16,
    "us": 812304943,
    "pid": 14,
    "op": 5,
    "name": "metadata",
    "begin": 139,
    "end": 139,
    "fail": 0,
    "distinct": 0,
    "repeat": 0,
    "total_us": 2533005,
    "max_us": 83251,
    "avg_us": 18223.057553956834,
    "snapshot_end_seen": true
  },
  "14:protection": {
    "line": 18315,
    "snapshot": 16,
    "us": 812304943,
    "pid": 14,
    "op": 6,
    "name": "protection",
    "begin": 33,
    "end": 33,
    "fail": 0,
    "distinct": 0,
    "repeat": 0,
    "total_us": 1143277,
    "max_us": 95071,
    "avg_us": 34644.757575757576,
    "snapshot_end_seen": true
  },
  "14:replace": {
    "line": 18320,
    "snapshot": 16,
    "us": 812304943,
    "pid": 14,
    "op": 7,
    "name": "replace",
    "begin": 33,
    "end": 33,
    "fail": 0,
    "distinct": 0,
    "repeat": 0,
    "total_us": 1036309,
    "max_us": 84016,
    "avg_us": 31403.303030303032,
    "snapshot_end_seen": true
  }
}
```

Complete same-run files: `reports\local\20261118-034108-962949-ios16-r15/console.log`, `console.frames`, `usb-status.json`, `observer-history.jsonl` and `observer-status.json`. The read-only observer follows this run every 45 seconds and independently decodes the raw frames. On capture termination it writes `run-a-final-audit.json`, `run-a-final-analysis.json` and `run-a-final-selected.log`.

Normal kernel timer is 10800 seconds; host receiver allows 12600 seconds. Runtime filesystem calls may extend finalization. Inventory/sync results: not yet captured. Verified final emitted stream through timer/drain: False. Sync and device flush both returned zero: False. Active writers are not stopped; even successful sync is not cross-reboot persistence proof.

Cross-reboot persistence, home-screen distinction, current terminal screen, full 6GB exposure, GPU and physical touch: **未验证**. A run-loop commit or Apple logo alone is not visible SpringBoard success; the recorded system-UI milestone also uses explicit user observation. Source-side diagnostic loss must be reviewed independently of USB transfer continuity. No automatic unchanged-image reboot or shutdown is scheduled.

Build/change delivery: `reports/hakios-ios16-r15-prepared.md`. Root-cause conclusion remains open until the run's actual progress and finalization evidence are reviewed.
