# QUPv2 combined register-read: isolated host-only next phase

2026-11-18. Host candidate and sanitizer tests passed. Independent development
source only: no production registration, kernel build, image regeneration,
device query/command/boot, partition operation, QEMU, or Git staging/config/commit.
Physical touch has NOT been tested or established. Device state was not queried.

## Write scope and preservation

Only these new files were written (the new directory was created as necessary):

- `maintenance/hakios-lithium/peripherals-next/qup-combined.h`
- `maintenance/hakios-lithium/peripherals-next/qup-combined-test.c`
- `reports/overnight-20261118/touch-combined-transfer-next.md`

Existing peripheral implementation and tests were read, compiled as standalone
host tests, and preserved. Before/after SHA256 checks matched:

| Existing file under maintenance/hakios-lithium/peripherals | SHA256 |
| --- | --- |
| qup-transfer.h | 0ccffd2a7b55d8522d8afe99674f440f7c3b0596bef858627579bd713c828680 |
| touch-i2c.h | e5a4673584fae9b12cde7f47773673fa25cea44ed32149ad2252d9a144134e08 |
| qup-transfer-test.c | c4e23a0e4f4d49a10902f3bed79c42b8ba4581c1b6eef7d2929be408e15556ae |
| touch-i2c-test.c | 27b7ae213b3528ee27fe5be6a7c6f4205da1a998a32e3e411541490ae9c62152 |

The complete set of 149 R19 live hashes was not re-read or regenerated. This
work makes no new assertion that all 149 were independently verified here.
No existing repository file was intentionally modified. Test executables used
anonymous Linux memfd objects; no repository binary, log, or build output was
created. Clang uses its normal transient compilation intermediates.

## Pinned source verification and behavior

The existing local Linux repository was reused; nothing was cloned or fetched.
Read-only commands, from `F:/IOS-MIX1`:

```powershell
git --no-optional-locks -C artifacts/reference-source/overnight-20261118/linux-v6.12 rev-parse HEAD
git --no-optional-locks -C artifacts/reference-source/overnight-20261118/linux-v6.12 rev-parse HEAD:drivers/i2c/busses/i2c-qup.c
git --no-optional-locks -C artifacts/reference-source/overnight-20261118/linux-v6.12 show adc218676eef25575469234709c2d87185ca223a:drivers/i2c/busses/i2c-qup.c
```

Results: commit `adc218676eef25575469234709c2d87185ca223a`; driver blob
`d480162a4d3941042cb59eb69aec0e816276761a`. This source is available through
Git objects rather than a checked-out driver path; references below are line
numbers in that exact blob.

| Linux i2c-qup.c lines | Verified behavior used |
| --- | --- |
| 45-123 | RESET=0, RUN=1, PAUSE=3; error/service masks; pack/unpack; v2 tags; count-reconfiguration bit 31 |
| 280-372 | Observed error W1C; FIFO write service can complete before OUTPUT_DONE; RX completion follows consumed data |
| 376-431 | State-valid polling, write state, poll target state |
| 434-453 | Final BUS_ACTIVE check; this candidate uses bounded existing waits rather than copying the source's non-breaking timeout loop |
| 542-586 | START/address each message; nonfinal write DATAWR=0x82; final read DATARD_STOP=0x87 |
| 897-912 | Timeout software reset and NACK/error distinction |
| 1121-1145 | FIFO counts and NO_INPUT for the write phase; no RX count write for a zero-RX phase |
| 1152-1172 | Set FIFO mode once in RESET and clear unused block counters |
| 1190-1244 | Discard two received tag bytes, unpack payload; source does not specify their values for validation |
| 1252-1282, 1306-1333 | Little-byte-first FIFO packing and partial last word |
| 1341-1418 | Two separately configured FIFO subtransfers; initial RUN/clock/PAUSE, then PAUSE and CONFIG_DURING_RUN between messages |
| 1425-1475, 1530-1598 | Per-message lengths and read RX overhead; last-message selection, final idle/reset |
| 1820-1879 | Discovered block-size encodings and FIFO size calculation; no board defaults imported |

## Implemented contract and line references

Paths in this section are under `maintenance/hakios-lithium/peripherals-next/`.

- `qup-combined.h:24`, `lqc_pack`: separate FIFO words for a 5-byte write
  message and a 4-byte read command. For address 0x38/register r/length n the
  **consumed** stream is `81 70 82 01 r 81 71 87 n`. Padding in the second
  word is not transmitted because first-phase MX_WRITE_CNT is 5. This is not
  a single packed nine-byte submission and not two STOP transfers.
- `qup-combined.h:43`, `lqc_wait_phase`: bounded FIFO service and error handling
  using the existing attestation, MMIO, time, error and state helpers.
