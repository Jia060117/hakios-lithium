# HakiOS lithium touch stage 0 source integration

Date: 2026-11-19 (Asia/Shanghai). This is a host-only source integration and test gate using R20-display as the accepted display baseline. R19 was tested by the user and rejected for incorrect resolution; it remains archived and was not retried.

## Result

- Synchronized the strict touch_stage=0..3 parser from the maintenance parser into the lithium production port parser. An explicit stage is distinct from absence; malformed, duplicate, and mixed legacy touch/touch_log/touch_poll arguments fail closed. The new option remains default-off.
- Connected explicit touch_stage=0 to a static LITHIUM TOUCH resource line in ports/hakios-lithium/peripherals/kernel.inc. It reports the stock-DT candidate resources and labels pin roles, clocks, and rails unverified. The stage-0 path does not read MPIDR, query the CPU count, or perform QUP/TLMM MMIO.
- Stages 1–3 now produce a specific qup6_handoff_unverified refusal before any MMIO, IRQ attachment, or HID publication. Legacy touch=1 remains fail-closed.
- Added production-hook host cases for stage 0, stages 1–3, the CPU-query boundary, and conflicting options. Added the existing stage-state and static-resource-log suites to the normal next-stage host runner; the R24 19-test gate omitted these two suites.

## Source evidence and unresolved hardware facts

The matching stock DTS assigns the configured FT5x46-family node to I2C12/QUP6 at 0x075ba000, address 0x38, with 19.2 MHz input and 400 kHz target (artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4844-4865). It assigns GPIO87/88 to the I2C12 pin group (:14221-14252), GPIO89 as reset (:4881), and L14/LAB/IBB supplies (:4868-4871). The saved Android inventory records an ft5x46 input device at 75ba000.i2c/i2c-12/12-0038 (reports/local/20261112-142718-199681/inventory.json:190-193). These establish static configuration and historical Android enumeration, not current runtime identity or ownership.

The same DTS disagrees on touch IRQ: the generic interrupt property resolves to GPIO105 (lithium-stock.dts:4866-4867), while the vendor irq-gpio and active touch pinctrl specify GPIO125 (:4882, :14029-14042). GPIO105 also appears in an ADV7533 pinctrl template (:13288-13313). Keep both candidates; neither is selected or claimed as the live HakiOS interrupt.

The inspected UEFI research sources explicitly describe BLSP2 UART2, not a QUP6 touch ownership/release contract (upstream/research/uefi-g0245d/Silicon/Qualcomm/Msm8996Pkg/Library/SerialPortLib/Msm8996SerialPortLib.c:65-100; Platforms/Gree/g0245dPkg/Library/MemoryMapLib/MemoryMapLib.c:86-101). This bounded source review did not establish who owns QUP6 at XNU entry, whether its clock and GPIO mux are live, or whether a QUP MMIO read is safe. Absence of a visible QUP6 initializer in those inspected files is not proof of hardware state. L14/LAB/IBB state, reset ownership, runtime chip ID/report, split-read coherence, and HID/backboardd delivery also remain unverified.

## Upstream cross-check

The local UniXNU MSM8996 common DTS describes `i2c@75ba000` as BLSP2 QUP6 with a 0x1000-byte window and SPI 106, but that source explicitly describes the G0245D touch bus and leaves the node disabled (`upstream/research/UniXNU-msm8996-common/dts/msm8996.dtsi:352-361`). It corroborates a related SoC resource mapping only; it is not MIX 1 lithium evidence and does not establish lithium's live handoff, touch IRQ choice, or safety of XNU MMIO. No external upstream commit was used to assert a lithium runtime property.

