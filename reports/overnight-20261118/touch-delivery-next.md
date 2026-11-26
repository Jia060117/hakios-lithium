# Independent transactional touch delivery protocol (host only)

Date: 2026-11-18. Workspace: F:/IOS-MIX1. This is independent next-source work, with no kernel integration or physical input backend.

## Scope and preservation

Created only these three previously absent paths:

- `maintenance/hakios-lithium/peripherals-next/touch-delivery.h`
- `maintenance/hakios-lithium/peripherals-next/touch-delivery-test.c`
- `reports/overnight-20261118/touch-delivery-next.md`

No existing file was edited. The main-owned `scripts/test-hakios-lithium-peripherals-next.py` was neither edited nor executed by this task. No Git/config changes, heavy build, phone query/command, boot, partition operation, or QEMU execution occurred. Tests wrote executables only in temporary WSL `/tmp/touch-delivery-*` directories, removed automatically afterward.

Read AGENTS.md, PLAN.md, OPERATIONS.md, project.json and the immutable queue/decoder before implementation. Read the R19 contract's `evidence_sha256` directly and hashed all 149 current files before and after the code/test changes: **149/149 matched their frozen hashes at both checks; zero changed between checks.** No frozen report or evidence was regenerated.

Immutable dependency SHA256, unchanged before/after:

- `maintenance/hakios-lithium/peripherals/touch-queue.h`: `2c172b21c00c20bb5a28c5ea23917c4a1bf05418f064ed69f81e14c5d22b7f0c`
- `maintenance/hakios-lithium/peripherals/touch-frame.h`: `2c336fb8e5283432ec2e7d043b1a9fd6c328ea3a3b8e2ce45fa43c7f0ce85a93`

## API and protocol

Line references are one-based in the new source files.

| Location | Behavior |
| --- | --- |
| `touch-delivery.h:33` | Batch envelope contains a nonzero acquisition sequence, monotonic nanosecond timestamp, and the existing bounded queue batch. |
| `touch-delivery.h:40` | Explicit atomic adapter ABI, synchronous whole-batch submit callback and optional downstream recovery fence. |
| `touch-delivery.h:74` | Initialize a zeroed object once; missing callback or unsupported ABI fails closed as UNAVAILABLE. Geometry and adapter are copied and fixed for that stream. |
| `touch-delivery.h:104` | Prepare runs immutable `lithium_touch_queue_frame` on a copy of committed state. Success retains the candidate and complete batch, reserving sequence/time; it does not commit contacts, slots or stroke allocator. |
| `touch-delivery.h:135` | Submit commits the candidate only after ACCEPT_ALL. REJECT_ALL retains the exact pending batch/candidate for retry; subsequent submit after success returns NO_PENDING without calling the sink. |
| `touch-delivery.h:162` | Uncertain delivery recovery requires an explicit adapter fence; successful recovery clears local active contacts/slots, burns candidate identities, and preserves sequence/time high-water marks. |

There is one pending frame, at most 20 events, fixed-size state and no heap allocation or internal retry/wait loop. Every entry point rejects callback recursion with BUSY. All calls must still run on one serialized producer executor; the guard is not a mutex or cross-thread race protection. Callback execution must itself be bounded and synchronous; this header cannot interrupt a callback that hangs.

The caller supplies acquisition sequence and timestamp from one monotonic source. Both must strictly increase after each successful prepare; zero, duplicate, decreasing and wrapped values fail. Invalid preparation changes nothing and consumes no stamp. The header checks numeric ordering, not the truth of a hardware clock. Numeric sequence gaps are allowed and do not independently detect acquisition loss: the producer must explicitly report a known dropped/untrustworthy frame.

After rejection, call submit again without preparing/redecoding/restamping; batch bytes, finger slots and candidate stroke identities remain unchanged. A second prepare returns PENDING, including attempts to supersede it with loss. There is deliberately no cancel/coalesce/timeout escape that silently discards a pending batch. Stop acquisition or remember loss, resolve the pending batch, then prepare an explicit newer loss. Permanent sink rejection keeps backpressure active.

Malformed input is rejected without inventing releases. Report actual stream loss separately using `frame=NULL, length=0, lost=true`. Releases are prepared against committed contacts, then committed only after acceptance; retry does not release twice, and repeated accepted losses contain zero further UP records. UP and missing IDs follow the immutable decoder. Stationary and empty frames still pass through the submit callback for their metadata receipt, even with zero event records.

All geometry remains solely in existing `lithium_touch_queue_map`, reached through immutable `touch-queue.h`. No second geometry implementation was added. Rotations, viewport and physical/render dimensions are inherited; no BGR24 stride reinterpretation.

## Precise sink contract and limitations

