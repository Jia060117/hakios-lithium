// SPDX-License-Identifier: MIT
// M0 packet/validation tests ONLY. The memory transport never submits or executes
// a shader. Synthetic zero "instructions" below are not an A5xx fill_id binary.
#include "hakios-metal/compute_a5xx.hpp"
#include "hakios-metal/command.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <map>

using namespace hakios::metal;
namespace ca = hakios::metal::compute_a5xx;

namespace {

class MemoryOnly final : public Transport {
public:
    uint64_t base = 0x1200000000ull;
    unsigned submissions = 0;
    int call(uint32_t request, void* data, size_t) override {
        switch (request) {
        case kGemNew: {
            auto& n = *static_cast<MsmGemNew*>(data);
            n.handle = ++last_;
            bytes_[n.handle].resize(size_t(n.size));
            return 0;
        }
        case kGemInfo: {
            auto& n = *static_cast<MsmGemInfo*>(data);
            n.value = base + uint64_t(n.handle) * 0x100000;
            return 0;
        }
        case kGemClose:
            bytes_.erase(static_cast<DrmGemClose*>(data)->handle);
            return 0;
        default:
            ++submissions; // any other request is a test failure, not a fake execution
            return ENOTSUP;
        }
    }
    void* map(uint32_t handle, uint64_t) override { return bytes_.at(handle).data(); }
    void unmap(uint32_t, void*) override {}
    int64_t nowNs() override { return 0; }
private:
    uint32_t last_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> bytes_;
};

struct Fixture {
    MemoryOnly* memory = new MemoryOnly;
    Gpu gpu{std::unique_ptr<Transport>(memory)};
    std::array<uint32_t, 64> words{};
    std::array<uint32_t, 16> immediates{{0x1234}};
    Kernel k{};
    ca::Grid grid{{16, 1, 1}, {64, 1, 1}, 1};
    ca::Program program{0x2300000000ull, 256, 530};
    uint64_t flush = 0x3400000004ull;
    std::unique_ptr<Buffer> buffer;
    Bindings b;
    pm4::Stream stream;
    std::string why = "stale";
    Fixture() {
        int error = -1;
        auto bo = gpu.newBo(262144, 0, &error);
        CHECK_EQ(error, 0);
        buffer = std::make_unique<Buffer>(std::move(bo), 262144);
        b.buffers.push_back({buffer.get(), 0x44});
        std::memset(buffer->contents(), 0xa5, 262144);
        k.words = words.data();
        k.numWords = uint32_t(words.size());
        k.instrlen = 1;
        k.constlen = 4;
        k.maxReg = 3;
        k.maxHalfReg = -1;
        k.doubleThreadsize = true; // A530 base32 * 2, not the A6xx base64
        std::copy_n(grid.local, 3, k.localSize);
        k.localIdRegid = 0;
        k.workGroupIdRegid = 0xc0; // A5xx shared r48, ir3_parser.y:846-851
        k.immediates = immediates.data();
        k.numImmediates = 4;
        k.driverParamsOffsetVec4 = -1;
        k.numBufs = 1;
        k.bufs[0] = {4096, kNoConstRegid};
        k.numwgConstRegid = kNoConstRegid;
        k.name = "M0-fill_id-layout-only";
    }
    bool encode(const ca::Immediates& override = {}) {
        return ca::encode(stream, k, grid, b, program, flush, &why, override);
    }
    void rejected(const char* reason) {
        stream.nop(2);
        const auto old = stream.words();
        CHECK(!encode());
        CHECK(why.find(reason) != std::string::npos);
        CHECK(stream.words() == old);
        CHECK_EQ(memory->submissions, 0u);
    }
};

struct Packet {
    uint32_t reg; // zero for commands
    uint32_t opcode;
    std::vector<uint32_t> data;
    bool operator==(const Packet& p) const { return reg == p.reg && opcode == p.opcode && data == p.data; }
};

std::vector<Packet> packets(const pm4::Stream& s) {
    std::vector<Packet> result;
    for (size_t at = 0; at < s.size();) {
        const auto h = pm4::decode(s.words()[at]);
        CHECK(h.kind != pm4::Header::kBad);
        if (h.kind == pm4::Header::kBad || h.count > s.size() - at - 1) {
            CHECK(false);
            break;
        }
        if (h.kind == pm4::Header::kCommand) {
            CHECK(h.opcode == 0x30 || h.opcode == 0x33 || h.opcode == 0x46 || h.opcode == 0x26);
            // These A6xx packet values must NEVER appear as commands.
            CHECK(h.opcode != 0x32 && h.opcode != 0x34 && h.opcode != 0x65 && h.opcode != 0x3e);
        }
        result.push_back({h.kind == pm4::Header::kRegisters ? h.reg : 0, h.opcode,
                          {s.words().begin() + at + 1, s.words().begin() + at + 1 + h.count}});
        at += h.count + 1;
    }
    return result;
}

std::vector<uint32_t> registerData(const std::vector<Packet>& ps, uint32_t reg) {
    for (const auto& p : ps) if (p.reg == reg) return p.data;
    return {};
}

std::vector<Packet> loads(const std::vector<Packet>& ps, uint32_t block, uint32_t type) {
    std::vector<Packet> found;
    for (const auto& p : ps)
        if (p.opcode == 0x30 && p.data.size() >= 3 &&
            ((p.data[0] >> 18) & 15) == block && (p.data[1] & 3) == type)
            found.push_back(p);
    return found;
}

} // namespace

