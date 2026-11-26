# MSM8996 GPU vector3 bus plan — host only

Implemented `bus-plan.c/.h`, using the existing `rpm_host_decode_ack()` as a
read-only dependency. `rpm-host.c` and its intentionally deferred
`rpm_host_encode_bus3()` entry point remain unchanged. This package provides
an explicit separate bus API; it has no transport, mapping, register access,
device command, kernel hook or power operation. Hardware is unverified.

Run from the repository root in WSL:

```sh
python3 ports/hakios-lithium/gpu/rpm/bus-run-tests.py
evidence/gpu-metal-20261125/bus/build/bus-demo
```

From PowerShell:

```powershell
wsl -e python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/rpm/bus-run-tests.py
```

All build products, source anchors, captured test commands and the synthetic
demo output are under `evidence/gpu-metal-20261125/bus/`.

## Exact downstream result

Pinned MiCode commit: `1492c8e4373b7428bb0accc4f4881e97b36508b9`.
The supplied stock DT binds the lowest active **133 MHz GPU power level's
default vector3** to `26 -> 512, AB=0, IB=1600000 KB/s`. Vector1/2 also exist;
vector3 is the requested power-level default, not the smallest nonzero table
entry. Decimal `KBTOB` makes IB **1,600,000,000 B/s**. This is a static request,
not observed memory traffic or measured clock speed.

| Node | Ownership / endpoint | Width / aggregation ports | GPU contribution | Derived clock request |
| --- | --- | --- | --- | --- |
| 26 `mas-oxili` | AP-owned; master RPM6 metadata does not cause a remote BW send | 8 bytes / 2 | AB0, IB1.6e9 | 200,000,000 Hz |
| 512 `slv-ebi` | remote `bslv` ID0 | 8 bytes / 2 | AB0, IB1.6e9 | 200,000,000 Hz |
| 0 `fab-bimc` | maximum of attached node clock requests | util154%, vrail100% | maximum of these two nodes, preserving other votes | 200,000,000 Hz GPU floor |

The legacy formula is
`max((sum_AB * 154 / 100) / 2, max_IB * 100 / 100) / 8`.
Aggregation ports divide **AB only**, and utilization154 does not scale IB.
Both node clock values feed the fabric maximum. Neither route node has its
own DT clock handle. Fabric `bus_clk` is `bimc_msmbus_clk`, while `bus_a_clk`
is `bimc_msmbus_a_clk`; these are voters on the normal/active-only BIMC
RPM clock pair.

Exact remote tuples:

| Purpose | Set / resource / ID / key | Value and width | Planner behavior |
| --- | --- | --- | --- |
| G1 active BIMC floor | active0 / `clk2` **0x326b6c63** / 0 / `KHz` **0x007a484b** | ceil(200e6 / 1000) = **200000**, uint32 | emit only if exact original AP active vote is lower |
| Slave AB | active0 / `bslv` **0x766c7362** / 0 / `bw` **0x00007762** | summed **AB**, uint64 B/s | zero GPU delta; leave existing slave vote intact |
| Master AB | metadata `bmas` **0x73616d62** / 6 / `bw` | AP-owned node uses local QoS | no master request |

The 40-byte clock request is one `req` service header plus one 4-byte KVP.
The standalone slave codec produces 44 bytes with an 8-byte KVP; it is
available for exact AB request/restore material, but vector3 emits no slave
action. Putting IB1.6e9 in `bmas6/bw` or `bslv0/bw` would be incorrect.

This API intentionally implements an **active-only G1 acquisition**. It does
not reproduce stock's complete suspend policy. For a normal dual-context
msmbus client, `update_request()` copies IB/AB into both contexts; both route
node requests are then 200 MHz in both contexts, normal `bus_clk` carries the
sleep floor and the RPM peer pair aggregates active votes. The grp3d DT
parser defaults to dual context when `active-only` is absent. Real KGSL can
delegate voting to `gpubw-dev`, so the DT vector alone does not prove the
live client's context or dynamically selected AB. This planner leaves
sleep KVPs untouched.

## QoS and local bus clocks

Stock master26 uses BIMC qport1, **bypass** mode. Source
`msm_bus_bimc_qos_init()` calls `set_bke_en(..., false)`. The exact address is
`bimc_base 0x400000 + 0x8000 + qport1 * 0x4000 + 0x300 = 0x0040c300`.
Only bit0 is cleared, with every other bit preserved. No health/priority,
grant-period, threshold, limiter or regulator programming is called for
bypass. BIMC has no fabric `bus_qos_clk` or `node-qos-clks` in this stock DT.
The source's generic NOC offset properties must not be added a second time
to the BIMC address.

