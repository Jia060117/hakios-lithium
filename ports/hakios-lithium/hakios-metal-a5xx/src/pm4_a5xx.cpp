#include "hakios-metal/pm4_a5xx.hpp"

#include <limits>
#include <new>
#include <stdexcept>

namespace hakios::metal::a5xx::pm4 {

const char* errorString(Error error) {
    switch (error) {
    case Error::kNone: return "ok";
    case Error::kInvalidArgument: return "invalid argument";
    case Error::kInvalidHeader: return "invalid packet type, reserved bits or parity";
    case Error::kTruncatedPacket: return "packet payload exceeds stream";
    case Error::kUnsupportedPacket: return "register packets are outside transfer subset";
    case Error::kUnknownOpcode: return "unknown or unimplemented opcode";
    case Error::kUnsupportedOpcode: return "CP_MEMCPY is A6xx+, forbidden on A5xx";
    case Error::kUnsupportedControl: return "unsupported packet length/control form";
    case Error::kUnaligned: return "nonempty transfer requires DWORD alignment";
    case Error::kAddressOverflow: return "GPU address range wraps";
    case Error::kOutOfRange: return "range outside one buffer/mapping";
    case Error::kPermission: return "buffer/mapping lacks requested access";
    case Error::kOverlap: return "overlapping transfer or mapping refused";
    case Error::kLimit: return "software stream budget exceeded";
    case Error::kNoMemory: return "host allocation failed";
    }
    return "invalid error code";
}

Result type4(uint32_t reg, uint32_t count, uint32_t& out) {
    if (!reg || reg > 0x3ffff || !count || count >= 0x7f ||
        count - 1 > 0x3ffff - reg)
        return {Error::kInvalidArgument};
    out = 0x40000000u | count | (oddParity(count) << 7) |
          (reg << 8) | (oddParity(reg) << 27);
    return {};
}

Result type7(uint32_t opcode, uint32_t count, uint32_t& out) {
    if (opcode > 0x7f || count > kMaxType7Count) return {Error::kInvalidArgument};
    out = 0x70000000u | count | (oddParity(count) << 15) |
          (opcode << 16) | (oddParity(opcode) << 23);
    return {};
}

Header decode(uint32_t word) {
    Header h;
    if (word >> 28 == 4) {
        h.reg = (word >> 8) & 0x3ffff;
        h.count = word & 0x7f;
        if (!(word & 0x04000000u) && h.reg && h.count && h.count < 0x7f &&
            h.count - 1 <= 0x3ffff - h.reg &&
            ((word >> 7) & 1) == oddParity(h.count) &&
            ((word >> 27) & 1) == oddParity(h.reg))
            h.kind = Header::Kind::kRegisters;
    } else if (word >> 28 == 7) {
        h.opcode = (word >> 16) & 0x7f;
        h.count = word & 0x3fff;
        if (!(word & 0x0f004000u) &&
            ((word >> 15) & 1) == oddParity(h.count) &&
            ((word >> 23) & 1) == oddParity(h.opcode))
            h.kind = Header::Kind::kCommand;
    }
    return h;
}

Result Stream::append(const Stream& other) {
    if (other.size() > remaining()) return {Error::kLimit};
    if (!other.size()) return {};
    // Self-append would otherwise invalidate its own input iterators on growth.
    if (&other == this) return {Error::kInvalidArgument};
    try {
        words_.insert(words_.end(), other.words_.begin(), other.words_.end());
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    } catch (const std::length_error&) {
        return {Error::kLimit};
    }
    return {};
}

Result Stream::packet(uint32_t opcode, const uint32_t* values, uint32_t count) {
    uint32_t header = 0;
    Result result = type7(opcode, count, header);
    if (!result) return result;
    if (count && !values) return {Error::kInvalidArgument};
    if (size_t(count) + 1 > remaining()) return {Error::kLimit};
    try {
        // Reserve first: subsequent writes of uint32_t cannot throw.
        const size_t needed = words_.size() + count + 1;
        if (needed > words_.capacity()) {
            const size_t capacity = words_.capacity();
            const size_t grown = capacity > maxWords_ / 2 ? maxWords_ : capacity * 2;
            words_.reserve(needed > grown ? needed : grown);
        }
        words_.push_back(header);
        if (count) words_.insert(words_.end(), values, values + count);
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    } catch (const std::length_error&) {
        return {Error::kLimit};
    }
    return {};
}

Result Stream::nop(uint32_t count) {
    if (count > kMaxType7Count) return {Error::kInvalidArgument};
    if (size_t(count) + 1 > remaining()) return {Error::kLimit};
    try {
        std::vector<uint32_t> zeros(count, 0);
        return packet(kNop, zeros.data(), count);
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    }
}
Result Stream::waitMemWrites() { return packet(kWaitMemWrites, nullptr, 0); }
Result Stream::waitForMe() { return packet(kWaitForMe, nullptr, 0); }
Result Stream::waitForIdle() { return packet(kWaitForIdle, nullptr, 0); }

Result Stream::memWrite(uint64_t destination, const uint32_t* values, uint32_t count) {
    if (!count || count > kMaxWriteWords || !values) return {Error::kInvalidArgument};
    if (destination & 3) return {Error::kUnaligned};
    if (4ull * count > std::numeric_limits<uint64_t>::max() - destination)
        return {Error::kAddressOverflow};
    if (size_t(count) + 3 > remaining()) return {Error::kLimit};
    try {
        std::vector<uint32_t> payload;
        payload.reserve(size_t(count) + 2);
        payload.push_back(uint32_t(destination));
        payload.push_back(uint32_t(destination >> 32));
        payload.insert(payload.end(), values, values + count);
        return packet(kMemWrite, payload.data(), count + 2);
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    }
}

Result Stream::memToMemWord(uint64_t destination, uint64_t source) {
    if ((destination | source) & 3) return {Error::kUnaligned};
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    if (destination > max - 4 || source > max - 4) return {Error::kAddressOverflow};
    const uint32_t payload[] = {0, uint32_t(destination), uint32_t(destination >> 32),
                                uint32_t(source), uint32_t(source >> 32)};
    return packet(kMemToMem, payload, 5);
}

}  // namespace hakios::metal::a5xx::pm4