TEST(a5xx_compute_fill_id_packet_golden) {
    Fixture f;
    CHECK(f.encode());
    CHECK(f.why.empty());
    const std::vector<Packet> golden = {
        {0x0e91, 0, {0, 0, 0, 0, 0x12}}, {0, 0x26, {}},
        {0xe580, 0, {0}}, {0xe784, 0, {0x884}}, {0xe5f0, 0, {0x100e}},
        {0xe790, 0, {1}}, {0xe796, 0, {3}}, {0xe589, 0, {1}},
        {0xe7dc, 0, {1, 1}}, {0xe5f3, 0, {0, 0x23, 0, 0, 0, 0}},
        {0xe78a, 0, {0x1f00000}}, {0xe7b7, 0, {0xfcfcc0, 1}},
        {0, 0x30, {0x760000, 0, 0x23}}, {0xe751, 0, {0}},
        {0, 0x30, {0x7c0000, 1, 0, 0x10000000, 0}},
        {0, 0x30, {0x7c0000, 2, 0, 0x100044, 0x12}}, {0, 0x26, {}},
        {0, 0x30, {0x740000, 1, 0, 0x1234, 0, 0, 0}},
        {0xe7b0, 0, {0xfd, 1024, 0, 1, 0, 1, 0}},
        {0xe7b9, 0, {1, 1, 1}}, {0, 0x33, {0, 16, 1, 1}},
        {0, 0x46, {4, 4, 0x34, 0}}, {0, 0x26, {}}
    };
    CHECK(packets(f.stream) == golden);
    CHECK_EQ(f.memory->submissions, 0u);
    // No CPU "fill" hides the absence of shader execution.
    const auto* bytes = static_cast<const uint8_t*>(f.buffer->contents());
    for (size_t i = 0; i < f.buffer->length(); ++i) CHECK_EQ(bytes[i], 0xa5);
}

TEST(a5xx_compute_preload_threshold_and_zero_resources) {
    for (uint32_t units : {1u, 32u, 33u, 65u}) {
        Fixture f;
        std::vector<uint32_t> binary(units * 32);
        f.k.words = binary.data();
        f.k.numWords = uint32_t(binary.size());
        f.k.instrlen = units;
        f.program.sizeBytes = binary.size() * 4;
        f.k.numBufs = 0;
        f.k.numImmediates = 0;
        f.k.constlen = 0;
        f.k.immediates = nullptr;
        f.k.doubleThreadsize = false;
        f.k.maxHalfReg = 2;
        f.k.branchstackHw = 127;
        f.k.localIdRegid = f.k.workGroupIdRegid = kNoRegid;
        f.b.buffers.clear();
        CHECK(f.encode());
        const auto ps = packets(f.stream);
        const auto shader = loads(ps, 13, 0);
        CHECK_EQ(shader.size(), units <= 32 ? 1u : 0u);
        CHECK(registerData(ps, 0xe7dc) == (std::vector<uint32_t>{0, units <= 32 ? units : 0}));
        const auto ctrl = registerData(ps, 0xe5f0);
        CHECK_EQ(ctrl.at(0), 0xfe001032u | (units <= 32 ? 4u : 0u));
        CHECK_EQ(loads(ps, 15, 1).size(), 0u);
        CHECK_EQ(registerData(ps, 0xe7b7).at(0), 0xfcfcfcfcu);
    }
}

