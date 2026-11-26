# A530 A5xx fence-packet encoder

Host-only isolated candidate; no production integration. Added only:

- `maintenance/hakios-lithium/peripherals-next/a5xx-fence-packet.h`
- `maintenance/hakios-lithium/peripherals-next/a5xx-fence-packet-test.c`
- this report

## Encoding and source basis

Linux v6.12 commit `adc218676eef25575469234709c2d87185ca223a` emits this exact five-dword packet in `drivers/gpu/drm/msm/adreno/a5xx_gpu.c:219-227`: a type-7 `CP_EVENT_WRITE` with four payload words; `CACHE_FLUSH_TS | IRQ`; low and high words of `rbmemptr(ring, fence)`; then the 32-bit `submit->seqno`. The source explains that the cache-flush timestamp event writes the timestamp to memory before triggering its interrupt (`a5xx_gpu.c:219-227`).

Type-7 header parity is independently checked against Linux `adreno_gpu.h:637-664`: `PM4_PARITY` returns the lookup-table parity, and `OUT_PKT7` places count parity at bit 15 and opcode parity at bit 23. The test computes even parity by direct bit counting, independently of the encoder's nibble-fold implementation, and compares the full packet against a separately assembled golden vector. For opcode `0x46` and payload count 4, the independently derived header is `0x70460004`.

The pinned PM4 XML identifies `CP_EVENT_WRITE` as opcode `0x46` for A2XX–A6XX (`registers/adreno/adreno_pm4.xml:301-303`), `CACHE_FLUSH_TS` as event value 4 and describes its cache flush/timestamp behavior (`:14-17`), and defines event/IRQ plus two 32-bit address words (`:1614-1630`). XML leaves payload word 3 undocumented (`:1631-1633`); the encoder follows the concrete Linux A5xx emitter, which writes the sequence there.

## Input constraints and transactional behavior

- The fence destination is a mapped GPU IOVA supplied by the caller, not a baked-in test address. Linux derives it from `memptrs_iova + offsetof(..., member)` (`msm_ringbuffer.h:13-14`); the fence slot is a `volatile uint32_t` (`:30-36`), and Linux allocates the memory-pointer object in the GPU address space (`msm_gpu.c:948-951`). The A5xx path uses the generic Adreno address-space constructor (`a5xx_gpu.c:1714`), whose lower bound is at least 16 MiB (`adreno_gpu.c:219-228`). The helper applies that Linux allocation minimum and requires four-byte alignment.
- The packet has two full 32-bit address payload fields (`adreno_pm4.xml:1625-1630`), so serialization preserves all 64 input bits and does not add an invented 40-bit or 48-bit A530 ceiling. The active IOMMU aperture and mapping ownership remain the caller's responsibility; the host test's maximum-address vector checks wire serialization only.
- A 32-bit sequence value is emitted unchanged (`a5xx_gpu.c:227`). Linux initializes its fence counter near rollover and increments it (`msm_fence.c:60-66,200-201`), so zero is not rejected by the encoder.
- `OUT_PKT7` reserves `cnt + 1` ring words for a packet (`adreno_gpu.h:659-664`); `cnt=4` means five dwords including the header. The encoder checks this capacity before publishing and stages all words locally, leaving the destination unchanged on invalid input or insufficient capacity.

## Validation

The standalone test covers the independent golden vector and parity, low/high IOVA words, zero and maximum sequence values, the 16 MiB Linux allocator boundary, each misalignment, capacities 0 through 4, null output, and unchanged sentinel output on every failure. It uses an ASan/UBSan host build. Command, compiler version, exit status and test output are recorded below after execution.

This only verifies CPU-side packet serialization. It does not initialize the CP, populate or publish a hardware ring, establish cache visibility, prove IOVA ownership/aperture validity, drive an interrupt, or demonstrate GPU execution. No phone, ADB/Fastboot, QEMU, firmware download, staging or commit was performed.
