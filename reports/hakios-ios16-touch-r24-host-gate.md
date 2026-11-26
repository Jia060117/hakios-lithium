# HakiOS lithium touch R24 host gate

Date: 2026-11-19 (Asia/Shanghai)  
Scope: host-side parser and delivery-contract review only. No production touch driver, device operation, partition write, or QEMU.

## Accepted display baseline

R20-display remains the sole display baseline: 1080×2040 BGR24, stride 3240, 360×680 points, scale 3; frozen image SHA-256 `3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82`. The user confirmed R19 was tested and had the wrong resolution; it is archived and excluded from future display or touch acceptance. R19 was not retried.

## Host changes and validation

- Extended the host boot-option parser with strict, default-off `touch_stage=0..3`; absent means no request, malformed/duplicate values and conflicts with the legacy `touch`, `touch_log`, or `touch_poll` options are rejected. The parser test also checks GPU options remain independent. This parser change is currently in the host-maintenance layer; it has not been synchronized into a new production kernel package.
- Corrected the delivery model for zero-event batches. HakiOS's current atomic queue accepts only non-empty batches. A stationary frame with no HID transition now commits locally and advances producer sequence/time high-water marks without a false sink receipt. An empty stream-loss frame is separately retained in a saturating producer-local counter; it is explicitly not delivered or acknowledged by the current event-only queue ABI. Non-empty batches keep atomic accept/reject/uncertain handling.
- Ran the 14 existing peripheral suites and 5 next-stage suites with Clang, ASan, and UBSan. All passed, including parser conflicts, stationary/no-op delivery, empty-loss accounting and saturation, contact release, QUP fault injection, and R20/R15 frozen-image checks.

Command:

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/artifacts/hakios/lithium-ios16/touch-r20-host-gate-20261119-r3
```

Result: exit code 0; all 19 suites passed. Evidence: `artifacts/hakios/lithium-ios16/touch-r20-host-gate-20261119-r3/passed.json`. R20-display and R15 desktop image hashes were unchanged.

## Build-report provenance

The mutable root `reports/hakios-kernel-build.json` currently names candidate kernel SHA-256 `32cec5999d77e9e9bc1642b1e942f35f218e2983892fe3cbd5503684552a13b1`; it is not R19 or R20 evidence. A later `hakios-kernel` candidate task refreshed that shared report, and its saved log records an inner symbol-set/export failure despite the wrapper's success. The frozen R19 kernel remains `8e9209c1ff077ba6ad16cbd467c6cceb3cb6b16c0357667e513b913190fd9b4b`; the separate R21 Stage 0 build records `f19ef6e026a10bef44003d91984c44eb475b000bc952f68d9247b3e2271b63b1`. Frozen R19 evidence and the R20 display image are unaffected. Keep the mismatch visible; do not use the mutable report as proof of a successful production build. Detailed provenance is in `reports/hakios-ios16-touch-freeze-audit.md`.

## Hardware gate and remaining work

The lithium host candidate still does not provide a production QUP6/FT5x46 producer. Available stock DT and saved Android inventory identify an I²C12/QUP6, address `0x38`, GPIO87/88 pin candidates, and an FT5x46-family device. They do not establish current resource ownership or handoff. The GPIO105/125 IRQ definitions conflict; clock, mux, rail state, GPIO89 reset ownership, runtime controller ID, raw report format, and split-read frame consistency remain unverified. HakiOS's Redwood SPI touchscreen driver is not a substitute for this MSM8996 QUPv2 I²C path.

Do not enable QUP/TLMM MMIO or attach an IRQ until these prerequisites are established. The next useful evidence is a matching lithium Android device-tree/driver resource trace plus controller runtime ID and raw idle/down/move/up reports, and a documented UEFI-to-XNU claim/handoff for QUP6, pins, and rails. Then integrate the producer behind a default-off gate, verify the actual HakiOS build and package, and only afterward use the previously authorized temporary Fastboot test. No userdata or partition writes are included in that authorization.

## Parallel execution record

- Boot-option parser worker: added `touch_stage=0..3` parsing and host cases in `maintenance/hakios-lithium/peripherals/boot-options.h` and `boot-options-test.c`. Focused Clang ASan/UBSan test passed. No production files were changed. Exact elapsed time was not available.
- Delivery-contract worker: proposed a zero-event fast path and focused tests. Independent review found that applying it to empty stream-loss batches would silently discard the loss indication. The proposal was not accepted as written; the main agent narrowed the behavior and added an explicit saturating local-loss counter. The focused/full host regressions were rerun after correction. Exact worker elapsed time was not available.
- Independent reviewer: checked no-op and stream-loss semantics against the actual 1..20-event HakiOS queue contract; no files changed. Its finding was incorporated into the corrected host model. Exact elapsed time was not available.
- Main agent: verified R20/R19 disposition and build-report provenance, reviewed the QUP6 and HID integration boundaries, corrected the candidate delivery contract, and ran the full 19-suite host regression. No agent file conflict remained.

## Explicitly not verified

No complete production kernel build or touch image was made. No phone status was queried; no ADB/Fastboot command, boot, flash, partition operation, or userdata write was performed. Physical touch, QUP6 ownership, IRQ delivery, HID/backboardd delivery, and SpringBoard response remain unverified.