TEST(a5xx_compute_ssbo_sizes_are_bytes_and_addresses_are_64bit) {
    Fixture f;
    f.k.numBufs = 4;
    const uint32_t sizes[] = {4, 65532, 65536, 131076};
    f.b.buffers.resize(4, f.b.buffers[0]);
    for (unsigned i = 0; i < 4; ++i) f.k.bufs[i] = {sizes[i], kNoConstRegid};
    CHECK(f.encode());
    const auto ps = packets(f.stream);
    const auto size = loads(ps, 15, 1).at(0).data;
    CHECK(size == (std::vector<uint32_t>{
        0x13c0000, 1, 0, 0x40000, 0, 0xfffc0000, 0, 0, 1, 0x40000, 2}));
    const auto ptr = loads(ps, 15, 2).at(0).data;
    CHECK_EQ(ptr.size(), 11u);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK_EQ(ptr[3 + i * 2], 0x100044u);
        CHECK_EQ(ptr[4 + i * 2], 0x12u);
    }
}

TEST(a5xx_compute_constants_offsets_patches_and_padding) {
    Fixture f;
    f.k.constlen = 8;
    f.k.immediatesOffsetVec4 = 2;
    f.k.numImmediates = 7;
    f.k.bufs[0].constRegid = 8;
    f.k.numwgConstRegid = 10;
    std::array<uint32_t, 7> replacement{{1, 2, 3, 4, 5, 6, 7}};
    const auto original = f.immediates;
    CHECK(f.encode({replacement.data(), uint32_t(replacement.size())}));
    const auto c = loads(packets(f.stream), 13, 1).at(0).data;
    CHECK(c == (std::vector<uint32_t>{0xb40002, 1, 0, 0x100044, 0x12, 16, 1, 1, 6, 7, 0}));
    CHECK(f.immediates == original);
    CHECK_EQ(replacement[0], 1u);
}

TEST(a5xx_compute_appends_and_null_reason_is_valid) {
    Fixture f;
    f.stream.nop(2);
    const auto prefix = f.stream.words();
    CHECK(ca::encode(f.stream, f.k, f.grid, f.b, f.program, f.flush));
    CHECK(std::equal(prefix.begin(), prefix.end(), f.stream.words().begin()));
    CHECK_EQ(f.memory->submissions, 0u);
}

TEST(a5xx_compute_refuses_uncovered_features_atomically) {
    const std::vector<std::function<void(Fixture&)>> changes = {
        [](auto& f) { f.k.numUbos = 1; }, [](auto& f) { f.k.numSsbos = 1; },
        [](auto& f) { f.k.numIbos = 1; }, [](auto& f) { f.k.numUboRanges = 33; },
        [](auto& f) { f.k.constantDataSize = 4; }, [](auto& f) { f.k.constantDataOffset = 128; },
        [](auto& f) { f.k.constantDataUbo = 1; }, [](auto& f) { f.k.driverParamsOffsetVec4 = 0; },
        [](auto& f) { f.k.localSizeVariable = true; }, [](auto& f) { f.k.driverParamsOffsetVec4 = -2; }
    };
    for (const auto& change : changes) {
        Fixture f;
        change(f);
        f.rejected("layout");
    }
    for (unsigned feature = 0; feature < 13; ++feature) {
        Fixture f;
        switch (feature) {
        case 0: f.k.numSamp = 1; break;
        case 1: f.k.numTextureSlots = 1; break;
        case 2: f.k.samplerMask = 1; break;
        case 3: f.b.nullMask = 1; break;
        case 4: f.k.textures[15].kind = kTexImage; break;
        case 5: f.k.textures[0].fbTarget = 1; break;
        case 6: f.k.sharedSize = 1; break;
        case 7: f.b.sharedBytes = 1; break;
        case 8: f.k.pvtmemSize = 512; break;
        case 9: f.b.privateMemory.perSp = 4096; break;
        case 10: f.b.privateMemory.address = 0x1000; break;
        case 11: f.k.mergedRegs = true; break;
        case 12: f.k.earlyPreamble = true; break;
        }
        f.rejected("A5xx:");
    }
}

