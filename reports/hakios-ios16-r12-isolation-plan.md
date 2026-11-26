# R12: isolate raw storage before changing performance policy

The user requested raw storage numbers first, one change at a time, the actual
boot-args line and min/average/max per test. R11 contains a successful 20.549070s
rename nested in a 22.923753s container replacement; these are wall times and do
not identify a UFS defect. R12 adds measurement only.

The full R11 state is preserved at `archive/ios16-r11-final-before-r12-20261117`
(`2445cf1`). R11 binaries/logs remain local and the consumed image is not retried.

## Fixed conditions

- Complete pinned HakiOS 63fc2b46 / XNU8796.121.2, D22AP/20F66 arm64.
- UFS, 2880MiB, `bootcpu=2 cpus=3`; the fourth physical core remains the loader worker.
- No CPU/cache/clock, queue/poll, HFS journaling, mount or flush policy change.
- No launchd or filesystem mount in this isolated prelaunch diagnostic.
- No userdata reinstallation, GPT change or Apple binary in Git/shareable artifacts.

## Scratch ownership and integrity

The installed image ends at userdata-relative block 5242880 (4096-byte blocks).
The test uses 100 blocks starting at 5243136, after a 1MiB gap. It is outside the
installed System/cryptex/Data extents and inside the verified userdata partition.
Firmware retains the real GPT/CRC/geometry checks and accepts only the original
Data interval plus this exact scratch interval, never the gap or a crossing request.
Loader requires `ufs_rawbench=1` before accepting scratch. Ordinary kernel Data
and raw-device writes retain their original boundary; only the prelaunch test uses
the separate scratch helper. A live HFS header check rejects a Data extent that
would extend beyond the installed image.

All 400KiB original bytes are saved before any test write. Distinct deterministic
patterns are written and compared on readback. Every possibly touched block,
including a failed partial write, is restored, flushed and compared byte-for-byte.
Restoration failures are reported, never treated as a pass. Power loss may leave
scratch patterns, but this range contains no installed filesystem. The experiment
has a 120-second measurement budget; restoration follows even if that expires.
The existing kernel timer returns after 600 seconds and drains USB normally.

## Measurement and decision point

100 samples each: normal WRITE_FAST completion, explicit FLUSH after that write,
their combined duration, and raw READ with pattern verification. Backup/restore
operations have their own statistics. Flush-before-test and flush-after-restore
are correctness operations and their error status is separate.

Each test prints the established `LITHIUM ... key=value` format, with successful
sample count, error count, minimum/average/maximum in microseconds. Failed samples
are excluded from latency statistics and make the run incomplete. `scope=wall`
times the synchronous bridge call; `scope=firmware` uses the existing loader service
counter (including its cache maintenance and firmware operation, not media-only).
Both use the reported CNTFRQ frequency. No logging occurs inside the sample loops.
Kernel-received boot arguments are emitted in full to the retained USB stream.

Report raw lines first. An average above approximately 2ms is a branch signal,
not proof of a hardware defect. Compare write-only with write+flush and firmware
with wall time before choosing worker/transport or filesystem investigation.
Fast raw results also do not by themselves prove HFS is the cause: CPU and higher
layer serialized work remain candidates. No conditional optimization is applied
before the user sees this result.

Host validation exercises actual benchmark code under ASan/UBSan, including
602 failed-request positions, partial writes, corruption, timeout, write denial,
statistics and actual loader/firmware boundaries. Full HakiOS, supplied-identity
loader/Mu, normal userspace build and existing compiled/package gates remain required.
Host checks do not establish phone latency, persistence or a working desktop.
