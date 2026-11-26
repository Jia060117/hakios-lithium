# Lithium peripheral host components

These are host-tested protocol/policy helpers, not operational phone drivers.
The only new production integration is the default-off admission diagnostic in
`ports/hakios-lithium/peripherals/kernel.inc`. It refuses hardware access because
the board backends are unverified. No helper below is called by that diagnostic.

| Files | Scope | Missing production connection |
|---|---|---|
| `boot-options.h` | Strict, default-off option parsing | Actual parser copy is byte-checked in the device layer. |
| `touch-frame.h` | Stock FocalTech frame parsing and contact lifecycle | Runtime identity and actual frame/latch evidence. |
| `touch-i2c.h` | Bounded callback contract, identity and split report reads | A combined write-register/repeated-START/read controller backend. |
| `qup-i2c.h` | QUPv2 tags/state/prerequisite planning | Controller transfers, board pin/clock/power binding. |
| `qup-transfer.h` | Source-backed, bounded FIFO single-message candidate | Repeated START, board handoff, real MMIO callbacks; GPL-2.0-only attribution is retained. |
| `touch-irq.h` | Serialized IRQ/coalescing/storm/poll state policy | Kernel scheduling, GPIO/TLMM, physical ISR and locks. |
| `touch-digitizer.h` | Standalone digitizer fields and geometry checks | Consumer/frame delivery policy and physical input. |
| `touch-queue.h` | HakiOS queue-record adapter and one geometry function | Atomic frame/batch delivery, acknowledgement, timestamps and overflow recovery. |
| `irq-route.h` | Owned-line GICv3 routing/check policy | Production bounded GIC callbacks and actual ARE/route/delivery evidence. |
| `gpu-probe.h` | Gated resource/deadline/cleanup callback orchestration | Qualified A530 power, clock, SMMU, secure-state and ID-read backend. |
| `gpu-firmware.h` | Synthetic PM4/PFP/FW2 format and bounds validation | Matched local firmware provenance and the real load path. |
| `gpu-submit.h` | CPU-array ring/fence/watchdog model | Hardware ring, DMA mappings, interrupts and actual reset/quiescence. |
| `gpu-iova.h` | Owned map/unmap/TLB transaction model | Real SMMUv2 page tables, cache/TLB operations and DMA lifetime. |

Never submit the mock fence packet to hardware: its fixed address has no backing
mapping. Never turn a fixture's prerequisite flags into asserted platform facts.
A deadline around a callback cannot recover a synchronous MMIO bus hang.

## Host tests

From Windows, one command builds and runs all current suites with clang, ASan and
UBSan, without a device connection. Outputs are temporary by default:

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py
```

To preserve manifests, use `--build-dir` with a **new** directory under ignored
artifacts. Never commit the generated ELF executables. To select one suite, pass
its complete filename stem, such as `--suite touch-irq-test`.

## Options in the admission hook

All features default off. `touch=1` requests touch; `touch_log=1` and
`touch_poll=1` only take effect with it. `gpu_stage=0` alone remains off;
`gpu_probe=1 gpu_stage=0` requests stage 0. Nonzero `gpu_stage=1..4` also requests
GPU admission. `gpu_wd=1` records a requested watchdog option, not a working
hardware watchdog. Requested features currently print blocked/unverified logs.
No new image is produced by the host test or evidence-check script.

The final evidence and conditional hardware handoff are in
`reports/overnight-20261118/REPORT.md`.
