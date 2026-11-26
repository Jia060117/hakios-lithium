# QUPv2 bounded FIFO transfer candidate — 2026-11-18

Result: host-only implementation and ASan/UBSan tests pass. The hardware backend
remains UNVERIFIED. No phone, QEMU, kernel build/integration, device query, boot,
partition write or commit was performed by this task.

## Ownership and source

Only these repository files were created/edited by this task:

- `maintenance/hakios-lithium/peripherals/qup-transfer.h` (289 lines).
- `maintenance/hakios-lithium/peripherals/qup-transfer-test.c` (375 lines).
- This report.

`qup-i2c.h` and its unfinished API were neither edited nor imported. Shared
instructions, plans, configuration, runner and kernel integration were not edited.
Read AGENTS.md, PLAN.md, OPERATIONS.md and project.json; the active kernel remains
the complete HakiOS baseline. Other agents' existing changes were left alone.

Pinned local source, accessed through `git show HEAD:drivers/i2c/busses/i2c-qup.c`
in `artifacts/reference-source/overnight-20261118/linux-v6.12`:

- Linux v6.12 commit: `adc218676eef25575469234709c2d87185ca223a`.
- Driver blob: `d480162a4d3941042cb59eb69aec0e816276761a`.
- Registers, bits and v2 tags: driver lines 27–160.
- Error masking/W1C, FIFO service completion: lines 280–374. FIFO write service
  signifies completion even when MX_OUTPUT_DONE lags (lines 358–365).
- State-valid/state/master polling and transitions: lines 376–432.
- Tags: lines 542–586; counts/mode: lines 1121–1172.
- Receive tag skipping and byte extraction: lines 1191–1245; little-byte-first
  FIFO packing: lines 1252–1282.
- RESET/RUN/clock/PAUSE/preload/RUN sequence: lines 1342–1414; software reset,
  minicore and master-enable initialization: lines 1536–1603.
- FIFO capacity decoding and clock/platform dependencies: probe at line 1655
  and subsequent code. No lithium instance base, GCC, pin or regulator addresses
  were inferred from these generic driver definitions.

## Implemented subset and entry points

`qup-transfer.h:188`, `lqt_transfer`: one nonempty 7-bit message, 1–32 bytes,
with STOP. Independent TX/RX FIFO capacity is decoded from IO_MODE after reset;
the complete tag/data stream must fit. Reserved block-size encodings and
oversized streams fail before FIFO writes. No dynamic allocation or DMA.

- Lines 46–89: eight mandatory platform attestations and callback contract.
- Lines 98–120: bounded STATE_VALID, master-generation and RESET/RUN/PAUSE waits.
- Lines 125–144: pure transactional v2 packing; no output/count changes on failure.
- Lines 147–180: source-backed NACK/core error masks, observed-bit-only W1C,
  finite bus-idle polling. Status and error classification use the same sample.
- Lines 188–273: reset/configuration, FIFO preload, service polling, RX staging.
- Lines 274–287: independently budgeted cleanup; successful transfers use state
  reset, failed transfers use software reset and confirm STATE_VALID/RESET.

Required attestations: correct MMIO instance/mapping/extent and memory type; QUPv2
layout identification; controller/target power and voltage; enabled core/iface
clocks and released resets; verified pinmux/open-drain/pullups; validated clk_ctl
against the actual clock rate; exclusive ownership including IRQ handlers and
firmware; ordered, bounded callbacks and a monotonic microsecond clock. They must
remain valid through cleanup. The engine supplies no defaults for these facts.
Missing attestations cause zero callbacks, including zero MMIO/time/delay calls.
Low-level state/idle/error helpers also check the gate. Invalid inputs are rejected
before callbacks. The callbacks must themselves return in bounded time; this
code cannot interrupt a hung callback or recover from a CPU MMIO abort by itself.

Each wait has 1–256 polls and a shared main deadline of 1–1,000,000 microseconds.
Cleanup gets a fresh deadline and its own bounded state polls. Frozen clocks
cannot make a loop infinite; backwards clocks expire conservatively, and unsigned
elapsed arithmetic supports a natural uint64_t wrap. Linux's bus-active loop at
lines 435–453 records timeout without exiting; that unbounded behavior was not
copied. Persistent BUS_ACTIVE returns BUS_STUCK. This does not identify physical
SCL versus SDA or another cause, and reset does not prove electrical recovery.

