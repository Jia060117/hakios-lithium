#include "hakios-metal/transfer_a5xx.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <vector>

namespace hakios::metal::a5xx {
namespace {
using pm4::Error;
using pm4::Result;

Result range(const BufferView& view, uint64_t offset, uint64_t length, uint64_t& at) {
    if (view.length > std::numeric_limits<uint64_t>::max() - view.gpuAddress)
        return {Error::kAddressOverflow};
    if (offset > view.length || length > view.length - offset)
        return {Error::kOutOfRange};
    at = view.gpuAddress + offset;
    return {};
}

Result boundary(pm4::Stream& stream) {
    Result result = stream.waitMemWrites();
    return result ? stream.waitForMe() : result;
}
}  // namespace

pm4::Result TransferEncoder::copy(const BufferView& source, uint64_t sourceOffset,
                                  const BufferView& destination, uint64_t destinationOffset,
                                  uint64_t length) {
    uint64_t from = 0, to = 0;
    Result result = range(source, sourceOffset, length, from);
    if (!result) return result;
    result = range(destination, destinationOffset, length, to);
    if (!result || !length) return result;
    if ((from | to | length) & 3) return {Error::kUnaligned};
    if (!source.readable || !destination.writable) return {Error::kPermission};
    // Both ends were checked above; difference comparison cannot wrap.
    if (from <= to ? to - from < length : from - to < length)
        return {Error::kOverlap};
    const uint64_t words = length / 4;
    if (stream_.remaining() < 4 || words > (stream_.remaining() - 4) / 6)
        return {Error::kLimit};
    pm4::Stream added(stream_.remaining());
    result = boundary(added);
    if (!result) return result;
    for (uint64_t i = 0; i < words; ++i) {
        result = added.memToMemWord(to + 4 * i, from + 4 * i);
        if (!result) return result;
    }
    result = boundary(added);
    return result ? stream_.append(added) : result;
}

pm4::Result TransferEncoder::fill(const BufferView& destination, uint64_t offset,
                                  uint64_t length, uint8_t value) {
    uint64_t to = 0;
    Result result = range(destination, offset, length, to);
    if (!result || !length) return result;
    if ((to | length) & 3) return {Error::kUnaligned};
    if (!destination.writable) return {Error::kPermission};
    const uint64_t words = length / 4;
    const uint64_t packets = words / pm4::kMaxWriteWords + (words % pm4::kMaxWriteWords != 0);
    const uint64_t needed = words + 3 * packets + 4; // bounded by uint64_t length / 4
    if (needed > stream_.remaining()) return {Error::kLimit};
    pm4::Stream added(stream_.remaining());
    result = boundary(added);
    if (!result) return result;
    try {
        const uint32_t chunk = uint32_t(std::min<uint64_t>(words, pm4::kMaxWriteWords));
        std::vector<uint32_t> pattern(chunk, uint32_t(value) * 0x01010101u);
        for (uint64_t done = 0; done < words;) {
            const uint32_t count = uint32_t(std::min<uint64_t>(chunk, words - done));
            result = added.memWrite(to + 4 * done, pattern.data(), count);
            if (!result) return result;
            done += count;
        }
    } catch (const std::bad_alloc&) {
        return {Error::kNoMemory};
    }
    result = boundary(added);
    return result ? stream_.append(added) : result;
}

}  // namespace hakios::metal::a5xx
