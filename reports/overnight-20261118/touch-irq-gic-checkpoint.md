# Touch IRQ policy and GIC host checkpoint

Historical checkpoint. Later work supersedes the IRQ test result and the
admission-integration status below: see `touch-irq-review.md`, `qup-review.md`,
`qup-transfer.md`, `accepted-host-tests/passed.json`, and the final `REPORT.md`.
The stopped agents did acquire reference material; they delivered no accepted
code/report within their bounded window. Subsequent agents completed disjoint
reviews. A compiled default-off admission hook now reports missing backends; it
still performs no MMIO or physical touch/GPU operation.

Date: 2026-11-18. Scope is source review and host-only policy tests. No phone, partition, userdata, or QEMU operations were performed. No firmware was fetched or added. The R15 desktop image remains unchanged.

## Touch IRQ policy model

Added `maintenance/hakios-lithium/peripherals/touch-irq.h` and `touch-irq-test.c`. This is a pure state model: no MMIO, GPIO, GIC, clock, interrupt registration, I2C, timer, or kernel scheduling calls are present.

The model represents one coalesced worker, counts interrupts in a 100ms window, requests masking after the 33rd interrupt, and schedules polling. Two consecutive poll errors double the retry interval up to 320ms. Three empty polls clear the storm state and request IRQ unmask. Polling can discover data after a missed edge and queues the worker. Invalid/nonmonotonic times and arithmetic overflow are rejected. The kernel adapter must still guarantee callback context, serialized state access, line ownership, and bounded work.

Host test command (Ubuntu WSL, Clang 18.1.3, ASan+UBSan):

```text
/usr/bin/python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py --suite touch-irq-test --build-dir /mnt/f/IOS-MIX1/reports/overnight-20261118/touch-irq-test-2
```

Result: PASS, one targeted suite; build and execution completed under the configured 14GiB/1200% CPU systemd resource cap. Source and test binary hashes are in `touch-irq-test-2/passed.json`. The earlier first run also passed before an additional poll-only cadence/overflow edge-case correction; the second run is the final evidence.

## GICv3 source audit

Fixed upstream source: `upstream/hakios-xnu-kernel/xnu/files/osfmk/arm64/gic_v3_irq.c`. `GICD_IROUTER(n)` is declared at line 56; `gic_v3_spi_attach()` begins at line 225. It accepts an SPI and handler, configures group/priority/trigger, reads current `MPIDR_EL1`, writes the masked Aff3/Aff2/Aff1/Aff0 value to that INTID's IROUTER at lines 262–263, settles the distributor at 264, then unmasks at 265. Initialization requests nonsecure affinity routing by setting ARE_NS at lines 484–485. This does not verify effective ARE_NS by readback, route readback, an online affinity target, or actual SPI delivery. Distributor settle and detach busy loops are not bounded in this source.

The existing host fake-GIC model `maintenance/hakios-lithium/peripherals/irq-route.h` requires the caller to identify an owned INTID, confirms `GICD_TYPER` bounds, masks before route changes, checks ARE, checks target affinity/online status, verifies IROUTER with IRM clear, and only then unmasks. Its tests cover all 988 standard SPI offsets and injected failures. It is not connected to active GIC code.

Lithium route arithmetic from the already audited matched stock DT: touch TLMM parent SPI 208 maps to GIC INTID 240 and `GICD_IROUTER` offset `0x6780`; GPU SPI 300 maps to INTID 332 and offset `0x6a60`. The online boot CPU is `phys/affinity 0x1` according to R15/R16 device logs. **The current hardware route register and physical SPI delivery to 0x1 remain unverified.** Touch TLMM child GPIO125 is not itself a GIC INTID.

## Still unverified / next safe work

- No actual QUP v2 controller driver or kernel touch/HID integration was built. Existing `touch-i2c.h` is a callback contract only. Do not claim physical touch support.
- No GPU kernel backend or A530 packet execution was built. Existing GPU files are host policy/protocol models only. Do not claim GPU probe, shader, render, or acceleration support.
- Hardware ownership and current states for TLMM/QUP/GPIO125/89, PM8994 L14/LAB/IBB, GPU clocks/GDSC/SMMU, and SCM/PAS were not measured. No register access should be attempted until those preconditions are established.
- Morning validation should start with the unchanged R15 baseline (`bootcpu=1 cpus=1`, expected affinity `0x1`, no new peripheral flags). New touch/GPU switches are not integrated into a kernel package and must not yet be passed as if implemented.

## Parallel execution record

Four bounded agents were assigned disjoint scopes: IRQ storm model; public GPU reference provenance; QUP I2C source-backed candidate; A5xx packet candidate. They produced no reviewable files before the bounded window ended and were stopped. No agent changes were accepted or merged; therefore there are no agent code claims. The main agent independently checked current workspace contents, ran the completed touch IRQ test, and performed the GIC source review above. The pre-existing touch, GPU, and IRQ model files are prior workspace content, not agent deliverables from this run.
