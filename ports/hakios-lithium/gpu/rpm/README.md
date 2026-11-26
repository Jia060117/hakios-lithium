# MSM8996 RPM GLINK: host preparation

This directory supplies an original callback-only C11 transport model and an
exact active-set MX corner request. It is unintegrated. The only supplied I/O
backend uses ordinary host byte arrays in `rpm-host-test.c`. There is no MMIO
mapper, volatile access, kernel entry, boot-argument parser, IRQ handler,
launcher, device utility, firmware or vendor binary.

Main retains the phone lifecycle. Latest access observations supplied by main:
GFX APID52/PPID317 has writeEE0, channelconfig0, statusDONE1; MX
APID127/PPID117 belongs to EE4. These observations are prerequisites reported
by main, not hardware measurements made by this package. Direct MX SPMI writes
are not implemented.

## Files and integration contract

* `rpm-host.h`: public types, concrete binding, bounded model interface,
  explicit ACK error bytes and bus deferred status.
* `rpm-host.c`: TOC/FIFO validation, version/open negotiation, atomic TX,
  exact request ACK and fixed MX encoder.
* `rpm-host-test.c`: deterministic byte-array peer and six scenario groups.
* `run-host-tests.py`: ASan/UBSan, optimized and freestanding AArch64 compile;
  source/evidence hash verification; JSON result. Temporary host executables
  are removed after the run.
* `evidence/gpu-metal-20261123/rpm/source-manifest.json`: primary-source
  commit, URLs, byte counts, SHA-256 and line anchors. Existing stock DT and
  upstream reference patches are hashed read-only.
* `evidence/gpu-metal-20261123/rpm/test-results.json`: exact commands,
  compiler identity, output, source hashes and evidence hashes.

The core has no allocation, libc include, global mutable state or arbitrary
request API. It is single caller. Tests own the callbacks, their time source
and all fake state. A callback must return promptly: a synchronous core cannot
preempt a callback that never returns. Every I/O/order/wait callback consumes
the operation's call budget and has a deadline check before and after it.
Separate finite state-machine steps bound peers that keep delivering traffic.
Bad indices, ownership changes, unknown/malformed packets, callback errors,
deadline expiry and exhausted budgets permanently fault the session. A faulted
session performs no further callbacks. There is no automatic request replay.
After publication, a lost ACK is an uncertain remote outcome, not permission
to send the vote again.

Host model call sequence:

```c
struct rpm_host s;
struct rpm_host_ack ack;
struct rpm_host_limits lim = {10000, 128, 10000};
rpm_host_init(&s, &fake_io);          /* validates callbacks; performs no I/O */
rpm_host_discover(&s, lim);           /* read-only; check every return code */
rpm_host_open(&s, lim, true);         /* asserts exclusive FRESH fake edge */
rpm_host_mx_corner4(&s, lim, &ack);    /* OK, NACK, or a latched error */
```

The example only describes host use. The `true` argument is a caller assertion,
not evidence that live ownership exists. No runnable hardware integration is
provided. Discovery never resets indices. Open requires all four initial
indices to be zero and refuses a live edge. The pinned downstream driver
resets its local indices during probe; this package deliberately supplies no
takeover/reset operation. A runtime adapter remains main's separate work.

## Concrete transport binding and bounds

The actual stock DT, lines 10035–10041, names
`qcom,glink-rpm-native-xprt`, message RAM PA `0x68000`, length `0x6000`,
doorbell PA `0x09820010`, mask `1`, and GIC SPI number `168`, flags `1`.
This is the DT SPI specifier, not an OS IRQ number. Lines 5921–5924 name the RPM
edge and `rpm_requests` channel. Those exact values are `rpm_host_msm8996`.

The pinned MiCode native transport puts the 256-byte TOC at the end of message
RAM. Discovery takes two identical snapshots, requires `grt0`/at most20
entries, and locates exactly one `ap2r` and `r2ap`. Each tuple contains the
offset of an8-byte `{read_index, write_index}` descriptor and a separate DATA
length. The descriptor must be included in range/overlap checks; it is not
subtracted from the tuple length. Overflow-safe bounds exclude the TOC.
Selected ranges cannot overlap. Word addresses and all indices are checked
before callback use. Published ring indices must be8-byte aligned and smaller
than the data length. Producer/consumer indices owned by the local model
cannot be changed behind its back.

TX leaves the pinned native driver's16-byte reserve: full/empty separation8
plus blocked-command reserve8. It writes a complete padded packet before
publishing the producer index, then orders and rings the exact doorbell.
Only local TX data/head and local RX tail are writable through the core.
Only message RAM is readable. Fake callbacks assert these address limits.
No guessed FIFO offsets or lengths are compiled into the transport.

Negotiation supports version1 with feature intersection0, bidirectional
`OPEN "rpm_requests"` and the matching local/remote channel IDs. A peer ACK
offering unsupported features causes a bounded version1/features0
renegotiation. Unknown versions and commands fail closed. `READ_NOTIF` kicks
after publishing the consumer index. Known8-byte sideband signals cannot
be mistaken for an RPM ACK. Close/restart faults the session.

