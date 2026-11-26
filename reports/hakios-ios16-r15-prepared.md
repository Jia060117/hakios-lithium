# R15 Run A delivery

Complete HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU 8796.121.2, supplied iOS 16.5 / 20F66 / D22AP / arm64. Full kernel, Mu, D22AP loader and 413 launchd jobs built and checked. Production-function ASan/UBSan regressions, 46 instruction checks, 16 package checks and independent R15 payload readback passed. This report records host preparation only; phone results belong to the subsequent R15 device report.

The user approved `bootcpu=1 cpus=1`: source numbering maps bootcpu 1 to affinity 0x1, which was the faster R14 CPU. Expected logical CPU 0 / affinity 0x1 must be confirmed by the actual `LITHIUM RUN A` and CPU sanity records. No PLL/cache/clock policy change is included. UFS and 2880 MiB stay in use, with affinity 0 reserved for firmware storage service.

## Frozen image and parameters

Private image: `artifacts/hakios/lithium-ios16/phase4-r15/lithium-hakios-ios16-r15.img` (16265216 bytes).
SHA256: `ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`.
Kernel SHA256: `7d76f0e065d2ac544b75914deb2c0770b1d32805aaafe509eab07d8298aef8c8`.

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

Only bootcpu, cpus and loader_return_after differ from R14's frozen payload arguments. The loader additionally appends debug/ramdisk/kernel-version parameters and a dynamically allocated UFS shared address; use the runtime `LITHIUM BOOTARGS` for the actual full line. Kernel timer is 10800 seconds; host capture is 12600 seconds. No first-frame extension. Filesystem calls cannot be forcibly timed out, so final sync may extend the return time.

## Changes

- `ports/hakios-lithium/diagnostics/lsdb-finalize.inc:140`: print actual CPU selection before init. At line 152, inventory relevant LaunchServices files with bounded no-follow scans on the verified RW HFS Data mount, then call real `VFS_SYNC(MNT_WAIT)` and device synchronize-cache, logging each result.
- `ports/hakios-lithium/patches/0015-lithium-run-a-finalize.patch:1`: integrate CPU check and finalizer into the complete upstream tree, before the existing save/stop/drain sequence.
- `ports/hakios-lithium/diagnostics/service-progress.inc:91`: retain the existing cumulative LITHIUM format at 30-second intervals for up to 10800 seconds / 361 rounds.
- `ports/hakios-lithium/diagnostics/springboard-observer.inc:8`: extend samples to 360 / 10800 seconds. Existing eight bounded multithread stack rounds stay bounded.
- `scripts/hakios_r15.py:1` and R15 prepare/build/check integration: preserve source hash chains, CPU numbering and payload budget. Compiler fix uses XNU's existing bounded `strnstr` instead of unavailable `strstr`; no feature removed.
- `scripts/extract-hakios-ios16-run-a.py:1`: extract per-snapshot bundle/container counts, method latency averages, fsync, CPU mapping, database inventory, sync and SpringBoard evidence from saved logs.

Backup branch: `archive/ios16-r14-before-r15-20261118`; external dirty-source archive SHA256 `cf71b711297f9d899b7049cd0ba5bc58b16f124153ffa7145aaebbbcde716edc`. Earlier work is preserved. Apple binaries and private images remain excluded from Git and shareable material.

## Completed build commands

```powershell
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-prepare
.\scripts\run-wsl-limited.ps1 -Task hakios-deps
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-review
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-regressions
.\scripts\run-wsl-limited.ps1 -Task hakios-kernel
.\scripts\run-wsl-limited.ps1 -Task hakios-support
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-userspace
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-build-check
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-freeze
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-package
.\scripts\run-wsl-limited.ps1 -Task hakios-r15-package-checks
wsl -d Ubuntu -u jia --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-r15-package.py
```

Authorized next action: `python scripts/boot-hakios-ios16.py --boot`, once. The guarded entry verifies frozen evidence and device identity and starts USB capture before temporary boot. No userdata reinstallation.

## Evidence to retain

`LITHIUM BOOTARGS`, `LITHIUM RUN A`, `LITHIUM CPU` sanity/benchmark/online; every `LITHIUM PROGRESS` snapshot and bundle/container/writeData/replace totals; `LITHIUM HFS` fsync totals and BENCH min/avg/max; `LITHIUM FS stat syscall=95`; `LITHIUM LSDB` file/path/inventory-end; `LITHIUM DATA SYNC` begin/filesystem-end/end; `LITHIUM SB SAMPLE` commits; normal timer, USB drain and loss counters. Extract using `python scripts/extract-hakios-ios16-run-a.py <console.log> --out <directory>`.

R12 raw-storage results remain previous measurements: read 207 us, write 146 us, flush 312 us, write+flush 465 us averages, no errors or mismatches. This run does not repeat raw storage. R14 reference: fast CPU integer loop 2451 us / memcpy 9641 us; HFS write 5487 us, fsync 210815 us, replace 24968 us averages.

Unverified: actual CPU mapping and speed, database ready, sync/flush success, desktop, and persistence across reboot. Active writers are not quiesced; sync success alone cannot prove database readiness or cross-boot persistence. Inventory is bounded and reports missing roots/errors/truncation; nested diagnostic times are inclusive. Persisted Data progress and changed observation duration limit causal comparisons with prior runs. Full 6 GB exposure, GPU and physical touch remain unverified.
