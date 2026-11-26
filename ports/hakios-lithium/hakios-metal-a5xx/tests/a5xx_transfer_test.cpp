// Opt-in: the copied shared Makefile does not yet link these adapter sources.
// Build with HAKIOS_A5XX_TRANSFER_TEST_STANDALONE for this file's own main,
// or HAKIOS_A5XX_TRANSFER_TEST_INTEGRATED to register with tests/test.hpp.
#if defined(HAKIOS_A5XX_TRANSFER_TEST_STANDALONE) || defined(HAKIOS_A5XX_TRANSFER_TEST_INTEGRATED)
#include "hakios-metal/fake_a5xx.hpp"
#include "hakios-metal/transfer_a5xx.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <thread>
#include <vector>
#include "test.hpp"

// Optional independent oracle: the UNMODIFIED official Mesa header is supplied
// via -I.../src/freedreno/common. It is not needed by the implementation.
#ifdef HAKIOS_A5XX_TEST_MESA_ORACLE
#include "freedreno_pm4.h"
#endif

namespace {
using namespace hakios::metal::a5xx;
using pm4::Error;
using pm4::Result;
using pm4::Stream;
uint64_t checks = 0;

void check(bool good, const char* expression, int line) {
    ++checks;
    if (!good) {
        std::fprintf(stderr, "FAIL a5xx_transfer_test.cpp:%d: %s\n", line, expression);
        ++test::failures();
    }
}
#define VERIFY(expression) check(bool(expression), #expression, __LINE__)
#define ERROR(result, expected) VERIFY((result).error == Error::expected)

// Independent parity oracle: arithmetic bit count, not the encoder's table.
uint32_t parity(uint32_t value) {
    unsigned count = 0;
    while (value) { count += value & 1; value >>= 1; }
    return (count % 2) ^ 1u;
}
uint32_t header(uint32_t opcode, uint32_t count) {
    return 0x70000000u | count | (parity(count) << 15) |
           (opcode << 16) | (parity(opcode) << 23);
}
std::vector<uint32_t> packet(uint32_t opcode, const std::vector<uint32_t>& payload) {
    std::vector<uint32_t> words{header(opcode, uint32_t(payload.size()))};
    words.insert(words.end(), payload.begin(), payload.end());
    return words;
}
std::vector<uint32_t> writePacket(uint64_t destination, uint32_t value = 0xaabbccdd) {
    return packet(0x3d, {uint32_t(destination), uint32_t(destination >> 32), value});
}
std::vector<uint32_t> copyPacket(uint64_t to, uint64_t from, uint32_t control = 0) {
    return packet(0x73, {control, uint32_t(to), uint32_t(to >> 32),
                         uint32_t(from), uint32_t(from >> 32)});
}
std::vector<uint8_t> read(FakeA5xx& fake, uint64_t at, size_t length) {
    std::vector<uint8_t> bytes;
    VERIFY(fake.read(at, length, bytes));
    return bytes;
}
void checkBoundary(const std::vector<uint32_t>& words) {
    VERIFY(words.size() >= 4);
    if (words.size() < 4) return;
    VERIFY(words[0] == header(0x12, 0));
    VERIFY(words[1] == header(0x13, 0));
    VERIFY(words[words.size() - 2] == header(0x12, 0));
    VERIFY(words.back() == header(0x13, 0));
}
}  // namespace