- `qup-combined.h:89`, `lqc_read_register`: address/length/capacity/callback
  validation before callbacks, all eight attestations required, discovered
  FIFO capacities, RESET configuration and master check. First phase sets
  NO_INPUT and MX_WRITE_CNT=5. A potentially stale MX_READ_CNT is irrelevant
  under NO_INPUT, matching the pinned source. After write service it pauses
  without idle/reset/STOP, writes bit31|4 and bit31|(n+2) to the FIFO write/read
  counters, clears NO_INPUT, queues read tags and resumes. Only final read is
  followed by idle verification and reset. Read output remains staged.
- `qup-combined.h:151`, cleanup: independent local timeout **clamped to the
  original absolute deadline** and finite polls. After the absolute deadline,
  no hardware cleanup is attempted. Cleanup then explicitly reports
  STATE_TIMEOUT; hardware state is unverified. Before deadline, successful
  transfers use state RESET; failures attempt SW_RESET. Both require reset
  confirmation and a bounded idle/error check. The original primary error is
  preserved if cleanup fails. Output commits only if all checks, including
  final deadline verification, succeed. No target-side rollback or retry.
- `qup-combined.h:192`, `lqc_touch_release_allowed`, and `:195`,
  `lqc_touch_write_read`: adapter requires a real external claim. Failed
  cleanup sets persistent `poisoned`, retains `last` outcome and returns
  IO_ERROR with zero published counts. Subsequent adapter calls reject with
  **zero callbacks**, even with a later deadline. NACK is retryable only after
  successful reset/idle cleanup. No automatic clearing of poison.
- The platform-provided release callback MUST inspect poison and keep ownership
  quarantined when release is disallowed; it cannot just drop the claim. The
  existing touch sequence always invokes release on failure. This requirement
  is explicit because no real platform claim/release backend exists here. Only
  verified platform recovery plus renewed prerequisites may clear poison.
- Direct users of `lqc_read_register` must likewise quarantine on non-OK cleanup;
  the function returns the unverified cleanup state but cannot police an external
  owner's bookkeeping. Callback execution itself must remain bounded and MMIO
  ordering/fault semantics must be provided by the platform.

## Exact final test command and results

Run in PowerShell at `F:/IOS-MIX1`; WSL Clang 18.1.3, x86_64 Linux. The process
number in the emitted `/proc/PID/fd/3` executable path varies by run.

```powershell
@'
import os, subprocess
root='/mnt/f/IOS-MIX1/maintenance/hakios-lithium/'
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
for source in ['peripherals-next/qup-combined-test.c','peripherals/qup-transfer-test.c','peripherals/touch-i2c-test.c']:
 fd=os.memfd_create('qup-host-test',0)
 exe=f'/proc/{os.getpid()}/fd/{fd}'
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',root+source,'-o',exe]
 print('COMPILE:', ' '.join(cmd),flush=True)
 subprocess.run(cmd,check=True)
 subprocess.run([exe],env=env,check=True)
 os.close(fd)
'@ | wsl --exec python3 -
```

Final command exited **0**, no ASan/UBSan diagnostics. Test output:

```text
MMIO failure sweep: 1792 cases (all lengths 1..32)
Error-path MMIO sweep: 117 cases; deadline poison/stale RX count PASS
PASS: combined FIFO lengths/tags/gates/capacity/faults/cleanup/32+31 adapter
PASS injected fault at all 42 MMIO calls (write)
PASS injected fault at all 37 MMIO calls (read)
PASS qup-transfer: packing, 64 transfers, gates, every MMIO fault, bounded errors/cleanup
PASS touch I2C: split report, runtime probe, ownership, retries, deadlines and transactional output
```

New tests: `qup-combined-test.c:129` all lengths with canaries and exact consumed
tags; `:144` invalid arguments/capacity and every missing attestation with zero
callbacks; `:184` each successful-path MMIO operation failed individually for
all 32 lengths, NACK/core/bus errors, missing completion/state/master, stuck bus,
frozen/backward clocks, rejected transitions and failed cleanup; `:246` actual
touch read-report adapter completing 32/31 reads or preserving the entire old
report on second-read failure; `:271` stale read count, absolute deadline with
no post-deadline reset, persistent poison and denied reuse/release, second-phase
errors, 117 individual error-path MMIO faults including W1C and reset cleanup,
minimum FIFO capacity and overflow, quarantined release through the touch
sequence, and safe NACK retry after confirmed cleanup.

## Physical prerequisites and remaining boundaries

Every board prerequisite remains physically unverified in this work: actual
QUP instance/layout and ordered device mapping; controller AND target power;
core/iface clocks, deasserted resets and actual timing source; correct pin mux,
open-drain electrical behavior, pullups and voltage; exclusive ownership and
IRQ exclusion; fault-contained bounded callbacks and monotonic time; target
identity/address/register behavior. There are no inferred power/clock/mux
defaults or real platform recovery/claim/release implementations.

The mock demonstrates software sequencing, bounds and publication behavior, not
electrical repeated START/STOP on a phone. No physical touch, IRQ delivery, frame
latching across split reads, live I2C reliability, UI event delivery or production
integration is verified. Sizes above 32, zero-length/10-bit messages, DMA/BAM,
block mode, GPIO recovery and concurrency are outside this candidate.