TX_DATA uses native16-byte `{cmd+lcid, intent0, chunk_len, remaining0}` and
one atomic payload. The pinned MiCode intentless path requires a complete
packet or failure; it disallows continued transmissions. This bounded model
therefore rejects TX_DATA_CONT, nonzero intent/remaining and oversized
responses instead of truncating/reassembling them. RX waits for the complete
aligned packet before advancing its consumer index. Maximum payload256
bytes and error text128 bytes are explicit capacities.

## MX selector4: exact payload

Stock DT lines 5974–6008 bind `pm8994_s2_corner` and its AO twin to
`smpa`, resource ID2, with `qcom,use-voltage-corner`.
Pinned `rpm-smd-regulator.c` lines 808–824 subtract `voltage_offset`;
lines 1484–1486 set it to `RPM_REGULATOR_CORNER_NONE`.
Pinned `include/linux/regulator/rpm-smd-regulator.h` line32 defines that
constant as1. Therefore the requested stock selector4 encodes as `corn=3`.
The field is not microvolts. `corn=4` would represent selector5.

The only request encoder emits40 bytes (ten little-endian words):

| Word | Value | Meaning |
|---|---|---|
| 0 | `0x00716572` | service `req\0` |
| 1 | `32` | bytes after the8-byte service header |
| 2 | caller's nonzero message ID | exact ACK correlation |
| 3 | `0` | active set |
| 4 | `0x61706d73` | resource `smpa` |
| 5 | `2` | resource ID |
| 6 | `12` | KVP byte length |
| 7 | `0x6e726f63` | key `corn` |
| 8 | `4` | value byte length |
| 9 | `3` | selector4 minus NONE1 |

The complete GLINK request frame is56 bytes. It does not send `uv`, `swen`,
sleep-set votes, floor votes, global scaling, unrelated clock handoffs or max
votes. This corner-only active request does not implement Linux's shared
regulator-client aggregation or enable/disable lifecycle. Prior MX enabled
observations do not establish that a future RPM corner request will settle.

Pinned Linux `drivers/soc/qcom/smd-rpm.c` lines80–83 additionally confirms
the `req`, `msg#` and `err` wire signatures independently of the local
Sailfish reference patch. ACK parsing requires the exact service/body length, leading
`msg#` KVP of length4, nonzero message ID and, optionally, exactly one bounded
`err` KVP. Unknown/duplicate/truncated records are errors. An absent `err`
means protocol success; its presence means NACK, preserving its raw error
bytes/length (which may include NUL). There is no invented numeric remote
errno and no assumption that the bytes are printable text. A well-formed
different message ID increments an unmatched counter and cannot complete the
pending request. The RX channel must also match the opened remote channel.
An RPM ACK is not proof of a measured/settled voltage, GPU power or bus clocks.

## Stock GPU bus index3: preserved, transmission deferred

The immutable stock DT GPU table at line11335 contains the fourth vector
(zero-based index3):

```
master=26 slave=512 AB=0 KB/s IB=1600000 KB/s
```

The 133MHz stock power level selects bus index3 at lines 11464–11473.
`rpm_host_stock_bus3` preserves these values, plus decimal1000 conversions
to AB0/IB1600000000 B/s. Pinned `msm_bus_of.c` lines127–133 calls `KBTOB`;
`msm_bus_core.h` line44 defines it as multiplication by1000ULL.
These fields are a plan, not a wire vote.

DT master26 (`mas-oxili`, lines3147–3158) is AP-owned and has two aggregate
ports, width8, four connections, and RPM master ID6. Slave512 (`slv-ebi`,
lines3595–3603) has RPM slave ID0. Neither DT26 nor512 may be used directly
as a guessed RPM resource ID.

Pinned `msm_bus_rpm_smd.c` shows64-bit per-node bandwidth KVPs. The adhoc
bus path computes clocks from aggregated AB/IB, utilization and width, and
the stock master is AP-owned. The exact isolated-client route, aggregation,
clock/QoS writes and prerequisite ownership have not been derived/proven
here. Sending the IB number as a master/slave bandwidth vote would therefore
be unsupported. `rpm_host_encode_bus3` returns `RPM_HOST_BUS_DEFERRED`, sets
length0 and leaves the caller's output bytes unchanged. No bus request API
can publish a fabricated vote.

## Validation and missing evidence

Run from Windows:

```powershell
wsl --exec python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/rpm/run-host-tests.py
```

Clang18.1.3 with C11/pedantic/all warnings as errors:
ASan+UBSan and optimized builds pass the same six scenario groups.
The fault campaign covers every callback position of a complete MX exchange
with both immediate I/O failure and late callback return. The result JSON
records assertion counts and fake read/write/order/wait/doorbell counts;
these are host model counts, never hardware access counts. Freestanding
AArch64 compilation is a compile check only, not an XNU link or hardware test.

Remaining evidence for main:

1. Live TOC contents and exclusive/fresh AP↔RPM GLINK ownership; how to handle
   bootloader-established edge state without erasing another owner's traffic.
2. Separate XNU mapping/allocation lifetime, memory ordering and poll/IRQ
   integration; no adapter has been added to shared code.
3. Real version/open/request ACK or NACK; analog settling and aggregate MX
   rail behavior. The API cannot produce a hardware-valid supply receipt.
4. Exact isolated GPU bus route and per-node clock/QoS/RPM derivation from the
   stock vector, with ownership of shared resources.

All work is confined to the requested RPM source and evidence directories.
No parser/shared files, upstream checkouts, live logs or device state are
modified. No device operation or runtime hardware write was performed.