The callback must implement real whole-batch atomic reservation/publication, or an equivalent atomic acknowledgment protocol:

1. **ACCEPT_ALL** means every event, its ordering and its sequence/time envelope have been accepted together. Copy borrowed data before returning; retain no pointer to the helper's batch. A previously accepted exact identity may only be answered from a sink's idempotent receipt, never republished.
2. **REJECT_ALL** means exactly zero records or metadata became visible and no delayed publication can follow that rejection. Only this outcome permits exact retry. Saturation cannot publish a release prefix and then reject the remaining records.
3. **UNCERTAIN**, including partial publication, timeout with unknown outcome, and every unrecognized callback return, poisons the object. The helper leaves committed state unchanged, keeps the failed batch for inspection, and blocks further prepare or submit. It does not roll back externally visible records or retry an ambiguous batch.
4. Resync is an explicit, idempotent downstream fence through the failed sequence. ACCEPT_ALL from that callback certifies no older queued/in-flight records can appear later and that downstream HID contacts have been released/reset and acknowledged. Rejected/uncertain resync remains poisoned; absent resync support fails closed. New input after recovery needs newer sequence/time and fresh stroke identities.

**Queue admission success is not IOHID end-consumer acknowledgment.** No healthy-stream IOHID consumer acknowledgment API or physical queue/IOHID connection is supplied. There is **no end-to-end exactly-once claim without sink idempotence and receipts**, and no persistence guarantee across a crash, stream recreation or reinitialization. The future adapter must also namespace stream lifetimes if it retains receipts across restarts. Atomic ABI declaration is an adapter promise, not runtime proof of atomicity.

The current upstream posting queue has not acquired transaction support, reservation, consumer acknowledgment or partial rollback through this helper. A loop around its existing per-event post operation is insufficient and must not claim this ABI. Actual queue reservation/commit or an appropriate acknowledgment facility, event metadata transport, consumer recovery handshake, locking and hardware validation remain future work. Reinitializing the object alone cannot repair partial delivery. No physical touch, IOHID delivery, display interaction, GPU result or device state is claimed.

## Commands and results

Host compiler: WSL Ubuntu clang 18.1.3. From `F:/IOS-MIX1`, the following PowerShell pipeline compiled and ran the final test source; both subprocesses exited 0:

```powershell
@'
import os, pathlib, subprocess, tempfile
source = '/mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals-next/touch-delivery-test.c'
with tempfile.TemporaryDirectory(prefix='touch-delivery-') as work:
    binary = str(pathlib.Path(work) / 'touch-delivery-test')
    command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', source, '-o', binary]
    print('COMMAND:', ' '.join(command), flush=True)
    subprocess.run(command, check=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    subprocess.run([binary], env=env, check=True)
'@ | wsl.exe --exec python3 -
```

Final actual binary path was `/tmp/touch-delivery-emumnvq1/touch-delivery-test`; it was removed with its temporary directory. Compile emitted no warnings with warnings-as-errors. ASan/UBSan emitted no findings. Final output:

```text
PASS down/move/up, private state, timestamps, identity reuse prevention
PASS 10 contacts/20 events, saturation, exact retry, no partial prefix
PASS explicit loss, rejected release retry, repeated loss, missing-ID release
PASS duplicate/decreasing sequence/time, empty/stationary, no wrap
PASS malformed length/null/loss/IDs/events/coordinates, no mutation
PASS immutable geometry: four rotations, midpoint, viewport
PASS callback recursion, absent atomic adapter, invalid API inputs
PASS partial/unknown poison, no replay, failed/successful recovery fence
PASS 8 synthetic touch delivery groups; no physical queue or IOHID connected
```

Focused test locations: down/move/up `touch-delivery-test.c:92`; 10-contact replacement plus three N-1-capacity rejections and byte-exact retry `:116`; loss/release `:151`; sequence/time, rejected zero-event metadata and allocator exhaustion `:173`; malformed/no-mutation `:209`; rotations/viewport via existing geometry `:234`; reentrancy/unsupported adapter `:260`; partial/unknown poison and resync `:287`; runner `:322`.

The queue mock is intentionally synthetic: it reserves capacity for the whole batch before publication, and its deliberate partial mode demonstrates poisoning. Its passing assertions are not evidence that the upstream queue or actual IOHID stack implements these guarantees.

Frozen-evidence verification used only read-only PowerShell `Get-Content .../phase4-r19/contract.json | ConvertFrom-Json` and `Get-FileHash -Algorithm SHA256 -LiteralPath <each evidence_sha256 path>`; the 149 expected/current SHA256 pairs were compared both to the contract and to the in-memory initial results. Result: count 149, mismatches `[]`, changed paths `[]`.
