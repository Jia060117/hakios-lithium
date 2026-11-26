// Host-only A5xx transfer packets. No dependency on the A6xx stream or a5xx.hpp.
// Source: official Mesa 25.0.7, freedreno_pm4.h and adreno_pm4.xml.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hakios::metal::a5xx::pm4 {

enum Opcode : uint32_t {
    kNop = 0x10,
    kWaitMemWrites = 0x12,
    kWaitForMe = 0x13,
    kWaitForIdle = 0x26,
    kMemWrite = 0x3d,
    kMemToMem = 0x73,
    // CP_MEMCPY (0x75) is A6XX- in Mesa. Deliberately no encoder for it.
};

enum class Error {
    kNone, kInvalidArgument, kInvalidHeader, kTruncatedPacket, kUnsupportedPacket,
    kUnknownOpcode, kUnsupportedOpcode, kUnsupportedControl, kUnaligned,
    kAddressOverflow, kOutOfRange, kPermission, kOverlap, kLimit, kNoMemory,
};

const char* errorString(Error error);

struct Result {
    Error error = Error::kNone;
    size_t wordOffset = 0;  // fake: offending packet's header index
    explicit operator bool() const { return error == Error::kNone; }
};

constexpr uint32_t kMaxType7Count = 0x3fff;
constexpr uint32_t kMaxWriteWords = kMaxType7Count - 2;
// Software allocation/encoding budget, NOT a measured hardware submission limit.
constexpr size_t kDefaultMaxStreamWords = 1024 * 1024;

constexpr uint32_t oddParity(uint32_t value) {
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    return (~0x6996u >> (value & 0xf)) & 1u;
}

// Checked versions of Mesa's canonical builders; invalid input leaves out alone.
Result type4(uint32_t reg, uint32_t count, uint32_t& out);
Result type7(uint32_t opcode, uint32_t count, uint32_t& out);

struct Header {
    enum class Kind { kInvalid, kRegisters, kCommand };
    Kind kind = Kind::kInvalid;
    uint32_t reg = 0, opcode = 0, count = 0;
};
Header decode(uint32_t word);

class Stream {
public:
    explicit Stream(size_t maxWords = kDefaultMaxStreamWords) : maxWords_(maxWords) {}
    const std::vector<uint32_t>& words() const { return words_; }
    size_t size() const { return words_.size(); }
    size_t remaining() const { return maxWords_ - words_.size(); }
    void clear() { words_.clear(); }

    // Each method either appends its entire packet/stream or leaves this unchanged.
    Result append(const Stream& other);
    Result nop(uint32_t count = 0);
    Result waitMemWrites();
    Result waitForMe();
    Result waitForIdle();
    Result memWrite(uint64_t destination, const uint32_t* values, uint32_t count);
    // Exactly fd5_emit.c:fd5_mem_to_mem's single-DWORD form:
    // payload = {0, dst_lo, dst_hi, src_lo, src_hi}; NOT a bulk length.
    Result memToMemWord(uint64_t destination, uint64_t source);

private:
    Result packet(uint32_t opcode, const uint32_t* values, uint32_t count);
    std::vector<uint32_t> words_;
    size_t maxWords_;
};

}  // namespace hakios::metal::a5xx::pm4
