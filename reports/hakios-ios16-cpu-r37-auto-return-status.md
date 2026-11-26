# R37 automatic Fastboot test status

Date: 2026-11-20 (Asia/Shanghai)

## Result

The R37 CPU diagnostic candidates are built, but **no R37 device boot was issued**. The request was to run the UFS/Data tests and return automatically to Fastboot. All five frozen R37 profiles are writable UFS/Data boots with `loader_return_after=10800`; each contract records `return_fastboot=false` and `ufs_automatic_return_supported=false`. A boot-argument-only change cannot enable the current RAM-only return protocol. Starting one would therefore leave the phone running or returning to UEFI after up to three hours, rather than satisfy the requested automatic Fastboot endpoint.

The requested unattended runner was invoked once on 2026-11-20. It stopped before any Fastboot command with `reference: manifest does not explicitly support UFS automatic return`. No intent guard, USB receiver, boot, reset, partition access, or post-run query was created. This is an intentional fail-closed result, not a failed phone test.

## Host evidence

- `scripts/tests/test_r37_ufs_return_contract.py` passed under the project WSL toolchain: all five UFS profiles are rejected by the current RAM profile gate; UNKNOWN/FAILED storage and repeated claim/handoff are rejected. No synthetic UFS success path was created.
- `scripts/tests/test-r37-ufs-firmware-proof.py` passed: 12 checks and 4 strict-warning compiles. The normal UFS firmware `Prepare`/`Commit` path performs no hardware callbacks and fails closed.
- Current Fastboot-return firmware advertises an enum for UFS but has no UFS backend. The loader accepts only `LFB_PROFILE_RAM_ISOLATED` with `LFB_CAP_NO_PHYSICAL_UFS`; the kernel records `RAM_NONE` only.
- The Data finalizer remains `void`. It logs `VFS_SYNC` and cache-flush errors, while explicitly stating `writers_quiesced=0` and `persistence=UNVERIFIED`. The return path also invokes `hakios_var_save_on_return()` after that finalizer. A successful log line or `VFS_SYNC` result alone cannot establish a quiesced HFS journal and UFS write path.
- Current index SHA256 at closeout: `fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2`. No index or frozen image was changed in this closeout.

## Host-only progress in this turn

- Added and passed the strict UFS argument gate: 87 Clang ASan/UBSan cases, 28 accepted and 59 rejected. This validates selectors and diagnostic options only; it does not prove storage safety.
- Added and passed the firmware v2 mock: default-off, normal UFS, explicit UFS, RAM, and mutually exclusive builds. Invalid proofs are rejected before timer, SCM, MMIO, or reset-core callbacks; a valid UFS proof reaches one mocked terminal commit. This is not hardware evidence.
- Added a host runner that admits a profile only when the package explicitly declares UFS automatic return, requires a bounded timer and `return_fastboot=1`, and permits one preflight, one boot, and one post-run Fastboot verification per profile. Python syntax passed; its first real invocation refused all current R37 profiles before device I/O.
- Read-only audits show that the loader still has an old two-argument `Prepare` call while the checked-in v2 protocol requires proof plus result. The generated build-tree header is also stale. A coordinated loader/Mu refresh is required.
- Read-only XNU audit shows `reboot_kernel()` and `vfs_unmountall()` do not provide an aggregated success result, HFS journal errors are discarded in the existing sync path, and `hakios_var_save_on_return()`/the userland saver do not expose a successful-save proof. These must be corrected before emitting `RFBT_STORAGE_UFS_SYNC_OK`.

## Device preflight

One read-only Fastboot preflight observed serial `54223ce9`, product `MSM8996_A8`, and raw `getvar battery-voltage=3818000` (unit not established). `getvar unlocked` returned no value, so unlock state was not established. No boot, reset, flash, erase, partition access, ADB, Android/Recovery, or QEMU operation was performed.

## CPU measurements

No R37 runtime data was collected. Per-core PMU MHz, CNTVCT/CNTPCT migration behavior, SpringBoard/backboardd per-core residency at Clock-icon jitter, and the effect of `slowcores_idle=1` remain **unverified**. Historical R35 integer and memcpy timings are in `reports/hakios-ios16-cpu-r37-prepared.md`; they do not establish CPU MHz or the clock source. The fixed-frequency design remains withheld pending measurements 1–5 in that report.

## Required before the requested unattended test

1. Implement and review explicit userland/writer and journal quiescence for the normal-return path, with all HFS, VFS, UFS request, and device-cache-flush errors propagated into a generation-bound result. Keep the existing RAM proof separate and unchanged.
2. Refresh the shared v2 header in the actual loader/Mu build and add an explicit UFS firmware profile. Preserve one-shot prepare/commit and fail closed on every missing, stale, or failed field.
3. Build and read back a new coordinated kernel/Mu/loader/firmware package. Do not alter the five existing R37 frozen packages.
4. Only then run the R37 profiles one at a time with the approved automatic return; verify the same Fastboot serial/product after each run. The current R37 boot arguments allow a 10,800-second observation, so the test duration should be intentionally bounded in the new package.

The firmware-only subtask did not change production source. The kernel-side audit found that HFS freeze/sync and the current UFS bridge do not provide the required writer-quiescence guarantee. The host test subtask added the negative-only test and evidence files listed above. This is a safety blocker in the current interface, not a failed CPU measurement.
