# R20-display touch geometry host alignment

Date: 2026-11-19 (Asia/Shanghai)

## Result

Aligned the host-side touch coordinate fixtures with the user-accepted R20-display geometry: 1080x2040 controller/display coordinates mapped into a 360x680-point viewport at scale 3. The mapping helper exposes one R20 geometry constructor so queue and digitizer tests share the same configured point bounds. Tests retain the 16-bit normalized coordinates consumed by HakiOS backboardd.

This is a host-only geometry and delivery-candidate change. It does not add an FT5x46 producer to the active HakiOS kernel, access QUP6/TLMM/GIC, or create a touch test image. The current lithium hook remains fail-closed. R19 remains archived and excluded; its display dimensions are not used as a touch reference.

## Changed files

- [maintenance/hakios-lithium/peripherals/touch-queue.h](../maintenance/hakios-lithium/peripherals/touch-queue.h): define R20 point dimensions and a shared geometry constructor.
- [maintenance/hakios-lithium/peripherals/touch-queue-test.c](../maintenance/hakios-lithium/peripherals/touch-queue-test.c): use R20 bounds and verify the clipped viewport mapping.
- [maintenance/hakios-lithium/peripherals/touch-digitizer-test.c](../maintenance/hakios-lithium/peripherals/touch-digitizer-test.c): use R20 viewport bounds and point-scaled expected coordinates.
- [maintenance/hakios-lithium/peripherals-next/touch-delivery-test.c](../maintenance/hakios-lithium/peripherals-next/touch-delivery-test.c): use R20 geometry for the host delivery candidate.

## Verification

Full host regression command:

```powershell
wsl.exe -d Ubuntu --exec python3 -B /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/reports/touch-r20-geometry-host-20261119-r3
```

Result: PASS. All 14 existing peripheral suites and all 5 next-stage suites passed with Clang AddressSanitizer and UndefinedBehaviorSanitizer. The run also verified the accepted R20-display and R15 baseline image hashes before and after testing. Evidence is in [passed.json](touch-r20-geometry-host-20261119-r3/passed.json).

 The exact HakiOS batch queue function extracted from patch 0020 was separately compiled and tested against the upstream touch-state helpers; all success, invalid-input, queue-capacity, wraparound and touch-state checks passed. `git diff --check` passed.

Frozen images remain unchanged:

- R20-display: `3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82`.
- R15 desktop baseline: `ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`.

The first regression attempt exposed stale 750x1334 test assumptions. The fixtures and expected point coordinates were corrected to R20, and the full regression then passed. No baseline image or production kernel source was changed.

## Not verified / next gate

- QUP6 ownership and UEFI-to-XNU handoff, controller clocks, GPIO87/88 pinmux, rail state and safe first transaction remain unverified.
- The stock DTS has an unresolved GPIO105/GPIO125 interrupt conflict; GPIO89 reset and shared power remain unverified.
- Runtime FT5x46 identity, raw frames, frame consistency, actual coordinate calibration and real touch events through backboardd/SpringBoard remain unverified.
- No full HakiOS production build/package was made because no hardware producer was integrated. This change is not eligible for a phone test and does not satisfy the conditional temporary-boot gate.
- No phone query/boot, ADB/Fastboot device command, partition/userdata operation or QEMU was performed.

The related touch files already had unrelated dirty changes at the start of this phase, so no mixed commit or aggregate patch was produced. Existing workspace changes were preserved.