TEST(a5xx_compute_refuses_bad_metadata_ranges_and_patches) {
    using Change = std::function<void(Fixture&)>;
    const std::vector<Change> changes = {
        [](auto& f) { f.program.gpuId = 0; }, [](auto& f) { f.program.gpuId = 635; },
        [](auto& f) { f.program.address = 0; }, [](auto& f) { f.program.address += 4; },
        [](auto& f) { f.program.address = UINT64_MAX - 127; }, [](auto& f) { f.program.sizeBytes = 128; },
        [](auto& f) { f.flush = 0; }, [](auto& f) { f.flush += 1; },
        [](auto& f) { f.flush = UINT64_MAX - 3; }, [](auto& f) { f.flush = f.program.address; },
        [](auto& f) { f.flush = f.buffer->gpuAddress(); }, [](auto& f) { f.program.address = f.buffer->gpuAddress(); },
        [](auto& f) { f.k.words = nullptr; }, [](auto& f) { f.k.numWords = 0; },
        [](auto& f) { f.k.instrlen = 0; }, [](auto& f) { f.k.instrlen = UINT32_MAX; },
        [](auto& f) { f.k.maxReg = -2; }, [](auto& f) { f.k.maxHalfReg = -2; },
        [](auto& f) { f.k.maxReg = INT32_MAX; }, [](auto& f) { f.k.maxHalfReg = INT32_MAX; },
        [](auto& f) { f.k.maxReg = 24; }, [](auto& f) { f.k.branchstackHw = 128; },
        [](auto& f) { f.k.localIdRegid = 0xff; }, [](auto& f) { f.k.localIdRegid = 1; },
        [](auto& f) { f.k.workGroupIdRegid = 0xe0; }, [](auto& f) { f.k.workGroupIdRegid = 16; },
        [](auto& f) { f.k.numBufs = 5; }, [](auto& f) { f.k.numBufs = UINT32_MAX; },
        [](auto& f) { f.b.buffers.clear(); }, [](auto& f) { f.b.buffers[0].buffer = nullptr; },
        [](auto& f) { f.b.buffers[0].offset = 1; }, [](auto& f) { f.b.buffers[0].offset = UINT64_MAX - 3; },
        [](auto& f) { f.b.buffers[0].offset = f.buffer->length(); },
        [](auto& f) { f.k.bufs[0].sizeBytes = 0; }, [](auto& f) { f.k.bufs[0].sizeBytes = 3; },
        [](auto& f) { f.k.bufs[0].sizeBytes = 262144; }, [](auto& f) { f.k.constlen = 3; },
        [](auto& f) { f.k.constlen = 516; }, [](auto& f) { f.k.numImmediates = UINT32_MAX; },
        [](auto& f) { f.k.immediatesOffsetVec4 = 4; }, [](auto& f) { f.k.immediates = nullptr; },
        [](auto& f) { f.k.bufs[0].constRegid = 3; }, [](auto& f) { f.k.numwgConstRegid = 2; },
        [](auto& f) { f.k.bufs[0].constRegid = 0; f.k.numwgConstRegid = 1; },
        [](auto& f) { f.grid.workDim = 0; }, [](auto& f) { f.grid.workDim = 4; },
        [](auto& f) { f.grid.groups[0] = 0; }, [](auto& f) { f.grid.groups[0] = 65536; },
        [](auto& f) { f.grid.local[0] = 0; }, [](auto& f) { f.grid.local[0] = 32; },
        [](auto& f) { f.grid.local[2] = f.k.localSize[2] = 65; },
        [](auto& f) { f.grid.groups[1] = 2; },
        [](auto& f) { f.grid.local[0] = f.k.localSize[0] = 1024; f.grid.local[1] = f.k.localSize[1] = 2; f.grid.workDim = 3; }
    };
    for (const auto& change : changes) {
        Fixture f;
        change(f);
        f.rejected("A5xx:");
    }
}