TEST(a5xx_headers_match_independent_and_official_mesa_oracles) {
    for (uint32_t opcode = 0; opcode < 128; ++opcode) {
        for (uint32_t count = 0; count <= 0x3fff; ++count) {
            uint32_t word = 0;
            VERIFY(pm4::type7(opcode, count, word));
            VERIFY(word == header(opcode, count));
#ifdef HAKIOS_A5XX_TEST_MESA_ORACLE
            VERIFY(word == pm4_pkt7_hdr(uint8_t(opcode), uint16_t(count)));
            VERIFY(pkt_is_type7(word));
#endif
            const auto decoded = pm4::decode(word);
            VERIFY(decoded.kind == pm4::Header::Kind::kCommand);
            VERIFY(decoded.opcode == opcode && decoded.count == count);
        }
    }
    for (uint32_t reg : {1u, 0x8u, 0x880u, 0xa800u, 0xffffu, 0x20000u, 0x3ff00u}) {
        for (uint32_t count = 1; count < 127; ++count) {
            uint32_t word = 0;
            VERIFY(pm4::type4(reg, count, word));
            VERIFY(word == (0x40000000u | count | (parity(count) << 7) |
                            (reg << 8) | (parity(reg) << 27)));
#ifdef HAKIOS_A5XX_TEST_MESA_ORACLE
            if (reg <= 0xffff) VERIFY(word == pm4_pkt4_hdr(uint16_t(reg), uint16_t(count)));
#endif
            const auto decoded = pm4::decode(word);
            VERIFY(decoded.kind == pm4::Header::Kind::kRegisters);
            VERIFY(decoded.reg == reg && decoded.count == count);
            VERIFY(pm4::decode(word ^ (1u << 7)).kind == pm4::Header::Kind::kInvalid);
            VERIFY(pm4::decode(word ^ (1u << 27)).kind == pm4::Header::Kind::kInvalid);
            VERIFY(pm4::decode(word | (1u << 26)).kind == pm4::Header::Kind::kInvalid);
        }
    }
    uint32_t untouched = 0xfeedface;
    for (uint32_t count : {0x4000u, 0xffffu, 0xffffffffu})
        ERROR(pm4::type7(1, count, untouched), kInvalidArgument);
    ERROR(pm4::type7(128, 0, untouched), kInvalidArgument);
    for (uint32_t reg : {0u, 0x40000u, 0xffffffffu})
        ERROR(pm4::type4(reg, 1, untouched), kInvalidArgument);
    for (uint32_t count : {0u, 127u, 128u, 0xffffffffu})
        ERROR(pm4::type4(1, count, untouched), kInvalidArgument);
    ERROR(pm4::type4(0x3ffff, 2, untouched), kInvalidArgument);
    VERIFY(untouched == 0xfeedface);
    VERIFY(header(0x73, 5) == 0x70738005u);
    VERIFY(header(0x3d, 3) == 0x703d8003u);
    for (unsigned bit : {14u, 15u, 23u, 24u, 25u, 26u, 27u})
        VERIFY(pm4::decode(header(0x73, 5) ^ (1u << bit)).kind == pm4::Header::Kind::kInvalid);
}

