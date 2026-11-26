# QUP plan generator host review — 2026-11-18

Scope: host-only review in `F:/IOS-MIX1`. Read `AGENTS.md`, `PLAN.md`,
`OPERATIONS.md`, and `project.json`; retain the complete HakiOS baseline.
No phone queries, boot, flash, QEMU, kernel build or shared-runner changes.
Only these three repository files were edited for this task:

- `maintenance/hakios-lithium/peripherals/qup-i2c.h`
- `maintenance/hakios-lithium/peripherals/qup-i2c-test.c`
- `reports/overnight-20261118/qup-review.md`

## Supplied source and contract

Read directly from the local Git objects, without a checkout or network fetch:

```text
git -C artifacts/reference-source/overnight-20261118/linux-v6.12 show HEAD:drivers/i2c/busses/i2c-qup.c
HEAD: adc218676eef25575469234709c2d87185ca223a
blob: d480162a4d3941042cb59eb69aec0e816276761a
```

Source lines 46–51 define RESET=0, RUN=1, PAUSE=3, mask=3 and VALID=bit2.
Lines 376–401 require VALID and compare only the requested state bits.
Lines 106–119 define v2 tags and the 256-byte limit; lines 542–586 build
START/address and final/non-final transfer tags, including zero meaning 256
at lines 580–584. Lines 149 and 1434 establish the separate Linux non-DMA
receive chunk limit of 254 payload bytes, leaving two RX tag bytes.

## Findings and changes

1. `qup-i2c.h:49,88–94`: removed the mandatory GIC route gate from plan
   generation. Ownership, enabled clocks, configured pins and verified rails
   remain mandatory. IRQ route metadata is advisory for this abstract model;
   a polling caller can generate the same tags without claiming IRQ readiness.
2. `qup-i2c.h:127–145`: explicitly allow only RESET, RUN and PAUSE requests.
   The old comparison accepted `state=4, required_state=4` as RESET because
   the request flag overlapped VALID. Reserved state 2, VALID-bearing requests
   and high-bit requests now return BAD_STATE. NULL preconditions return
   INVALID; observed unrelated status bits remain ignored.
3. `qup-i2c.h:88–121`: preserve null, address and length validation and existing
   error precedence; construct a local result and publish it once. Every
   rejected plan call leaves any supplied output byte-for-byte unchanged.
   Only one register write byte is supported; reads are optional, bounded
   at 256. No silent truncation of SIZE_MAX or a 256-byte write is permitted.
4. `qup-i2c.h:69–81,104–119`: document the actual tag-level boundary. Read 0
   means write-only DATAWR_STOP; read 256 encodes length tag 0 and retains
   rx_length=256. Read messages use a repeated START and DATARD_STOP. The
   separate register payload belongs after the first four tag bytes. This
   concatenated tag array is not directly executable as a FIFO stream.
   Corrected the earlier non-DMA wording: a future Linux-style non-DMA
   adapter must rebuild/chunk 255/256-byte reads at 254, rather than execute
   this abstract single-read tag plan unchanged.

## Targeted host verification

The pre-edit suite passed, demonstrating that the old tests missed both
defects. The expanded suite passed after the fixes, on 2026-11-18 at about
19:24 Asia/Shanghai, using Ubuntu Clang 18.1.3, C11, pedantic warnings as
errors, ASan and UBSan:

```text
wsl --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py --suite qup-i2c-test
BUILD qup-i2c-test.c
qup-i2c: 65792 encoding plans; all 32 prerequisite combinations, NULLs, bounds, state matrix and error atomicity passed (host tag model only)
PASS 1 peripheral test suites (ASan and UBSan)
```

- `qup-i2c-test.c:15–65`: sentinel-byte error atomicity helper, all 32 boolean
  prerequisite combinations for reads 0/63/256 and RESET/RUN/PAUSE, all seven
  NULL argument combinations, and NULL state-validation inputs.
- `qup-i2c-test.c:69–120`: exact FT report tags and 65,792 successful plans
  (128 seven-bit addresses × 257 read lengths × both IRQ metadata values).
  Includes 254/255/256 boundaries, all register byte values, write-only
  STOP, zeroed stale read fields, exact tags and decoded payload lengths.
- `qup-i2c-test.c:123–153`: all invalid representable addresses 128–255;
  unsupported write lengths 0/2/255/256/257/SIZE_MAX; oversized reads
  257/65535/SIZE_MAX; multiple-invalid-input precedence and output atomicity.
- `qup-i2c-test.c:156–178`: 16 requested states × 256 observed low-byte states
  × three IRQ/high-status variants = 12,288 comparisons, plus three high-bit
  malformed requests. Missing VALID and mismatched/reserved states reject.

Two temporary-directory regression controls restored each defect separately:
mandatory IRQ routing and unchecked required_state flags. Both compiled
with strict warnings and failed assertions as expected (exit -6). Temporary
copies were removed automatically; the repository header was not mutated
for these controls. Both owned C sources also passed trailing-whitespace
and final-newline checks. The shared runner was invoked unchanged and its
temporary build mode was used; no test artifacts were added to the repository.

## Unverified / absent

This is a host tag/state model, not a QUP register driver. Actual ownership
handoff, controller base/version, clock rates, TLMM mux, regulators, GIC
routing, IRQ registration, FIFO packing, RX tag stripping, 254-byte chunk
execution, bounded polling/timeouts, NACK/error recovery, I2C completion,
FT5x46 physical reads and touch delivery remain unimplemented here and/or
unverified on hardware. A verified-boolean input is a caller assertion, not
evidence of device readiness. No kernel integration or phone result is
claimed, and the broad peripheral suite was not rerun for this bounded task.
