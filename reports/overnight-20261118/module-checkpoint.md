# Peripheral source checkpoint — host only

Scope: source-backed protocol helpers and explicit host models, before the
production-admission checkpoint. See final REPORT.md for full acceptance status.
No physical touch/GPU capability is claimed.

Accepted component set: FocalTech frame/I2C contracts, digitizer/queue mapping,
IRQ/poll policy, owned GIC-route policy, QUPv2 tag planning and single-message
FIFO candidate, GPU probe/firmware/ring/fence/IOVA models. The final combined
14-suite execution was independently run by the main agent with ASan/UBSan;
its source-bound manifest is `accepted-host-tests/passed.json` (recorded in the
subsequent integration checkpoint because it also covers production admission).

The remaining contract mismatch is explicit: QUP FIFO single-message transfers
do not implement the repeated-START register-read callback required by touch.
No board MMIO/clock/regulator backend or hardware GPU stage was merged.

Agent ownership and main review:

- Hume: touch-irq.h/test fixes; main reviewed coalescing/read-race/timeout changes
  and reran all 16 deterministic groups in the combined suite.
- Kuhn: qup-i2c.h/test; main verified polling does not require IRQ routing and
  reran all 32 prerequisite combinations and 65,792 tag plans.
- Noether: qup-transfer.h/test; main checked source attribution, gates and cleanup,
  then reran all 64 length/direction transfers and injected MMIO failures.
- Epicurus: corrected GPU source-chain reports; main checked exact pinned source
  generation dispatch and rejected the older claim that Turnip .cc files were absent.
- Main: touch-frame/I2C/digitizer/queue and GPU/IRQ model assembly, test runner
  integration and independent final test execution. Earlier read-only source
  contributors are attributed in the audit and final parallel record.

Known-good desktop file remains `baseline-desktop-r15.img`, SHA256
`ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`.
Backup ref: `archive/ios16-peripherals-before-final-20261118`. A verified local
source-only ZIP with 431 text files also preserves pre-existing dirty R12–R18
sources; no unrelated work is reset or included wholesale in this checkpoint.