TEST(a5xx_copy_exact_fd5_single_dword_form_and_high_addresses) {
    constexpr uint64_t from = 0x1234567800001000ull, to = 0x3456789a00002000ull;
    Stream stream;
    TransferEncoder encoder(stream);
    VERIFY(encoder.copy({from, 16}, 4, {to, 16}, 8, 8));
    checkBoundary(stream.words());
    auto first = copyPacket(to + 8, from + 4), second = copyPacket(to + 12, from + 8);
    VERIFY(std::equal(first.begin(), first.end(), stream.words().begin() + 2));
    VERIFY(std::equal(second.begin(), second.end(), stream.words().begin() + 8));
    VERIFY(stream.size() == 16);
    FakeA5xx fake;
    const std::vector<uint8_t> initial{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    VERIFY(fake.map(from, initial, true, false));
    VERIFY(fake.map(to, std::vector<uint8_t>(16, 0xee)));
    VERIFY(fake.submit(stream.words()));
    const auto bytes = read(fake, to, 16);
    for (size_t i = 0; i < bytes.size(); ++i)
        VERIFY(bytes[i] == (i >= 8 ? initial[i - 4] : 0xee));
    const auto stats = fake.stats();
    VERIFY(stats.wordCopies == 2 && stats.bytesWritten == 8 && stats.memWritePackets == 0);
    VERIFY(stats.packets == 6 && stats.rejections == 0);
}

TEST(a5xx_fill_chunk_boundaries_and_all_byte_patterns) {
    const std::vector<uint64_t> counts{1, 2, pm4::kMaxWriteWords - 1,
        pm4::kMaxWriteWords, pm4::kMaxWriteWords + 1,
        2ull * pm4::kMaxWriteWords, 2ull * pm4::kMaxWriteWords + 1};
    for (const uint64_t count : counts) {
        for (const uint8_t value : {uint8_t(0), uint8_t(1), uint8_t(0x80), uint8_t(0xff)}) {
            Stream stream;
            TransferEncoder encoder(stream);
            const uint64_t length = count * 4, base = 0x100000010ull;
            VERIFY(encoder.fill({base, length + 8}, 4, length, value));
            checkBoundary(stream.words());
            const uint64_t packets = (count + pm4::kMaxWriteWords - 1) / pm4::kMaxWriteWords;
            VERIFY(stream.size() == count + 3 * packets + 4);
            uint64_t done = 0;
            for (size_t i = 2; i < stream.size() - 2;) {
                const uint32_t n = stream.words()[i] & 0x3fff;
                VERIFY(((stream.words()[i] >> 16) & 0x7f) == 0x3d);
                VERIFY(n >= 3 && n <= pm4::kMaxType7Count);
                VERIFY(stream.words()[i + 1] == uint32_t(base + 4 + 4 * done));
                VERIFY(stream.words()[i + 2] == uint32_t((base + 4 + 4 * done) >> 32));
                done += n - 2;
                i += size_t(n) + 1;
            }
            VERIFY(done == count);
            FakeA5xx fake;
            VERIFY(fake.map(base, std::vector<uint8_t>(size_t(length + 8), 0x5a)));
            VERIFY(fake.submit(stream.words()));
            const auto bytes = read(fake, base, size_t(length + 8));
            for (size_t i = 0; i < bytes.size(); ++i)
                VERIFY(bytes[i] == (i >= 4 && i < length + 4 ? value : 0x5a));
            VERIFY(fake.stats().memWritePackets == packets);
            VERIFY(fake.stats().wordCopies == 0);
        }
    }
    for (unsigned value = 0; value < 256; ++value) {
        Stream stream;
        TransferEncoder encoder(stream);
        VERIFY(encoder.fill({0x1000, 4}, 0, 4, uint8_t(value)));
        VERIFY(stream.words()[5] == value * 0x01010101u);
    }
}

TEST(a5xx_refusals_are_atomic_and_never_use_memcpy_shader_or_2d) {
    Stream stream;
    VERIFY(stream.nop(2));
    const auto before = stream.words();
    TransferEncoder encoder(stream);
    const BufferView source{0x1000, 64}, dest{0x2000, 64};
    ERROR(encoder.copy(source, 1, dest, 0, 4), kUnaligned);
    ERROR(encoder.copy(source, 0, dest, 2, 4), kUnaligned);
    ERROR(encoder.copy(source, 0, dest, 0, 3), kUnaligned);
    ERROR(encoder.fill(dest, 1, 4, 0), kUnaligned);
    ERROR(encoder.fill(dest, 0, 2, 0), kUnaligned);
    ERROR(encoder.copy(source, 65, dest, 0, 0), kOutOfRange);
    ERROR(encoder.copy(source, 64, dest, 0, 4), kOutOfRange);
    ERROR(encoder.copy(source, 0, dest, 63, 4), kOutOfRange);
    ERROR(encoder.fill(dest, 60, 8, 0), kOutOfRange);
    ERROR(encoder.fill(dest, UINT64_MAX, 1, 0), kOutOfRange);
    ERROR(encoder.copy({0x1000, 64, false, true}, 0, dest, 0, 4), kPermission);
    ERROR(encoder.copy(source, 0, {0x2000, 64, true, false}, 0, 4), kPermission);
    ERROR(encoder.fill({0x2000, 64, true, false}, 0, 4, 0), kPermission);
    ERROR(encoder.copy(source, 0, source, 0, 4), kOverlap);
    ERROR(encoder.copy(source, 0, source, 4, 8), kOverlap);
    ERROR(encoder.copy(source, 4, source, 0, 8), kOverlap);
    ERROR(encoder.copy(source, 0, {0x1004, 64}, 0, 8), kOverlap);
    ERROR(encoder.fill({UINT64_MAX - 3, 4}, 0, 4, 0), kAddressOverflow);
    ERROR(encoder.copy({UINT64_MAX - 3, 4}, 0, dest, 0, 4), kAddressOverflow);
    ERROR(encoder.copy(source, 0, {UINT64_MAX - 3, 4}, 0, 4), kAddressOverflow);
    // Even empty requests cannot hide invalid metadata.
    ERROR(encoder.fill({UINT64_MAX, 1}, 0, 0, 0), kAddressOverflow);
    VERIFY(encoder.fill({0x2001, 1, false, false}, 1, 0, 0));
    VERIFY(encoder.copy({0x1001, 1, false, false}, 1, {0x2001, 1, false, false}, 1, 0));
    VERIFY(stream.words() == before);
    // Adjacent non-overlapping views and an unaligned BO with aligned subrange.
    VERIFY(encoder.copy({0x1001, 15}, 3, {0x1010, 16}, 0, 12));
    VERIFY(encoder.fill({0x2001, 15}, 3, 12, 0));
    for (size_t i = 0; i < stream.size();) {
        const auto decoded = pm4::decode(stream.words()[i]);
        VERIFY(decoded.kind == pm4::Header::Kind::kCommand);
        VERIFY(decoded.opcode == 0x10 || decoded.opcode == 0x12 || decoded.opcode == 0x13 ||
               decoded.opcode == 0x3d || decoded.opcode == 0x73);
        i += size_t(decoded.count) + 1;
    }
}

TEST(a5xx_budget_exact_edges_huge_lengths_and_low_level_refusals) {
    const BufferView source{0x1000, 16}, dest{0x2000, 16};
    for (size_t budget = 0; budget <= 30; ++budget) {
        Stream copy(budget), fill(budget);
        TransferEncoder copyEncoder(copy), fillEncoder(fill);
        const auto c = copyEncoder.copy(source, 0, dest, 0, 16);
        const auto f = fillEncoder.fill(dest, 0, 16, 0xff);
        VERIFY(bool(c) == (budget >= 28));
        VERIFY(bool(f) == (budget >= 11));
        VERIFY(copy.size() == (c ? 28u : 0u));
        VERIFY(fill.size() == (f ? 11u : 0u));
    }
    Stream stream(64);
    VERIFY(stream.nop());
    const auto before = stream.words();
    TransferEncoder encoder(stream);
    ERROR(encoder.fill({0, UINT64_MAX}, 0, UINT64_MAX - 3, 0), kLimit);
    ERROR(encoder.copy({0, 1ull << 61}, 0, {1ull << 62, 1ull << 61}, 0, 1ull << 61), kLimit);
    ERROR(stream.memToMemWord(UINT64_MAX - 3, 0), kAddressOverflow);
    ERROR(stream.memToMemWord(0, UINT64_MAX - 3), kAddressOverflow);
    ERROR(stream.memToMemWord(2, 0), kUnaligned);
    const uint32_t value = 42;
    ERROR(stream.memWrite(0, &value, 0), kInvalidArgument);
    ERROR(stream.memWrite(0, nullptr, 1), kInvalidArgument);
    ERROR(stream.memWrite(0, &value, pm4::kMaxWriteWords + 1), kInvalidArgument);
    ERROR(stream.memWrite(1, &value, 1), kUnaligned);
    ERROR(stream.memWrite(UINT64_MAX - 3, &value, 1), kAddressOverflow);
    ERROR(stream.nop(0x4000), kInvalidArgument);
    ERROR(stream.append(stream), kInvalidArgument);
    VERIFY(stream.words() == before);
    Stream tiny(1);
    VERIFY(tiny.nop());
    ERROR(tiny.waitForIdle(), kLimit);
    ERROR(tiny.append(stream), kLimit);
    VERIFY(tiny.size() == 1 && tiny.remaining() == 0);
    stream.clear();
    VERIFY(stream.remaining() == 64);
}

TEST(a5xx_fake_strict_headers_opcodes_controls_and_no_partial_effects) {
    FakeA5xx fake;
    VERIFY(fake.map(0x1000, std::vector<uint8_t>(16, 0x5a)));
    const auto original = read(fake, 0x1000, 16);
    auto reject = [&](const std::vector<uint32_t>& words, Error expected) {
        const auto before = fake.stats();
        const Result result = fake.submit(words);
        VERIFY(result.error == expected);
        VERIFY(fake.stats().bytesWritten == before.bytesWritten);
        VERIFY(fake.stats().packets == before.packets);
        VERIFY(read(fake, 0x1000, 16) == original);
    };
    for (uint32_t invalid : {0u, 0x80000000u, 0xc0000000u, 0xffffffffu})
        reject({invalid}, Error::kInvalidHeader);
    const auto good = writePacket(0x1000);
    for (unsigned bit : {14u, 15u, 23u, 24u, 25u, 26u, 27u}) {
        auto words = good;
        words[0] ^= 1u << bit;
        reject(words, Error::kInvalidHeader);
    }
    for (size_t size = 1; size < good.size(); ++size)
        reject(std::vector<uint32_t>(good.begin(), good.begin() + size), Error::kTruncatedPacket);
    for (uint32_t opcode = 0; opcode < 128; ++opcode) {
        if (opcode == 0x10 || opcode == 0x12 || opcode == 0x13 || opcode == 0x26 ||
            opcode == 0x3d || opcode == 0x73) continue;
        reject(packet(opcode, {}), opcode == 0x75 ? Error::kUnsupportedOpcode : Error::kUnknownOpcode);
    }
    // A shader-dispatch-looking packet does not modify the model's bytes.
    reject(packet(0x2d, {1, 1, 1, 1}), Error::kUnknownOpcode);
    reject(packet(0x75, {1, 0x1000, 0, 0x1004, 0}), Error::kUnsupportedOpcode);
    for (uint32_t opcode : {0x12u, 0x13u, 0x26u})
        reject(packet(opcode, {0}), Error::kUnsupportedControl);
    for (size_t size : {0u, 1u, 2u})
        reject(packet(0x3d, std::vector<uint32_t>(size)), Error::kUnsupportedControl);
    for (uint32_t control : {1u, 2u, 4u, 1u << 29, 1u << 30, 1u << 31, 0xffffffffu})
        reject(copyPacket(0x1004, 0x1000, control), Error::kUnsupportedControl);
    for (size_t size : {0u, 1u, 2u, 3u, 4u, 6u, 7u, 9u})
        reject(packet(0x73, std::vector<uint32_t>(size)), Error::kUnsupportedControl);
    uint32_t regHeader = 0;
    VERIFY(pm4::type4(0xa800, 1, regHeader));
    reject({regHeader, 0}, Error::kUnsupportedPacket);
    reject({regHeader ^ (1u << 7), 0}, Error::kInvalidHeader);
    reject({regHeader | (1u << 26), 0}, Error::kInvalidHeader);
    auto tail = good;
    tail.push_back(header(0x75, 0));
    const Result failure = fake.submit(tail);
    VERIFY(failure.error == Error::kUnsupportedOpcode && failure.wordOffset == 4);
    VERIFY(read(fake, 0x1000, 16) == original);
    // NOP contents are opaque, even when they look like forbidden opcodes.
    VERIFY(fake.submit(packet(0x10, {header(0x75, 0), 0xffffffffu})));
    VERIFY(fake.submit({}));
}

TEST(a5xx_fake_mapping_permissions_overflow_alignment_and_little_endian) {
    FakeA5xx fake;
    VERIFY(fake.map(0x1000, std::vector<uint8_t>(4, 0x11)));
    VERIFY(fake.map(0x1004, std::vector<uint8_t>(4, 0x22)));
    VERIFY(fake.map(0x2000, std::vector<uint8_t>(4, 0x33), true, false));
    VERIFY(fake.map(0x3000, std::vector<uint8_t>(4, 0x44), false, true));
    VERIFY(fake.map(0x4001, std::vector<uint8_t>(4, 0x55)));
    ERROR(fake.map(0x1000, std::vector<uint8_t>(4)), kOverlap);
    ERROR(fake.map(0x0fff, std::vector<uint8_t>(2)), kOverlap);
    ERROR(fake.map(0x1007, std::vector<uint8_t>(2)), kOverlap);
    ERROR(fake.map(0, {}), kInvalidArgument);
    ERROR(fake.map(UINT64_MAX - 3, std::vector<uint8_t>(4)), kAddressOverflow);
    ERROR(fake.submit(writePacket(0x1008)), kOutOfRange);
    ERROR(fake.submit(writePacket(0x1002)), kUnaligned);
    ERROR(fake.submit(writePacket(UINT64_MAX - 3)), kAddressOverflow);
    ERROR(fake.submit(packet(0x3d, {0x1000, 0, 1, 2})), kOutOfRange);
    ERROR(fake.submit(writePacket(0x2000)), kPermission);
    ERROR(fake.submit(copyPacket(0x1000, 0x3000)), kPermission);
    ERROR(fake.submit(copyPacket(0x2000, 0x1000)), kPermission);
    ERROR(fake.submit(copyPacket(0x1000, 0x1008)), kOutOfRange);
    ERROR(fake.submit(copyPacket(0x1008, 0x1000)), kOutOfRange);
    ERROR(fake.submit(copyPacket(0x1000, UINT64_MAX - 3)), kAddressOverflow);
    ERROR(fake.submit(copyPacket(UINT64_MAX - 3, 0x1000)), kAddressOverflow);
    ERROR(fake.submit(copyPacket(0x1000, 0x1001)), kUnaligned);
    ERROR(fake.submit(copyPacket(0x4004, 0x1000)), kOutOfRange);
    std::vector<uint8_t> untouched{0xee};
    ERROR(fake.read(0x3000, 4, untouched), kPermission);
    ERROR(fake.read(0x1000, 8, untouched), kOutOfRange);
    ERROR(fake.read(UINT64_MAX, 1, untouched), kAddressOverflow);
    VERIFY(untouched == std::vector<uint8_t>{0xee});
    VERIFY(fake.submit(writePacket(0x1000, 0x12345678)));
    VERIFY(read(fake, 0x1000, 4) == (std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12}));
    VERIFY(fake.submit(copyPacket(0x1004, 0x1000)));
    VERIFY(read(fake, 0x1004, 4) == (std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12}));
    // Destination-only memory is writable even when it cannot be a copy source.
    VERIFY(fake.submit(copyPacket(0x3000, 0x1000)));
}

