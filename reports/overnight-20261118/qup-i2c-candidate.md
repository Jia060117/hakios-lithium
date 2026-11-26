# QUP v2 I2C candidate

Historical first candidate description. The accepted revisions and final tests
are in `qup-review.md`, `qup-transfer.md`, and `accepted-host-tests/passed.json`.
The current tag planner requires route attestation only for IRQ mode; polling
does not require it. Its combined-message tag plan is not an executing FIFO
driver. The separate FIFO candidate still lacks repeated START.

The new `maintenance/hakios-lithium/peripherals/qup-i2c.h` and `qup-i2c-test.c` are a source-backed host model for the smallest FT5x46 transfer. They do not access the phone or kernel MMIO.

Linux v6.12 `drivers/i2c/busses/i2c-qup.c` supplies the checked contract: QUP register offsets and v2 tags are declared at lines 1-110; v2 tag construction is at lines 542-586; v2 transfer setup soft-resets and polls RESET at lines 1536-1565; v2 mode selection and transfer sequencing are at lines 1573-1589; input/output block and FIFO sizing is at lines 1817-1850.

The candidate constructs the FT5x46 two-message sequence for address `0x38`: write register byte, repeated START, read up to 256 bytes, and STOP. It requires controller ownership, clocks, pins, rail state, and GIC route to be explicitly marked verified before producing a plan. It tests the 63-byte stock report path, write-only path, 256-byte length encoding, malformed lengths/addresses, invalid state and missing preconditions.

This is not an implementation of QUP registers, TLMM pin mux, clocks, PM8994 regulators, IRQ registration, FIFO packing, timeout polling or I2C completion. Those remain **未验证** and must be implemented only after the platform ownership and handoff state are established.
