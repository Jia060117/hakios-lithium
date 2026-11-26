# Touch IRQ host policy review — 2026-11-18

Scope: host-only review and deterministic testing of the existing policy model. Read the current AGENTS.md, PLAN.md, OPERATIONS.md and project.json, using targeted slices of repetitive history. No phone queries, USB/device commands, boots, flashes, QEMU, kernel builds or commits were performed. Only the two owned source files and this report were edited. Runner-produced artifacts are isolated in a fresh WSL `/tmp` directory. The shared runner was not edited.

## Findings and changes

Source: `maintenance/hakios-lithium/peripherals/touch-irq.h`.

- Lines 118–178: IRQs formerly incremented `coalesced_count` once for an existing worker and again when masked. It now counts exactly once per IRQ that finds a queued/running worker. A late callback delivered after masking can reserve an idle worker and returns ACCEPTED, without increasing coalesced count. Masking remains a one-shot transition per storm.
- Lines 182–232: the previous completion unconditionally cleared `worker_pending`, losing an IRQ that arrived during a read. Added `lithium_touch_worker_begin`, `worker_running`, and a bounded `worker_pending_again` latch. Events queued before begin are covered by that read; one or more events during the read reserve exactly one follow-up. A new IRQ during that follow-up can reserve another read. Completion without begin is rejected.
- Lines 95–115 and 132–139: initialization deadline failure preserves the previous state; storm-entry deadlines are checked before time, counters, worker reservation, or output actions change.
- Lines 263–354: poll completion calculates a candidate state and commits only after the resulting deadline is representable. EMPTY, DATA, ERROR, backoff doubling, and the final recovery/unmask branch all preserve the original state and clear actions on deadline rejection. Poll ownership survives rejection, so the caller can handle it explicitly. Exact representable UINT64_MAX deadlines remain accepted.
- Lines 203–260: every `start_poll` action now reserves `poll_inflight`, including immediate worker-error recovery. A second due callback returns BUSY; a completion can directly consume the reservation. A latched worker follow-up takes priority over starting a retry poll, while leaving its retry deadline due. Worker reads and polls exclude one another. IRQs may queue a worker while a poll is inflight; begin returns BUSY until that poll completes.
- Lines 142–145 and 319–350: storm recovery requires three consecutive empty polls without intervening IRQ activity or reserved worker work. A callback during an inflight poll invalidates that clean observation. DATA and ERROR break the clean streak. Masked recovery stays at the 20ms base cadence, rather than accidentally switching to 250ms after its first successful poll. The successful final unmask switches back to the normal 250ms cadence.

The header explicitly documents serialized policy calls and ordered scheduling/mask actions, with I/O outside the policy lock. This is a host state machine, not an atomic implementation, interrupt controller driver, or real scheduler. `init` requires a quiescent adapter and is not a cancellation API. Future adapters must call worker_begin before reads and complete each reserved poll exactly once.

## Deterministic tests

Source: `maintenance/hakios-lithium/peripherals/touch-irq-test.c`. Retained six existing groups (correcting/strengthening five) and added ten groups, for 16 groups total.

- Lines 20, 36, 62, 89 and 108: original coalescing, mask/recovery, exact two-error backoff through its 320ms cap, missed IRQ/worker error recovery, invalid completion and overflow checks now follow the explicit read/poll lifecycle. The old test that expected overflow to destroy poll ownership was corrected.
- Line 167: enumerate zero through eight same-timestamp IRQs during a read; distinguish pre-read coalescing, active-read latching, and arrival after completion. Check duplicate begin and completion without begin.
- Line 199: masked idle/queued IRQ counts; IRQ arrival during the third recovery poll; BUSY worker begin while polling; streak restart; exactly one unmask and a later second storm.
- Line 236: DATA and ERROR each invalidate two previously clean storm polls.
- Line 257: failed read with a latched IRQ preserves the follow-up; polling is deferred until it drains; duplicate due/completion does not produce duplicate work.
- Line 283: byte-for-byte state rollback for all three poll results across normal, poll-only and recovering-storm states (nine combinations), followed by representable retry. Covers rollback of would-be worker queue and unmask actions.
- Line 311: byte-for-byte storm-overflow rejection with queued and idle workers, failed reinitialization preserving state, and deadline helper boundaries.
- Line 342: backward-time rejection preserves active worker and poll ownership with no actions.
- Line 367: poll due boundary and inclusive 100ms storm-window boundary versus one microsecond outside it.
- Line 390: IRQs during three successive reads, including transfer failure and storm entry, retain every required follow-up.
- Line 412: exact maximum deadlines, transactional rollback when a second poll failure would double the delay past UINT64_MAX, and immediate worker-error polling at UINT64_MAX.

## Commands and results

The requested `--suite touch-irq` was attempted with a fresh `--build-dir`; the existing runner rejected it with `Unknown suites: touch-irq`. It registers filename stems, so the existing name is `touch-irq-test`. The runner was left unchanged. The successful final invocation from PowerShell was:

```powershell
wsl.exe -d Ubuntu -u root --exec systemd-run --quiet --wait --pipe --collect -p MemoryMax=14G -p MemoryHigh=12G -p MemorySwapMax=1G -p CPUQuota=1200% -p TasksMax=1024 -p User=jia /usr/bin/python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py --suite touch-irq-test --build-dir /tmp/ios-mix1-touch-irq-review-20261118-c
```

Exit code: 0. Output:

```text
BUILD touch-irq-test.c
touch-irq: 16 deterministic host-policy groups passed (read races, storm recovery, transactional deadlines, poll ownership)
PASS 1 peripheral test suites (ASan and UBSan)
```

Compiler: Ubuntu clang version 18.1.3 (1ubuntu1). Runner flags include C11, pedantic, Wall/Wextra/Werror, O1, ASan and UBSan; sanitizer options halt on first error. No sanitizer finding was reported. `git diff --check` passed for the owned sources.

Final evidence: `/tmp/ios-mix1-touch-irq-review-20261118-c/passed.json` (WSL; temporary host evidence, not a repository artifact). Source hashes read back from that manifest and independently checked against the workspace:

```text
touch-irq.h      84c1b2f8272d5a1e1f9b945653d7747c1aaa96fe65ff2fc46ed1f8fa75800ff2
touch-irq-test.c dafe9ce37f6fdfb12576c2d82c9b5e1f7aa4e103d5fe1c3abaf0e53bad7ee3e9
test executable 8446e40a9549b8984c072a55abfad6ae6905b9eb0f51b99b7f1fe241800138f4
```

## Unverified

These tests exercise deterministic serialized interleavings, not actual parallel threads or interrupt/preemption behavior. Adapter locking, callback lifetime/cancellation, retry scheduling after BUSY, and action application ordering still require integration review. No active kernel or package was changed. Physical GPIO/TLMM/GIC mask/unmask, IRQ polarity/acknowledgement, delivery during real I2C traffic, controller FIFO/read behavior, physical touch/HID events, latency and storm recovery on hardware remain unverified. No hardware support or device acceptance is claimed.
