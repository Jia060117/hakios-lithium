# R20-display touch host follow-up

Date: 2026-11-19. This is host-only verification; no phone query, boot, flash, partition, userdata, ADB/Fastboot device command, or QEMU was used. R20-display remains the sole accepted display baseline; R19 is archived.

## Result

The existing 14-suite peripheral regression and 10 isolated follow-up suites all passed under Clang with AddressSanitizer and UndefinedBehaviorSanitizer. The retained evidence records 21 successful command records: one existing-regression invocation, ten builds, and ten test executions. It hashes 51 source/evidence inputs, including the legacy regression runner and its accepted input record.

Evidence: reports/touch-stage0-complete-host-20261119-r2/passed.json

Evidence SHA-256: def9fb7d3d766623cec56e0962d27c6a84ef9e2e86aa4dd45d936511145b59d4

Follow-up suites: qup-combined, touch-delivery, irq-affinity-audit, a5xx-fence-packet, ft5x46-frame, touch-stage, touch-static, touch-stage-trace, qup-clean-entry-safety, and touch-submit-deadline. The deadline test now also covers cleanup fencing against the latest of two accepted sequences.

Reproduction command:

    wsl.exe -d Ubuntu --cd /mnt/f/IOS-MIX1 --exec python3 scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/reports/<new-evidence-directory>

Use a new directory: the runner refuses to overwrite retained evidence. This follow-up changed only host tests/evidence and status documentation. The full production build was already completed in reports/hakios-ios16-touch-stage0-build-20261119.md/json; its 14+7 test count describes that earlier run, whereas this follow-up is 14+10. The earlier build report was not rewritten as a new build.

R20-display SHA-256 remains 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82; R15 remains ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55. Both were checked before and after the run.

## What this proves

The host models cover parser gates, QUP transfer fault isolation, FT5x46 frame parsing, touch state and queue behavior, IRQ affinity policy, A5xx fence packet construction, trace/deadline handling, and safe cleanup boundaries.

The production Stage 0 kernel was not rebuilt in this follow-up. The previously built kernel remains SHA-256 3e130b765b47ec06ac5e5392d5f94301d22ed43fa37b937827b65ab220180c93; current source and active workspace hashes were independently checked, and the kernel contains the Stage 0 static resource line plus the Stage 1–3 qup6_handoff_unverified fail-closed marker.

There is still no physical touch producer or touch package. The tests do not prove QUP6 access, real GPIO interrupts, runtime controller reports, IOHID delivery, or physical touch behavior.

## Board evidence and remaining limits

The stock DTS at artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4844-4883 configures the FT5x46 family on I2C12 at 0x38, reset GPIO89, and L14/LAB/IBB supplies. GPIO87/88 use the I2C pin group, but the DTS does not establish their individual SDA/SCL roles. The same DTS gives GPIO105 in the generic interrupts property and GPIO125 in the vendor irq-gpio property. GPIO125 is a static candidate, not a confirmed runtime IRQ.

Historical Android registration at reports/local/20261112-142718-199681/inventory.json:192 lists ft5x46 at 75ba000.i2c/i2c-12/12-0038, event4. It contains no raw touch reports and does not prove the current controller state or UEFI-to-XNU handoff. No new device dump was taken.

Cleanup callbacks are required to be independently bounded, but the synchronous host state machine cannot preempt an arbitrary stuck callback. This remains an integration requirement for a real backend; the passing immediate-return mocks do not validate its hardware timeout behavior.

## Parallel execution and independent review

- Avicenna the 2nd: read-only current source/kernel/report audit. Confirmed the three production peripheral input hashes and actual 14,917,896-byte Stage 0 kernel hash/markers; distinguished the older build's 14+7 suites from this follow-up's 14+10. Main agent independently compared port versus active WSL workspace hashes, read kernel markers/symbols, and verified the frozen kernel hash. No edits or merge conflict.
- Huygens the 2nd: read-only runner/deadline-test review. Confirmed 14+10 suites and 21 command records; identified missing legacy-runner provenance and missing multi-frame latest-sequence cleanup coverage. Main agent verified and corrected both, then reran the complete 24-suite regression into the new r2 evidence directory. Reviewer did not rerun tests. No overlapping writes.
- Main agent: integrated three missing suite registrations, retained the prior deadline-test fix, added latest-sequence cleanup coverage and provenance hashes, ran both complete host regressions, verified immutable baselines, and updated status records. No production MMIO/IRQ/HID or build change was made in this follow-up.

An initial attempt to create fresh reviewers hit the open-thread limit. Completed prior agents were collected and closed; two fresh reviewers were then used. Prior agents' old workspace mismatch findings were not accepted as live evidence: current readback disproved those stale findings. Exact per-agent elapsed durations are unavailable; both new reviews completed during the host follow-up. No agent result was merged without main-agent verification.

## Next gate

Before enabling any touch MMIO, establish an auditable UEFI-to-XNU QUP6 ownership contract covering transaction quiescence, clock and pinmux state, shared rail preservation, GPIO105 versus GPIO125 IRQ selection, and GIC routing. After that, implement only a bounded default-off controller-ID/read probe; keep IRQ and HID disabled until raw-frame evidence is reliable.

The later intended steps are controller ID, bounded raw-frame polling plus GPIO correlation, then event delivery and finally IRQ routing. All require a new reviewed production backend, complete HakiOS build, package inspection and freeze before a device run; the current static Stage 0 package does not meet those conditions.

## Text-only checkpoints

Checkpoint exports are reports/hakios-ios16-touch-followup-host-20261119.patch (tracked source/status overlay against HEAD, including pre-existing edits in those named files), reports/hakios-ios16-touch-runner-20261119.patch (new host runner), and reports/hakios-ios16-touch-deadline-test-20261119.patch (new deadline test). These are selected-file checkpoints, not a standalone repository or firmware package. Git staging was preserved; unrelated existing work was not committed or reset. No proprietary firmware or binary was exported.