`bus-plan` returns a masked action containing exact expected-before and
desired-after words. A future adapter must compare before writing, enforce
ordering and read back before reporting completion. Applying the clock
floor precedes the optional QoS change; restore reverses completed actions.
An already-clear BKE bit produces no QoS write.

The stock GPU also has bus-facing clocks outside the msmbus vector:

| GPU clock name | Stock clock | Register / dependency |
| --- | --- | --- |
| `mem_clk` | `gcc_bimc_gfx_clk` | GCC base0x300000 + CBCR0x46018 = **0x00346018**; shared sibling, enable-bit checking, stock `always_on` |
| `mem_iface_clk` | `gcc_mmss_bimc_gfx_clk` | CBCR **0x00309010**; shared sibling, enable-bit checking |
| `alt_mem_iface_clk` | `mmss_mmagic_ahb_clk` | MMSS base0x8c0000 + CBCR0x5024 = **0x008c5024**; parent `ahb_clk_src` |

KGSL prepares/enables its provided group clocks on power-up. These three
handles are therefore integration prerequisites, not three additional
RPM bandwidth nodes. `ahb_clk_src` has 19.2/40/80 MHz stock choices and
VDD_DIG dependencies; the vector does not select a unique parent operating
point. There are no local branch/RCG/voltage writes in this implementation.
Source anchors include these exact handles and parent tables so main can
integrate them with its clock/rail work.

## Original state, correlation and boundaries

`rpm_bus_begin()` requires a once-zeroed session and an explicit
`rpm_bus_original`:

* A known-present **original AP active `clk2/id0/KHz` request**, from an
  authoritative sender shadow/handoff. It must include every enabled AP
  voter and the normal/active-only peer. A register's actual frequency,
  RPM aggregate output, ACK or guessed boot default cannot establish this.
* An exact stable original BKE word and resource exclusivity covering
  both clock peers/voters and the QoS word through restoration.
* The transport's previous message ID, with exclusive allocation of the
  next two IDs if a clock change is necessary. Incoming ACK bytes must come
  from the same bound RPM request channel.

Missing original evidence produces `EORIGINAL` before an action is emitted.
Absent/untracked KVP invalidation is unsupported rather than treated as
zero. The floor is `max(original, 200000)`; a higher vote stays intact.
Only a changed clock KVP and changed BKE bit are emitted: at most two apply
actions and two reverse restore actions, with distinct IDs for apply/restore.
Restoration writes the supplied original rate, including an explicitly
known original zero, and the exact original QoS word under expected-value
guards.

One action may be outstanding. `next()` never republishes it. ACKs must
match its ID; up to eight unrelated ACKs are tolerated. Malformed ACK,
matching NACK, time reversal or deadline expiry latches uncertainty, retains
the pending record, and disallows retries/reinitialization. A matching NACK
does not certify the original state. `start_restore()` can roll back an
ACK-completed prefix before the next action is issued. It refuses pending
work; uncertain outcomes cannot be reported restored.

The whole apply/hold/restore interval is bounded to at most 1,000,000 us.
Each pure function performs bounded work. A future adapter must separately
bound its own transport and MMIO. `rpm_bus_restored()` is a software result
from supplied completions; the demo deliberately labels its completions
synthetic.

## Exact remaining integration evidence

No static bus tuple or QoS field is deferred. The outstanding evidence is
runtime-specific:

1. Original AP `clk2/0/KHz` request and enabled peer/voter state. The exact
   needed source is the pre-XNU firmware/RPM sender's request shadow or a
   handoff receipt. The downstream RPM clock driver has no getter for the
   original request; its `get_rate()` uses cached software state.
2. Current `0x0040c300` word and the three local bus clock/parent states.
   These require main's separately authorized bounded reads and ownership
   receipts. No such phone reads are performed here.
3. Whether current RPM clock scaling is already usable. The stock GCC
   probe globally votes `INT_MAX` and calls scaling enable; those probe
   actions are excluded from this GPU plan. ACK, achieved BIMC frequency,
   scaling behavior and complete GPU power-up remain hardware-unverified.

The exact source ranges, content hashes, derived values and this boundary
are also in `source-anchors.json`, `derivation.json` and `handoff.json`.
