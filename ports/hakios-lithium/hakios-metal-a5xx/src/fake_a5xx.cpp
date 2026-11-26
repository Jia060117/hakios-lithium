#include "hakios-metal/fake_a5xx.hpp"

#include <limits>
#include <new>
#include <stdexcept>

namespace hakios::metal::a5xx {
namespace {
using pm4::Error;
using pm4::Result;

// Independent from the encoder's folding parity and decode(). This model checks
// the actual header bits, including reserved bit 14 and bits 24..27 in type 7.
unsigned oddBit(uint32_t field) {
    unsigned ones = 0;
    for (; field; field >>= 1) ones += field & 1u;
    return (ones & 1u) ^ 1u;
}
bool valid7(uint32_t word) {
    return word >> 28 == 7 && !(word & 0x0f004000u) &&
           ((word >> 15) & 1u) == oddBit(word & 0x3fff) &&
           ((word >> 23) & 1u) == oddBit((word >> 16) & 0x7f);
}
bool valid4(uint32_t word) {
    const uint32_t count = word & 0x7f, reg = (word >> 8) & 0x3ffff;
    return word >> 28 == 4 && !(word & 0x04000000u) &&
           reg && count && count < 0x7f && count - 1 <= 0x3ffff - reg &&
           ((word >> 7) & 1u) == oddBit(count) && ((word >> 27) & 1u) == oddBit(reg);
}
uint64_t address(const uint32_t* words) {
    return uint64_t(words[0]) | (uint64_t(words[1]) << 32);
}
void store(uint8_t* to, uint32_t word) {
    // Explicit GPU little-endian memory order, independent of host endianness.
    for (unsigned i = 0; i < 4; ++i) to[i] = uint8_t(word >> (8 * i));
}
}  // namespace

size_t FakeA5xx::find(uint64_t at, uint64_t length) const {
    for (size_t i = 0; i < regions_.size(); ++i) {
        const Region& region = regions_[i];
        if (at >= region.address && at - region.address <= region.bytes.size() &&
            length <= region.bytes.size() - (at - region.address))
            return i;
    }
    return regions_.size();
}

pm4::Result FakeA5xx::map(uint64_t at, const std::vector<uint8_t>& initial,
                          bool readable, bool writable) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (initial.empty()) return {Error::kInvalidArgument};
    if (initial.size() > std::numeric_limits<uint64_t>::max() - at)
        return {Error::kAddressOverflow};
    for (const auto& region : regions_) {
        if (at <= region.address ? region.address - at < initial.size()
                                 : at - region.address < region.bytes.size())
            return {Error::kOverlap};
    }
    try {
        regions_.push_back({at, initial, readable, writable});
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    } catch (const std::length_error&) {
        return {Error::kLimit};
    }
    return {};
}

pm4::Result FakeA5xx::read(uint64_t at, size_t length, std::vector<uint8_t>& out) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (length > std::numeric_limits<uint64_t>::max() - at)
        return {Error::kAddressOverflow};
    const size_t index = find(at, length);
    if (index == regions_.size()) return {Error::kOutOfRange};
    const Region& region = regions_[index];
    if (!region.readable) return {Error::kPermission};
    try {
        const size_t offset = size_t(at - region.address);
        std::vector<uint8_t> bytes(region.bytes.begin() + offset,
                                    region.bytes.begin() + offset + length);
        out.swap(bytes);
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    } catch (const std::length_error&) {
        return {Error::kLimit};
    }
    return {};
}

pm4::Result FakeA5xx::validate(const std::vector<uint32_t>& words) const {
    for (size_t i = 0; i < words.size();) {
        const uint32_t header = words[i];
        if (valid4(header)) return {Error::kUnsupportedPacket, i};
        if (!valid7(header)) return {Error::kInvalidHeader, i};
        const uint32_t count = header & 0x3fff, opcode = (header >> 16) & 0x7f;
        if (count > words.size() - i - 1) return {Error::kTruncatedPacket, i};
        const uint32_t* payload = words.data() + i + 1;
        switch (opcode) {
        case 0x10: break; // CP_NOP payload is opaque, never interpreted as packets
        case 0x12: case 0x13: case 0x26:
            if (count) return {Error::kUnsupportedControl, i};
            break;
        case 0x3d: {
            if (count < 3) return {Error::kUnsupportedControl, i};
            const uint64_t to = address(payload), length = 4ull * (count - 2);
            if (to & 3) return {Error::kUnaligned, i};
            if (length > std::numeric_limits<uint64_t>::max() - to)
                return {Error::kAddressOverflow, i};
            const size_t index = find(to, length);
            if (index == regions_.size()) return {Error::kOutOfRange, i};
            if (!regions_[index].writable) return {Error::kPermission, i};
            break;
        }
        case 0x73: {
            if (count != 5 || payload[0] != 0) return {Error::kUnsupportedControl, i};
            const uint64_t to = address(payload + 1), from = address(payload + 3);
            if ((to | from) & 3) return {Error::kUnaligned, i};
            if (to > std::numeric_limits<uint64_t>::max() - 4 ||
                from > std::numeric_limits<uint64_t>::max() - 4)
                return {Error::kAddressOverflow, i};
            const size_t dst = find(to, 4), src = find(from, 4);
            if (dst == regions_.size() || src == regions_.size())
                return {Error::kOutOfRange, i};
            if (!regions_[dst].writable || !regions_[src].readable)
                return {Error::kPermission, i};
            break;
        }
        case 0x75: return {Error::kUnsupportedOpcode, i};
        default: return {Error::kUnknownOpcode, i}; // includes every shader dispatch
        }
        i += size_t(count) + 1;
    }
    return {};
}

pm4::Result FakeA5xx::submit(const std::vector<uint32_t>& words) {
    std::lock_guard<std::mutex> guard(mutex_);
    ++stats_.submissions;
    const Result result = validate(words);
    if (!result) {
        ++stats_.rejections;
        return result;
    }
    for (size_t i = 0; i < words.size();) {
        const uint32_t count = words[i] & 0x3fff, opcode = (words[i] >> 16) & 0x7f;
        const uint32_t* payload = words.data() + i + 1;
        ++stats_.packets;
        if (opcode == 0x3d) {
            const uint64_t to = address(payload);
            Region& region = regions_[find(to, 4ull * (count - 2))];
            uint8_t* bytes = region.bytes.data() + size_t(to - region.address);
            for (uint32_t j = 2; j < count; ++j) store(bytes + 4ull * (j - 2), payload[j]);
            ++stats_.memWritePackets;
            stats_.bytesWritten += 4ull * (count - 2);
        } else if (opcode == 0x73) {
            const uint64_t to = address(payload + 1), from = address(payload + 3);
            Region& dst = regions_[find(to, 4)];
            const Region& src = regions_[find(from, 4)];
            uint32_t value = 0;
            for (unsigned j = 0; j < 4; ++j)
                value |= uint32_t(src.bytes[size_t(from - src.address) + j]) << (8 * j);
            store(dst.bytes.data() + size_t(to - dst.address), value);
            ++stats_.wordCopies;
            stats_.bytesWritten += 4;
        }
        i += size_t(count) + 1;
    }
    return {};
}

FakeA5xx::Stats FakeA5xx::stats() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return stats_;
}

}  // namespace hakios::metal::a5xx
