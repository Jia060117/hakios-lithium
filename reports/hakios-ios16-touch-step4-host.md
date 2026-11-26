# HakiOS iOS 16 lithium touch: Step 4 host gate and R20 baseline

Date: 2026-11-19. Scope: continue touch work from the user-confirmed R20-display baseline. No phone query/boot/reboot, ADB/Fastboot device command, partition or userdata operation, or QEMU was performed.

## Baseline decision

The user clarified that R19 was tested but its display resolution was wrong, and asked to stop spending time on R19. R19 is excluded from touch acceptance and from this host test runner's freeze gate. R20-display is the active display reference: 1080x2040 BGR24, 360x680 points at scale 3. The R20 image's actual SHA256 is 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82, matching its hardware acceptance report. The preserved R15 desktop image also still matches its recorded SHA256.

## Host changes and checks

Updated scripts/test-hakios-lithium-peripherals-next.py:1,17-48 so the next-phase host tests guard the immutable R20-display and R15 desktop images, and cross-check R20's hardware display acceptance and package-check reports. It no longer requires the obsolete R19 evidence snapshots to match the live working tree. It does not update or bypass the R19 contract; that contract remains a historical frozen record.

Commands and results:

- WSL full host runner: wsl.exe -d Ubuntu --exec python3 -B /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py --compiler clang — PASS. The existing 14 peripheral suites and 5 next-stage suites passed under ASan/UBSan. Coverage included QUP combined-transfer models and fault sweeps, FT5x46 frame/state parsing, delivery state machine, IRQ-affinity policy, A5xx fence packet model, and coordinate/queue behavior.
- Exact patch harness: WSL Python test-touch-batch-patch.py with Clang, ASan and UBSan — PASS. It compiled and exercised the exact hakios_touch_post_batch candidate from patch 0020 with HakiOS touch_feel_add and count_fingers helpers.
- Python syntax compilation for the adjusted runner — PASS.
- The runner verified the R20-display and R15 image hashes before and after its tests. No image, production kernel source, or userdata image was changed.

## Production integration remains blocked

There is still no production FT5x46/QUP6 backend. ports/hakios-lithium/peripherals/kernel.inc:29-31 intentionally reports platform_backend_unverified with mmio=0, irq_attached=0, and hid_events=0. The upstream /dev/hakios-touch producer/consumer route exists, and patch 0020 only adds batched queue posting; neither fact means physical input is wired to it.

The QUP candidate is not yet safe as the first hardware transaction: maintenance/hakios-lithium/peripherals-next/qup-combined.h:104-115 fails closed unless every platform ownership attestation is present, then writes QUP SW_RESET at line 115. The board's QUP ownership, live clock and pinmux state are not verified, so static DT data cannot justify enabling those attestations or issuing that reset. The DTS also contains a GPIO105/125 interrupt conflict (artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4866-4867,4882), while reset GPIO89 and shared display-related supply ownership must remain untouched until verified. Runtime controller ID/firmware, split-report consistency, IRQ delivery and the end-to-end queue/backboardd/SpringBoard path are all unverified.

Thus this step clears a stale R19 test gate and confirms the host models; it does not produce a flashable or bootable touch package, and does not claim physical touch support.

## Next steps

1. Review the pinned QUPv2 transfer source and implement/test a non-reset initial transaction path, including bounded cleanup that never falsely releases a poisoned controller. Keep hardware MMIO unreachable until platform ownership prerequisites are independently established.
2. Add a default-off lithium FT5x46 producer in the active HakiOS port, using the existing queue API and the R20 1080x2040 mapping. Keep IRQ and polling controls separate; do not select GPIO105 or GPIO125 as proven until runtime evidence resolves the conflict.
3. Run the full HakiOS build, integration/static/package checks and freeze the resulting candidate before any device test. Only then use the authorized temporary-boot path; do not write partitions or userdata.
4. First device round should collect only bounded controller identity, QUP result/error counters and IRQ counts, without reset, rail toggles or HID injection. Proceed to raw frames and then UI events only if each prior result is valid.

## Parallel execution record

Four read-only audit agents were started in bounded, disjoint scopes covering QUP first-transfer safety, GPIO/IRQ/GIC routing, HakiOS input integration and R19/R20 freeze status. None returned a reviewable deliverable within the bounded wait windows; all were closed and no agent result or code change was used. Two initial spawn attempts were rejected while the session agent-thread limit was full; the audits were started after capacity became available. The main agent independently checked the admission hook, QUP reset gate, queue patch, R20 acceptance record and host-test results. No patch conflict or agent-authored change was merged.

## Verification boundary

R20 display acceptance is confirmed by prior user report/photo and its device report. Physical touch, QUP ownership/clock/pads, FT5x46 runtime identity, GPIO interrupt source, shared-power handoff, and input events on the screen remain unverified. No host model is treated as a substitute for those hardware checks.