RX data commits only after receive service, bus idle and confirmed cleanup.
Partial RX, NACK, MMIO faults, deadline/poll exhaustion and cleanup failure leave
the caller's complete RX buffer unchanged. Original failure and cleanup result
are separate fields; otherwise-successful cleanup failures become
LQT_CLEANUP_FAILED. WRITE effects on a target cannot be rolled back; transfers
are never retried. Linux skips two RX tag bytes without specifying their values;
this subset likewise skips them and does not invent a response-tag validator.

## Verification

Final isolated invocation, using the existing runner as found, fresh directory
outside the repository; command exit 0:

```text
wsl --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py --suite qup-transfer-test --build-dir /mnt/c/Users/Jia33/AppData/Local/Temp/iosmix1-qup-transfer-20261118-final-1932
```

Compiler: Ubuntu clang 18.1.3. Runner: C11, pedantic/Wall/Wextra/Werror, O1,
ASan and UBSan, both halt-on-error. Final stdout:

```text
BUILD qup-transfer-test.c
PASS injected fault at all 42 MMIO calls (write)
PASS injected fault at all 37 MMIO calls (read)
PASS qup-transfer: packing, 64 transfers, gates, every MMIO fault, bounded errors/cleanup
PASS 1 peripheral test suites (ASan and UBSan)
```

No sanitizer diagnostics; test stderr empty. Recorded test runtime: 0.022925 s.
`passed.json` and the test executable remain in that temporary build directory.
This final directory is on the Windows filesystem, outside the repository; earlier
passing runs used volatile WSL /tmp directories. Current source hashes were
matched against the final runner manifest immediately after the test run.
The shared runner's manifest also snapshots neighboring source hashes; those
snapshots are not dependencies on their APIs or evidence about their behavior.

Test locations:

- `qup-transfer-test.c:134`: exact independent golden words, high-bit data, partial
  word zero padding, invalid/huge length and transactional packing rejection.
- `:159`: every missing gate bit, all missing callbacks, invalid bounds/address,
  unsupported state, zero callbacks before gate, low-level helper gate checks.
- `:206`: every length 1–32 in both directions, canaries, counts, clock value,
  reset and bounded call counts.
- `:232`: every individual MMIO fault in full 32-byte write and 9-byte read,
  including FIFO reads/writes and cleanup; NACK/W1C, core/bus errors, frozen
  clock, transfer timeout, pre/post-transfer stuck bus, invalid/missing master
  state, rejected RUN/PAUSE, missing RX service and DONE without RX data.
- `:294`: reserved/too-small FIFOs, deadline expiry, backwards/wrapping clocks,
  failed cleanup polling/write and preserved primary NACK, staged-output rollback.
- `:328`: service completion without DONE flags, exact 8-byte FIFO boundaries,
  delayed state success at the minimum and maximum supported poll budgets.

Final source SHA256:

```text
qup-transfer.h       0ccffd2a7b55d8522d8afe99674f440f7c3b0596bef858627579bd713c828680
qup-transfer-test.c  c4e23a0e4f4d49a10902f3bed79c42b8ba4581c1b6eef7d2929be408e15556ae
test executable     73737cc9f56c76dba8e8cef774f1ddf6b9677061782a87697c45a28d45aab19d
```

## Incomplete / not established

No physical platform facts have been newly verified: BLSP instance identity and
MMIO, clock rate/timing, resets, power/voltage, pinmux/pullups, IRQ ownership,
controller FIFO behavior, electrical SCL/SDA recovery and target responses remain
unverified on lithium. No hardware backend is wired to this candidate.

No repeated START or write-register-address/read combination, 10-bit addressing,
zero-length/SMBus transfers, split/block/BAM/DMA transfers, interrupt integration,
GPIO bus-clear pulses or physical touch support. It is a minimal FIFO-message
engine, not a completed device-register read API or an operational touch driver.
This task neither consumes nor assumes the other agent's unfinished plan-generator
API. No full HakiOS build,
kernel regression or live-device validation was claimed or attempted.