TEST(a5xx_random_transfer_sequences_against_byte_reference) {
    constexpr uint64_t from = 0x100000000ull, to = 0x200000000ull;
    constexpr size_t bytes = 4096;
    std::mt19937 rng(0xa5302026);
    std::vector<uint8_t> source(bytes), expected(bytes, 0x5a);
    for (auto& byte : source) byte = uint8_t(rng());
    FakeA5xx fake;
    VERIFY(fake.map(from, source, true, false));
    VERIFY(fake.map(to, expected));
    Stream stream;
    TransferEncoder encoder(stream);
    for (size_t iteration = 0; iteration < 2000; ++iteration) {
        const size_t length = size_t(rng() % 128) * 4;
        const size_t srcOffset = size_t(rng() % ((bytes - length) / 4 + 1)) * 4;
        const size_t dstOffset = size_t(rng() % ((bytes - length) / 4 + 1)) * 4;
        if (rng() & 1) {
            VERIFY(encoder.copy({from, bytes}, srcOffset, {to, bytes}, dstOffset, length));
            std::copy_n(source.begin() + srcOffset, length, expected.begin() + dstOffset);
        } else {
            const uint8_t value = uint8_t(rng());
            VERIFY(encoder.fill({to, bytes}, dstOffset, length, value));
            std::fill_n(expected.begin() + dstOffset, length, value);
        }
        VERIFY(fake.submit(stream.words()));
        VERIFY(read(fake, to, bytes) == expected);
        stream.clear();
    }
    // Mutation fuzz: every refused submission must leave all bytes unchanged.
    const auto seed = copyPacket(to, from);
    for (unsigned iteration = 0; iteration < 12000; ++iteration) {
        auto words = seed;
        const size_t index = rng() % words.size();
        words[index] ^= 1u << (rng() % 32);
        const auto before = read(fake, to, bytes);
        const auto result = fake.submit(words);
        if (!result) VERIFY(read(fake, to, bytes) == before);
    }
}

