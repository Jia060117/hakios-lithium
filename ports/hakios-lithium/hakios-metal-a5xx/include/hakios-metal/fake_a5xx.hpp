// Strict synchronous HOST MODEL of the transfer subset, not a GPU transport.
// Interprets CP_MEM_WRITE and the control=0/single-source CP_MEM_TO_MEM form.
// Executes no shader, 2D operation, indirect buffer, or register write.
// Waits are syntax-checked only: no hardware cache/timing/completion claims.
#pragma once

#include <mutex>
#include <vector>
#include "hakios-metal/pm4_a5xx.hpp"

namespace hakios::metal::a5xx {

class FakeA5xx {
public:
    struct Stats {
        uint64_t submissions = 0, rejections = 0, packets = 0;
        uint64_t memWritePackets = 0, wordCopies = 0, bytesWritten = 0;
    };

    // Owns a copy of the initial bytes. Overlapping, empty or wrapping regions
    // are refused. Adjacent regions stay separate for bounds checks.
    pm4::Result map(uint64_t address, const std::vector<uint8_t>& initial,
                     bool readable = true, bool writable = true);
    pm4::Result read(uint64_t address, size_t length, std::vector<uint8_t>& out) const;
    // Validates the WHOLE stream before any mutation. Rejection writes no bytes.
    // Every addressed range must fit in a single mapping with required access.
    // Methods are serialized; caller must keep submission input stable during call.
    pm4::Result submit(const std::vector<uint32_t>& words);
    Stats stats() const;

private:
    struct Region {
        uint64_t address;
        std::vector<uint8_t> bytes;
        bool readable, writable;
    };
    size_t find(uint64_t address, uint64_t length) const;
    pm4::Result validate(const std::vector<uint32_t>& words) const;
    mutable std::mutex mutex_;
    std::vector<Region> regions_;
    Stats stats_;
};

}  // namespace hakios::metal::a5xx
