# R20-display touch Stage 0 host build

Date: 2026-11-19 (Asia/Shanghai). Scope: host-side integration/build validation only. R20-display is the accepted display baseline. R19 remains archived and was not retried or used for display acceptance.

## Result

The R20 geometry patch now applies with stable source context after the actual 0010 and 0012 HakiOS patch sequence. The source review verifies the full upstream plus existing service observer plus R20 geometry result byte-for-byte. The phase-4 checker now admits only the reviewed post-R20 Stage 0 source delta and checks that the static Stage 0 and fail-closed markers are present in the linked kernel.

The production HakiOS source workspace was synchronized and checked. The complete HakiOS kernel, D22AP loader, Mu firmware, and all 413 upstream userland launchd jobs rebuilt successfully. The phase-4 build check passed its 46 instruction scans; the updated source review passed 83 checks. The 14 existing and 7 new Clang ASan/UBSan peripheral suites passed, as did the ordered R20 geometry patch test.

This is not a physical touch package. Stage 0 emits static resource facts only; stages 1–3 remain blocked before QUP/TLMM MMIO, IRQ attachment, or HID event publication. No production FT5x46/QUP6 event producer is present, so there is no new image eligible for touch testing.

## Frozen baseline and host outputs

- R20-display image SHA-256: 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82 (unchanged). Its accepted geometry remains 1080×2040 BGR24, 360×680 points, scale 3.
- R15 desktop image SHA-256: ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55 (unchanged).
- HakiOS kernel SHA-256: 3e130b765b47ec06ac5e5392d5f94301d22ed43fa37b937827b65ab220180c93. The linked binary contains the Stage 0 static and QUP6 fail-closed diagnostic strings.
- D22AP loader SHA-256: f0084524c0fb7f36f3d4cd86c1cf6d3e00820f7ebcada0f2d1a618ccdcbdd81b.
- Mu firmware SHA-256: 2be05c780b07b5521a4bcacbb6fbb459aae2194768c9c84fd36eab88148ccaa5.
- Userland root / flashboot / System APFS SHA-256: 09f6917ea3ef051b44d313bc479d2b8f7ab7c138f71638f88deb963c25fb6624 / b48dd157a3aa981fb40ee7ba2d24628dc268acb48756c89492454a03bbb8d625 / f9465f526753f9a0d3b9a2f9604650421bfd680a8c43f8e751966dfa7baab4fa.

Evidence: reports/hakios-ios16-touch-r20-host-gate-20261119-r7/passed.json, reports/hakios-ios16-touch-stage0-build-checks.json, reports/hakios-ios16-source-review.json, reports/hakios-kernel-build.json, reports/hakios-support-build.json, and reports/hakios-ios16-phase4-userspace-build.json. Host outputs are retained under artifacts/hakios/lithium-ios16/phase4-touch-stage0/.

## Remaining hardware prerequisites

QUP6 ownership and UEFI-to-XNU claim/release, QUP clocks, pinmux and touch rails, GPIO105/125 interrupt selection, reset GPIO89 ownership, runtime controller identity/report reads, and HID/backboardd event delivery remain unverified. Do not enable MMIO or claim physical-touch support until matching lithium platform evidence establishes safe ownership and runtime resources. After that, integrate the producer behind the existing default-off gate, test it on the host, rebuild and freeze a complete image, then use the previously authorized temporary-boot path without partition or userdata writes.

No phone status was queried; no ADB/Fastboot device command, boot, flash, partition operation, userdata write, or QEMU run occurred.

## Parallel execution record

- Patch/application auditor: confirmed the stable-context fix and the real ordered 0010 → 0012 → 0021 application sequence; read-only, no source edits.
- Build-gate auditor: found the stale historical-delta check and enumerated the exact five post-R20 Stage 0 source changes; read-only, no source edits.
- Phase-4 gate auditor: reviewed the R20/Stage 0 allowlist and the source/build checks; read-only, no source edits. Exact elapsed times were unavailable.
- Main agent: repaired the patch context, strengthened ordered patch tests and exact source review, synchronized and built the HakiOS tree, ran host tests and full build checks, and verified frozen image hashes. No overlapping agent edits or merge conflicts occurred.
