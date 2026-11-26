#pragma once

#include "hakios-metal/pm4_a5xx.hpp"

namespace hakios::metal::a5xx {

// Adapter inputs: actual GPU virtual address/visible length, NOT CPU pointers.
// The integrating CommandBuffer must keep the BOs alive and register their uses.
struct BufferView {
    uint64_t gpuAddress = 0;
    uint64_t length = 0;
    bool readable = true;
    bool writable = true;
};

// Standalone packet encoder: does not allocate/submit a BO or access a device.
// Precondition: source data is CP-visible/coherent; render/texture cache flushes,
// CPU mapping coherency and external queue ordering belong to the caller.
// Transfer boundaries contain CP_WAIT_MEM_WRITES + CP_WAIT_FOR_ME. They do not
// establish shader-cache coherency or constitute hardware completion evidence.
// One encoder/stream requires external synchronization when shared by callers.
class TransferEncoder {
public:
    explicit TransferEncoder(pm4::Stream& stream) : stream_(stream) {}

    // DWORD-aligned source/destination/range, non-overlapping GPU address ranges.
    // Nonzero overlap (including identical ranges) and byte copies are refused.
    // Emits 6 stream DWORDs per copied DWORD, as Mesa's A5xx implementation does.
    pm4::Result copy(const BufferView& source, uint64_t sourceOffset,
                     const BufferView& destination, uint64_t destinationOffset,
                     uint64_t length);
    // Repeats value as four bytes per CP_MEM_WRITE data word. No A6xx memcpy
    // doubling, shader emulation, read/modify/write, or guessed 2D registers.
    pm4::Result fill(const BufferView& destination, uint64_t offset,
                     uint64_t length, uint8_t value);

    // A refusal leaves stream unchanged. Zero length checks view/offset bounds,
    // then emits nothing (no alignment/permission requirement for an empty range).
private:
    pm4::Stream& stream_;
};

}  // namespace hakios::metal::a5xx