TEST(a5xx_concurrent_fake_submissions_snapshots_and_maps) {
    FakeA5xx fake;
    constexpr size_t threads = 6, iterations = 128;
    std::vector<std::thread> workers;
    std::atomic<unsigned> failures{0};
    for (size_t thread = 0; thread < threads; ++thread) {
        workers.emplace_back([&, thread] {
            const uint64_t base = 0x1000 + thread * 0x1000;
            if (!fake.map(base, std::vector<uint8_t>(64))) { ++failures; return; }
            for (size_t i = 0; i < iterations; ++i) {
                Stream stream;
                TransferEncoder encoder(stream);
                const uint8_t value = uint8_t(i + thread);
                if (!encoder.fill({base, 64}, 0, 64, value) ||
                    !fake.submit(stream.words())) { ++failures; return; }
                std::vector<uint8_t> snapshot;
                if (!fake.read(base, 64, snapshot) ||
                    snapshot != std::vector<uint8_t>(64, value)) { ++failures; return; }
                (void)fake.stats();
            }
        });
    }
    for (auto& worker : workers) worker.join();
    VERIFY(failures.load() == 0);
    VERIFY(fake.stats().submissions == threads * iterations);
    VERIFY(fake.stats().bytesWritten == threads * iterations * 64);
}

#ifdef HAKIOS_A5XX_TRANSFER_TEST_STANDALONE
int main() {
    for (const auto& item : test::cases()) {
        const int before = test::failures();
        item.run();
        std::printf("%s %s\n", test::failures() == before ? "ok" : "FAIL", item.name);
    }
    std::printf("A5xx HOST MODEL ONLY: %zu tests, %llu checks, %d failures; hardware_tested=false\n",
                test::cases().size(), static_cast<unsigned long long>(checks), test::failures());
    return test::failures() ? EXIT_FAILURE : EXIT_SUCCESS;
}
#endif
#endif