TEST(a5xx_compute_override_and_patch_failures_are_atomic) {
    Fixture f;
    f.stream.nop(1);
    const auto old = f.stream.words();
    const uint32_t override[] = {1, 2, 3, 4};
    CHECK(!f.encode({override, 3}));
    CHECK(f.why.find("override") != std::string::npos);
    CHECK(f.stream.words() == old);
    CHECK(!f.encode({nullptr, 4}));
    CHECK(f.stream.words() == old);
    f.k.immediatesOffsetVec4 = 1;
    f.k.constlen = 8;
    f.k.bufs[0].constRegid = 0; // below the uploaded range, not relative index zero
    CHECK(!f.encode());
    CHECK(f.why.find("patch") != std::string::npos);
    CHECK(f.stream.words() == old);
}

TEST(a5xx_compute_grid_boundary_sweep) {
    Fixture f;
    f.grid.workDim = 3;
    unsigned accepted = 0, refused = 0;
    for (uint32_t x : {1u, 2u, 32u, 64u, 1024u, 1025u})
        for (uint32_t y : {1u, 2u, 16u, 1024u})
            for (uint32_t z : {1u, 2u, 64u, 65u})
                for (uint32_t groups : {0u, 1u, 65535u, 65536u}) {
                    f.grid.local[0] = f.k.localSize[0] = x;
                    f.grid.local[1] = f.k.localSize[1] = y;
                    f.grid.local[2] = f.k.localSize[2] = z;
                    f.grid.groups[0] = groups;
                    f.stream.clear();
                    const bool expected = x <= 1024 && z <= 64 && uint64_t(x) * y * z <= 1024 &&
                                          groups >= 1 && groups <= 65535;
                    CHECK_EQ(f.encode(), expected);
                    if (expected) {
                        ++accepted;
                        const auto nd = registerData(packets(f.stream), 0xe7b0);
                        CHECK_EQ(nd.at(1), x * groups);
                        CHECK_EQ(nd.at(3), y);
                        CHECK_EQ(nd.at(5), z);
                    } else {
                        ++refused;
                        CHECK_EQ(f.stream.size(), 0u);
                    }
                }
    CHECK_EQ(accepted + refused, 384u);
    CHECK_EQ(f.memory->submissions, 0u);
}

namespace {
void airFillId(Fixture& f) {
    // Layout verified in compiler/a530-smoke.log:42-57. This test intentionally
    // still uses synthetic words; it validates packets, never their execution.
    f.k.instrlen = 2;
    f.k.constlen = 12;
    f.k.maxReg = 2;
    f.k.localSizeVariable = true;
    f.k.localSize[0] = f.k.localSize[1] = f.k.localSize[2] = 0;
    f.k.numBufs = 0;
    f.k.numSsbos = f.k.numIbos = 1;
    f.k.driverParamsOffsetVec4 = 0;
    f.k.immediatesOffsetVec4 = 8;
    f.k.numImmediates = 1;
    f.immediates[0] = 0x9e3779b1;
    f.k.numUboRanges = 1;
    f.k.uboRanges[0] = {1, 4, 0, 16};
    f.k.constantDataUbo = 1; // unused reserved tag emitted by actual compiler, size=0
    f.b.buffers[0].offset = 0;
    f.b.buffers.push_back({f.buffer.get(), 0x10000}); // caller-padded constant range
}
}

