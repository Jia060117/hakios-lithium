# HakiOS iOS 16 Touch R23 Host Stage

Date: 2026-11-19 (Asia/Shanghai)

## Result

R20-display is the sole accepted display baseline. The user confirmed that R19 was tested but had the wrong resolution; R19 remains archived and is excluded from touch geometry and acceptance.

Added a lithium-scoped HakiOS patch that scales the existing EdgeTip gesture margins to R20's 360x680 point space: 16 points on the left, right, and top, and 34 points at the bottom. The physical panel remains 1080x2040 BGR24 at scale 3. The patch changes only userland/src/inject.c and leaves unverified controller metadata untouched.

This is a host-side userland geometry correction. It does not add or enable an FT5x46/QUP6 producer, does not integrate the peripheral candidate into the active kernel, and is not a physical touch package.

## Changed Files

- ports/hakios-lithium/patches/0021-lithium-r20-touch-geometry.patch: R20 point-space EdgeTip thresholds for the lithium build.
- scripts/test-hakios-ios16-touch-r23.py: verifies the pinned clean HakiOS source, one-file patch scope, exact geometry, R20 display evidence, and R20/R15 frozen image hashes.
- reports/touch-r23-host-20261119-r1/passed.json: full host regression evidence.
- reports/hakios-ios16-touch-r23-host.patch: selective patch export for this phase's new source, test, and report files.

## Verification

Passed:

- R23 patch check against HakiOS commit 63fc2b4633bac3af8e57dadab9f5a8d4cb940438, with the upstream checkout clean.
- Static contract check: one target file, exact 360x680 point geometry, no obsolete dimensions in additions, and no controller transport/manufacturer metadata edits.
- Full Clang ASan/UBSan host regression: 14 existing peripheral suites and 5 next-stage suites passed.
- Frozen image checks before and after: R20-display SHA256 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82; R15 desktop SHA256 ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55.
- git diff --check for the new patch and test script.

Commands:

    wsl.exe -d Ubuntu --exec python3 -B /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-touch-r23.py
    wsl.exe -d Ubuntu --exec python3 -B /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/reports/touch-r23-host-20261119-r1

No full HakiOS production build or image package was made. No phone query, boot, ADB/Fastboot device command, partition or userdata operation, or QEMU run was performed.

## Parallel Execution Record

- Ramanujan: repaired the FT5x46 host contact-state model and tests in peripherals-next/ft5x46-frame.h and peripherals-next/ft5x46-frame-test.c. The host parser/state test passed with Clang ASan/UBSan. The main agent reviewed the changed state transitions and reran the full 19-suite host regression. The agent's elapsed time was not recorded; no conflict or rework was needed.
- Beauvoir: aligned the host digitizer EdgeTip margins to the R20 360x680 viewport and added boundary tests in peripherals/touch-digitizer.h and peripherals/touch-digitizer-test.c. Its focused test passed with Clang ASan/UBSan. The main agent reviewed the point-space calculation and reran the full 19-suite host regression. The agent's elapsed time was not recorded; no conflict or rework was needed.
- Main agent: added the isolated HakiOS userland patch and patch-contract test, verified both agents' edits, reran the complete host regression, checked the pinned upstream checkout and frozen image hashes, and exported the phase patch. No overlapping writes or merge conflicts occurred.

## Unverified and Next Work

- Physical FT5x46 identity and firmware revision, QUP6 ownership/handoff, bus clocks and pinmux, regulator state, GPIO IRQ selection, and actual touch reports remain unverified.
- GPIO105 versus GPIO125 remains an unresolved IRQ conflict. No IRQ choice is promoted by this geometry patch.
- No production touch producer, IOKit event delivery, SpringBoard response, or complete HakiOS production build has been verified.
- Continue with the default-off QUP6/FT5x46 producer only after establishing safe resource ownership and the runtime controller identity from available device/Android evidence. Keep the existing fail-closed path until that evidence exists. A phone test remains gated on a complete production build, package checks, and frozen touch image.