The active loader source adds a more important ownership caveat: `upstream/hakios-xnu-kernel/xnu/files/osfmk/arm64/loader_return.c:5-13` says UEFI is intentionally left intact and explicitly limits the return channel to cases where XNU does not drive hardware UEFI also uses. The loader directly branches into the kernel (`upstream/hakios-xnu-kernel/xnuloader/loader.c:742-765,3524`); the inspected loader source only declares `ExitBootServices` and does not call it. This does not prove UEFI currently owns QUP6, but it means there is no basis to assume that all QUP resources are released to XNU. A documented claim/release or explicit shared-ownership contract is still required before QUP6 register access.

For that reason, this change does not implement a physical QUP producer. The no-reset transfer, FT5x46 parser, stage scheduler, GPIO evidence collector, and queue/delivery models remain host candidates; no physical callbacks were wired. It would be unsafe to represent a host mock's ownership flags as a real platform claim.

## Validation

Command: `wsl.exe -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/artifacts/hakios/lithium-ios16/touch-r20-host-gate-20261119-r5`

Exit code: 0. The 14 existing peripheral suites and 7 next-stage suites passed under Clang ASan/UBSan. The new suites include touch-stage (6 host groups) and touch-static (3-line bounded report); the production admission suite checks that stage 0 performs zero affinity/CPU queries and all higher stages remain blocked without MMIO. Evidence: artifacts/hakios/lithium-ios16/touch-r20-host-gate-20261119-r5/passed.json.

The evidence confirms R20-display SHA-256 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82 and R15 desktop SHA-256 ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55 are unchanged before and after. A fresh r5 run passed the same 21 suites; 45 recorded source hashes match the checked source tree. `git diff --check` passes on the touched source files. This does not establish a full HakiOS production build, package, touch hardware operation, or device behavior.

The production port source has the parser and admission hook, but the active build workspace and latest kernel binary were not rebuilt from this source snapshot. Therefore this is source-level integration plus host validation only; the currently frozen R20/R21 images do not contain or exercise the new `touch_stage` hook. A normal HakiOS prepare/build/package/freeze cycle is still required after hardware prerequisites are supported.

## Parallel execution record

- Board-resource audit worker: compared the stock DTS, saved Android inventory, local UEFI research sources, and prior Xiaomi driver audit. It confirmed the 105/125 static conflict and left runtime ownership/state explicitly unknown. Main-agent review checked the cited local DTS/inventory lines. No files were changed by the worker.
- Production-integration audit worker: identified the production parser and kernel.inc admission hook and confirmed the HID consumer route exists but has no physical producer. Its findings informed the narrow stage parser/report integration. No files were changed by the worker.
- Host-contract audit worker: found the R24 runner omitted touch-stage and touch-static; it also confirmed host GPIO samples do not establish real pin or IRQ behavior. Main agent added both suites to the runner and reran the complete gate. No files were changed by the worker.
- Production-port audit worker: found that the HakiOS lithium port source contains the new parser/hook, but the active build workspace and current kernel binary do not prove those files are synchronized or built. Main-agent review verified the production build status remains false; this narrows the result to source-level integration and host tests.
- Main agent: synchronized the parser, added static/fail-closed source behavior and tests, reran the 21-suite host gate, and reconciled the reports. No worker file conflicts occurred; exact elapsed durations were unavailable.

## Not done and next gate

No phone status was queried; no ADB/Fastboot command, boot, flash, partition operation, userdata write, or QEMU run occurred. No production kernel build or boot image was produced. The existing R21 read-only Stage 0 image remains historical and is not promoted as a physical-touch package.

Before enabling QUP/TLMM access, obtain a defensible QUP6 claim/handoff and live clock/pin/rail prerequisites from matching firmware/platform evidence. Keep GPIO105 and GPIO125 as competing candidates until the permitted read-only sampling test provides evidence; do not drive reset or shared rails. After those prerequisites, wire bounded QUP and FT5x46 callbacks into the production worker, test stages 1–3 through the existing frame/queue path, then run the full HakiOS build/package/freeze gate. Only a complete touch package is eligible for the previously authorized temporary-boot test, with no partition or userdata writes.