TEST(a5xx_compute_air_fill_id_pushed_ubo_and_driver_params) {
    Fixture f;
    airFillId(f);
    const ca::AirLayout layout{12, 64, true}; // actual smoke variant, not A530 threadsize_base
    CHECK(ca::encodeAir(f.stream, f.k, f.grid, f.b, f.program, f.flush, layout, &f.why));
    const auto ps = packets(f.stream);
    const auto cs = loads(ps, 13, 1);
    CHECK_EQ(cs.size(), 3u);
    CHECK(cs.at(0).data == (std::vector<uint32_t>{0x1360004, 0x110001, 0x12}));
    CHECK(cs.at(1).data == (std::vector<uint32_t>{0x740008, 1, 0, 0x9e3779b1, 0, 0, 0}));
    CHECK(cs.at(2).data == (std::vector<uint32_t>{
        0x1340000, 1, 0, 16, 1, 1, 1, 0, 0, 0, 64, 64, 1, 1, 6, 0, 0, 0, 0}));
    CHECK(registerData(ps, 0xe7dc) == (std::vector<uint32_t>{3, 2}));
    CHECK_EQ(f.memory->submissions, 0u);
}

TEST(a5xx_compute_air_refusals_preserve_stream) {
    for (unsigned i = 0; i < 19; ++i) {
        Fixture f;
        airFillId(f);
        ca::AirLayout layout{12, 128, true};
        switch (i) {
        case 0: layout.boundUboPushes = false; break;
        case 1: layout.numDriverParams = 0; break;
        case 2: layout.numDriverParams = 17; break;
        case 3: layout.subgroupSize = 0; break;
        case 4: layout.subgroupSize = 256; break;
        case 5: f.k.numUbos = 1; break; // UBO_PTRS allocation is missing in Kernel
        case 6: f.k.numIbos = 2; break;
        case 7: f.k.numSsbos = 25; break;
        case 8: f.k.uboRanges[0].offsetVec4 = 0; break;
        case 9: f.k.uboRanges[0].offsetVec4 = 8; break;
        case 10: f.k.uboRanges[0].sizeDwords = 4; break;
        case 11: f.k.uboRanges[0].startDword = 1; break;
        case 12: f.k.uboRanges[0].block = UINT32_MAX; break;
        case 13: f.k.numUboRanges = 33; break;
        case 14: f.b.buffers[1].offset = f.buffer->length() - 4; break;
        case 15: f.k.driverParamsOffsetVec4 = -1; break;
        case 16: f.k.numwgConstRegid = 32; break;
        case 17: f.b.buffers[1].offset = UINT64_MAX - 3; f.k.uboRanges[0].startDword = 16; break;
        case 18: f.k.uboRanges[0].block = 0; break; // reserved constant-data tag must not be pushed
        }
        f.stream.nop(1);
        const auto old = f.stream.words();
        CHECK(!ca::encodeAir(f.stream, f.k, f.grid, f.b, f.program, f.flush, layout, &f.why));
        CHECK(!f.why.empty());
        CHECK(f.stream.words() == old);
        CHECK_EQ(f.memory->submissions, 0u);
    }
}

TEST(a5xx_compute_air_small_constant_requires_real_padding) {
    Fixture f;
    airFillId(f);
    int error = -1;
    auto bo = f.gpu.newBo(4, 0, &error);
    CHECK_EQ(error, 0);
    Buffer onlyFour(std::move(bo), 4); // BO allocation padding is NOT Buffer::length
    f.b.buffers[1] = {&onlyFour, 0};
    CHECK(!ca::encodeAir(f.stream, f.k, f.grid, f.b, f.program, f.flush, {12, 128, true}, &f.why));
    CHECK(f.why.find("padded") != std::string::npos);
    CHECK_EQ(f.stream.size(), 0u);
}

TEST(a5xx_compute_air_max_ssbo_slots_and_pushed_range) {
    Fixture f;
    airFillId(f);
    f.k.numSsbos = f.k.numIbos = 24;
    f.b.buffers.resize(24, {f.buffer.get(), 0});
    f.k.uboRanges[0] = {1, 12, 0, 16};
    f.k.constlen = 16;
    CHECK(ca::encodeAir(f.stream, f.k, f.grid, f.b, f.program, f.flush, {12, 64, true}, &f.why));
    const auto ps = packets(f.stream);
    const auto ss = loads(ps, 15, 1).at(0).data;
    CHECK_EQ(ss.size(), 51u);
    CHECK_EQ(ss.at(0) >> 22, 24u);
    CHECK_EQ(f.memory->submissions, 0u);
}
