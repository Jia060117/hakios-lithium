// SPDX-License-Identifier: MIT
// Host memory + packet tests; no transport submission or shader interpretation.
#include "hakios-metal/air_dispatch_a5xx.hpp"
#include "hakios-metal/command.hpp"
#include "test.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <type_traits>

using namespace hakios::metal;
namespace ad = hakios::metal::air_dispatch_a5xx;

namespace {
struct Stats {
    unsigned allocations = 0, snapshots = 0, unexpected = 0, live = 0;
    std::vector<std::pair<uint64_t, uint64_t>> reads;
};

class HostTransport final : public Transport {
public:
    explicit HostTransport(std::shared_ptr<Stats> stats) : stats_(std::move(stats)) {}
    int call(uint32_t request, void* data, size_t) override {
        if (request == kGemNew) {
            auto& n = *static_cast<MsmGemNew*>(data);
            n.handle = ++last_;
            bytes_[n.handle].resize(size_t(n.size));
            ++stats_->live;
            return 0;
        }
        if (request == kGemInfo) {
            auto& n = *static_cast<MsmGemInfo*>(data);
            n.value = 0x1500000000ull + uint64_t(n.handle) * 0x100000;
            return 0;
        }
        if (request == kGemClose) {
            stats_->live -= unsigned(bytes_.erase(static_cast<DrmGemClose*>(data)->handle));
            return 0;
        }
        ++stats_->unexpected;
        return ENOTSUP;
    }
    void* map(uint32_t handle, uint64_t) override { return bytes_.at(handle).data(); }
    void unmap(uint32_t, void*) override {}
    int64_t nowNs() override { return 0; }
private:
    std::shared_ptr<Stats> stats_;
    uint32_t last_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> bytes_;
};

class HostMemory final : public ad::Memory {
public:
    std::shared_ptr<Stats> stats = std::make_shared<Stats>();
    std::shared_ptr<Gpu> gpu = std::make_shared<Gpu>(std::make_unique<HostTransport>(stats));
    int failAllocation = -1;
    bool failSnapshot = false, noOwner = false;
    std::shared_ptr<Buffer> badAllocation;
    std::shared_ptr<const void> backingOwner() const override {
        return noOwner ? nullptr : gpu;
    }
    std::shared_ptr<Buffer> allocate(uint64_t size, std::string* why) override {
        const unsigned call = stats->allocations++;
        if (int(call) == failAllocation) { *why = "injected allocation failure"; return nullptr; }
        if (badAllocation) return badAllocation;
        int error = -1;
        auto bo = gpu->newBo(size, 0, &error);
        if (!bo || error) { *why = "host memory allocation error"; return nullptr; }
        // Poison the entire physical mapping to expose any accidental padding read.
        std::memset(bo->cpu(), 0xbd, size_t(bo->size()));
        return std::make_shared<Buffer>(std::move(bo), size);
    }
    bool snapshot(const Buffer& source, uint64_t offset, uint64_t size,
                  void* destination, std::string* why) override {
        ++stats->snapshots;
        stats->reads.push_back({offset, size});
        if (failSnapshot) { *why = "injected CPU-read refusal"; return false; }
        if (!destination || offset > source.length() || size > source.length() - offset) {
            *why = "bounded read failure";
            return false;
        }
        std::memcpy(destination, static_cast<const uint8_t*>(source.contents()) + offset, size_t(size));
        return true;
    }
    ad::Argument argument(const std::shared_ptr<Buffer>& buffer, uint64_t bytes, uint64_t offset = 0) {
        return {gpu, buffer, offset, bytes};
    }
};

template<class T, class = void> struct HasFields : std::false_type {};
template<class T> struct HasFields<T, std::void_t<decltype(std::declval<T>().compiledChipId),
    decltype(std::declval<T>().driverParamsDwords), decltype(std::declval<T>().subgroupSize),
    decltype(std::declval<T>().a5xxBoundUboPushes)>> : std::true_type {};
template<class T> void setFields(T& k, const ad::Metadata& m) {
    if constexpr (HasFields<T>::value) {
        k.compiledChipId = m.compiledChipId;
        k.driverParamsDwords = m.driverParamsDwords;
        k.subgroupSize = m.subgroupSize;
        k.a5xxBoundUboPushes = m.a5xxBoundUboPushes;
    }
}

Resource resource(Resource::Kind kind, uint32_t index, uint32_t size = 0) {
    Resource r{};
    r.kind = kind;
    r.index = index;
    r.sizeBytes = size;
    r.read = true;
    r.write = r.irWrite = kind == Resource::kDeviceBuffer;
    return r;
}

struct Fixture {
    HostMemory memory;
    std::shared_ptr<Buffer> output, constant;
    CompiledKernel c;
    ad::Metadata metadata{0x05030004, 12, 64, true};
    compute_a5xx::Grid grid{{16, 1, 1}, {64, 1, 1}, 1};
    std::vector<ad::Argument> args;
    ad::Prepared prepared;
    std::string why;
    Fixture() {
        output = memory.allocate(4096, &why);
        constant = memory.allocate(4, &why);
        const uint32_t value = 0x1234;
        std::memcpy(constant->contents(), &value, 4);
        c.words.resize(64, 0); // synthetic packet fixture, NOT shader code
        c.immediates = {0x9e3779b1};
        c.resources = {resource(Resource::kDeviceBuffer, 0), resource(Resource::kConstantBuffer, 1, 4)};
        auto& k = c.kernel;
        k.words = c.words.data(); k.numWords = 64; k.instrlen = 2; k.constlen = 12;
        k.maxReg = 2; k.maxHalfReg = -1;
        k.localSizeVariable = true;
        k.localIdRegid = 0; k.workGroupIdRegid = 0xc0;
        k.immediates = c.immediates.data(); k.numImmediates = 1; k.immediatesOffsetVec4 = 8;
        k.driverParamsOffsetVec4 = 0;
        k.numSsbos = k.numIbos = 1;
        k.numUboRanges = 1; k.uboRanges[0] = {1, 4, 0, 16};
        k.numwgConstRegid = kNoConstRegid;
        setFields(k, metadata);
        args = {memory.argument(output, 4096), memory.argument(constant, 4)};
        memory.stats->allocations = 0;
    }
    bool prepare() {
        return ad::prepareWithMetadata(c, metadata, grid, args, memory, &prepared, &why);
    }
    void reject() {
        prepared.stream.nop(2);
        const auto old = prepared.stream.words();
        CHECK(!prepare());
        CHECK(!why.empty());
        CHECK(prepared.stream.words() == old);
        CHECK_EQ(memory.stats->allocations, 0u);
        CHECK_EQ(memory.stats->snapshots, 0u);
        CHECK_EQ(memory.stats->unexpected, 0u);
    }
};

void checkPrepared(const ad::Prepared& p) {
    CHECK(p.program != nullptr);
    CHECK(p.scratch != nullptr);
    CHECK_EQ(p.stagedUbos.size(), 1u);
    CHECK_EQ(p.stagedUbos.at(0).buffer->length(), 64u);
    CHECK_EQ(p.stagedUbos.at(0).logicalBytes, 4u);
    const auto* bytes = static_cast<const uint8_t*>(p.stagedUbos.at(0).buffer->contents());
    uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    CHECK_EQ(value, 0x1234u);
    for (size_t i = 4; i < 64; ++i) CHECK_EQ(bytes[i], 0u);
    unsigned exec = 0, push = 0, programObjects = 0, ssboAddresses = 0, flushes = 0;
    for (size_t at = 0; at < p.stream.size();) {
        auto h = pm4::decode(p.stream.words()[at]);
        CHECK(h.kind != pm4::Header::kBad);
        if (h.kind == pm4::Header::kBad || h.count > p.stream.size() - at - 1) {
            CHECK(false); break;
        }
        if (h.kind == pm4::Header::kCommand) {
            CHECK(h.opcode == 0x26 || h.opcode == 0x30 || h.opcode == 0x33 || h.opcode == 0x46);
            exec += h.opcode == 0x33;
            if (h.opcode == 0x30 && h.count == 3 && (p.stream.words()[at + 2] & 3) == 1) {
                ++push;
                const uint64_t address = uint64_t(p.stream.words()[at + 2] & ~3u) |
                                         uint64_t(p.stream.words()[at + 3]) << 32;
                CHECK_EQ(address, p.stagedUbos.at(0).buffer->gpuAddress());
                CHECK_EQ(p.stream.words()[at + 1] & 0x3fff, 4u);
            }
            if (h.opcode == 0x30 && h.count == 5 &&
                ((p.stream.words()[at + 1] >> 18) & 15) == 15 &&
                (p.stream.words()[at + 2] & 3) == 2) {
                ++ssboAddresses;
                const uint64_t address = uint64_t(p.stream.words()[at + 4]) |
                                         uint64_t(p.stream.words()[at + 5]) << 32;
                CHECK_EQ(address, p.residents.at(0).buffer->gpuAddress());
                CHECK(p.residents.at(0).write);
            }
            if (h.opcode == 0x46 && h.count == 4) {
                ++flushes;
                const uint64_t address = uint64_t(p.stream.words()[at + 2]) |
                                         uint64_t(p.stream.words()[at + 3]) << 32;
                CHECK_EQ(address, p.scratch->gpuAddress());
                CHECK_EQ(p.stream.words()[at + 1], 4u);
            }
        } else if (h.reg == 0xe5f3 && h.count == 6) {
            ++programObjects;
            const uint64_t address = uint64_t(p.stream.words()[at + 1]) |
                                     uint64_t(p.stream.words()[at + 2]) << 32;
            CHECK_EQ(address, p.program->gpuAddress());
        }
        at += h.count + 1;
    }
    CHECK_EQ(exec, 1u);
    CHECK_EQ(push, 1u);
    CHECK_EQ(programObjects, 1u);
    CHECK_EQ(ssboAddresses, 1u);
    CHECK_EQ(flushes, 1u);
    CHECK_EQ(p.stream.size(), 109u);
    CHECK_EQ(p.residents.size(), 4u);
    for (const auto& r : p.residents) CHECK(r.backingOwner != nullptr);
    auto uses = p.boUses();
    CHECK_EQ(uses.size(), 4u);
    CHECK_EQ(std::count_if(uses.begin(), uses.end(), [](auto u) { return u.write; }), 2);
}
} // namespace

TEST(a5xx_air_dispatch_zero_padding_owned_program_and_no_submit) {
    Fixture f;
    CHECK(f.prepare());
    CHECK(f.why.empty());
    checkPrepared(f.prepared);
    CHECK_EQ(f.memory.stats->allocations, 3u);
    CHECK_EQ(f.memory.stats->snapshots, 1u);
    CHECK(f.memory.stats->reads == (std::vector<std::pair<uint64_t, uint64_t>>{{0, 4}}));
    CHECK(std::memcmp(f.prepared.program->contents(), f.c.words.data(), 256) == 0);
    for (size_t i = 0; i < 4096; ++i)
        CHECK_EQ(static_cast<const uint8_t*>(f.output->contents())[i], 0xbd);
    CHECK_EQ(f.memory.stats->unexpected, 0u);
}

TEST(a5xx_air_dispatch_schema_requires_explicit_compiler_metadata) {
    Fixture f;
    ad::Metadata read;
    const bool extracted = ad::metadataFromKernel(f.c.kernel, &read, &f.why);
    CHECK_EQ(extracted, HasFields<Kernel>::value);
    if (extracted) {
        CHECK_EQ(read.compiledChipId, 0x05030004u);
        CHECK(ad::prepare(f.c, f.grid, f.args, f.memory, &f.prepared, &f.why));
        checkPrepared(f.prepared);
    } else {
        CHECK(f.why.find("Kernel lacks") != std::string::npos);
        CHECK(!ad::prepare(f.c, f.grid, f.args, f.memory, &f.prepared, &f.why));
        CHECK_EQ(f.memory.stats->allocations, 0u);
    }
    CHECK(!ad::metadataFromKernel(f.c.kernel, nullptr, &f.why));
    CHECK_EQ(f.memory.stats->unexpected, 0u);
}

TEST(a5xx_air_dispatch_bounded_parameters_reject_before_allocation) {
    using Change = std::function<void(Fixture&)>;
    const std::vector<Change> changes = {
        [](auto& f) { f.metadata.compiledChipId = 0; },
        [](auto& f) { f.metadata.compiledChipId = 0x06030500; },
        [](auto& f) { f.metadata.a5xxBoundUboPushes = false; },
        [](auto& f) { f.metadata.driverParamsDwords = 0; },
        [](auto& f) { f.metadata.driverParamsDwords = 17; },
        [](auto& f) { f.metadata.subgroupSize = 0; },
        [](auto& f) { f.args.resize(33); }, [](auto& f) { f.c.resources.resize(33); },
        [](auto& f) { f.c.words.clear(); }, [](auto& f) { f.c.kernel.numWords = 63; },
        [](auto& f) { f.c.kernel.numImmediates = UINT32_MAX; },
        [](auto& f) { f.c.kernel.instrlen = UINT32_MAX; },
        [](auto& f) { f.c.hasThreadgroupArg = true; },
        [](auto& f) { f.c.kernel.sharedSize = 64; },
        [](auto& f) { f.c.kernel.pvtmemSize = 512; },
        [](auto& f) { f.c.kernel.numUbos = 1; },
        [](auto& f) { f.c.kernel.numUboRanges = 33; },
        [](auto& f) { f.c.kernel.numSsbos = 25; },
        [](auto& f) { f.c.kernel.mergedRegs = true; },
        [](auto& f) { f.c.kernel.constantDataSize = 64; },
        [](auto& f) { f.c.kernel.textures[0].kind = kTexImage; },
        [](auto& f) { f.c.kernel.constlen = 513; },
        [](auto& f) { f.c.kernel.driverParamsOffsetVec4 = 4; },
        [](auto& f) { f.c.resources[1].index = 0; },
        [](auto& f) { f.c.resources[1].index = 32; },
        [](auto& f) { f.c.resources[1].argbufSet = 0; },
        [](auto& f) { f.c.resources[1].kind = Resource::kThreadgroupBuffer; },
        [](auto& f) { f.c.resources[1].sizeBytes = 8; },
        [](auto& f) { f.c.resources[1].write = true; },
        [](auto& f) { f.args[1].buffer.reset(); },
        [](auto& f) { f.args[0].backingOwner.reset(); },
        [](auto& f) { f.args[1].offset = UINT64_MAX; },
        [](auto& f) { f.args[1].offset = 4; },
        [](auto& f) { f.args[1].sizeBytes = UINT64_MAX; },
        [](auto& f) { f.args[1].sizeBytes = 0; },
        [](auto& f) { f.args[0].sizeBytes = 4092; }, // unrepresentable SSBO subrange
        [](auto& f) { f.args[0].offset = 1; f.args[0].sizeBytes = 4095; },
        [](auto& f) { f.c.kernel.uboRanges[0].sizeDwords = 4; },
        [](auto& f) { f.c.kernel.uboRanges[0].startDword = UINT32_MAX - 15; },
        [](auto& f) { f.c.kernel.uboRanges[0].offsetVec4 = 0; },
        [](auto& f) { f.c.kernel.uboRanges[0].block = UINT32_MAX; },
        [](auto& f) { f.grid.local[0] = 0; },
        [](auto& f) { f.grid.groups[0] = 65536; },
        [](auto& f) { f.grid.workDim = 0; },
        [](auto& f) { f.grid.local[2] = 65; }
    };
    for (const auto& change : changes) {
        Fixture f;
        change(f);
        f.reject();
    }
}

TEST(a5xx_air_dispatch_failures_unwind_allocations_preserve_existing_result) {
    for (int fail = 0; fail < 4; ++fail) {
        Fixture f;
        f.memory.failAllocation = fail < 3 ? fail : -1;
        f.memory.failSnapshot = fail == 3;
        f.prepared.stream.nop(2);
        const auto old = f.prepared.stream.words();
        CHECK(!f.prepare());
        CHECK(f.prepared.stream.words() == old);
        CHECK(!f.why.empty());
        CHECK_EQ(f.memory.stats->live, 2u);
        CHECK_EQ(f.memory.stats->unexpected, 0u);
    }
}

TEST(a5xx_air_dispatch_allocator_alias_and_missing_go_owner_refused) {
    Fixture f;
    f.output = f.memory.allocate(256, &f.why); // match program size to reach alias check
    f.args[0] = f.memory.argument(f.output, 256);
    f.memory.badAllocation = f.output;
    CHECK(!f.prepare());
    CHECK_EQ(f.memory.stats->snapshots, 0u);
    CHECK_EQ(f.memory.stats->live, 2u);
    CHECK_EQ(static_cast<const uint8_t*>(f.output->contents())[0], 0xbd);
    Fixture g;
    g.memory.noOwner = true;
    CHECK(!g.prepare());
    CHECK_EQ(g.memory.stats->allocations, 0u);
}

TEST(a5xx_air_dispatch_failed_reprepare_keeps_previous_owners_and_bytes) {
    Fixture f;
    CHECK(f.prepare());
    auto previous = f.prepared.program;
    const auto words = f.prepared.stream.words();
    f.memory.failAllocation = int(f.memory.stats->allocations) + 1; // new UBO allocation
    CHECK(!f.prepare());
    CHECK(f.prepared.program == previous);
    CHECK(f.prepared.stream.words() == words);
    checkPrepared(f.prepared);
    CHECK_EQ(f.memory.stats->live, 5u);
    CHECK_EQ(f.memory.stats->unexpected, 0u);
}

TEST(a5xx_air_dispatch_context_and_bo_survive_provider_and_caller) {
    ad::Prepared result;
    std::weak_ptr<Gpu> context;
    std::weak_ptr<const Buffer> ssbo;
    std::shared_ptr<Stats> stats;
    {
        Fixture f;
        CHECK(f.prepare());
        context = f.memory.gpu;
        ssbo = f.output;
        stats = f.memory.stats;
        result = std::move(f.prepared);
    }
    CHECK(!context.expired());
    CHECK(!ssbo.expired());
    CHECK_EQ(stats->live, 4u); // original constant was snapshotted, not referenced
    checkPrepared(result);
    ad::Prepared copied = result;
    result = {};
    CHECK(!context.expired());
    CHECK_EQ(stats->live, 4u);
    copied = {}; // copy-swap releases old buffers before their backing Gpu
    CHECK_EQ(stats->live, 0u);
    CHECK(context.expired());
    CHECK(ssbo.expired());
    CHECK_EQ(stats->unexpected, 0u);
}

TEST(a5xx_air_dispatch_stale_kernel_pointers_rebound_from_owned_vectors) {
    Fixture f;
    f.c.kernel.words = reinterpret_cast<const uint32_t*>(uintptr_t(1));
    f.c.kernel.immediates = reinterpret_cast<const uint32_t*>(uintptr_t(1));
    CHECK(f.prepare());
    checkPrepared(f.prepared);
    f.c.words.assign(64, UINT32_MAX);
    CHECK_EQ(static_cast<const uint32_t*>(f.prepared.program->contents())[0], 0u);
}

TEST(a5xx_air_dispatch_replacing_argument_keeps_old_context_through_bo_release) {
    ad::Argument saved;
    std::weak_ptr<Gpu> oldContext, newContext;
    std::shared_ptr<Stats> oldStats, newStats;
    std::string why;
    {
        HostMemory m;
        auto buffer = m.allocate(4, &why);
        saved = m.argument(buffer, 4);
        oldContext = m.gpu;
        oldStats = m.stats;
    }
    CHECK(!oldContext.expired());
    {
        HostMemory m;
        auto buffer = m.allocate(4, &why);
        saved = m.argument(buffer, 4);
        newContext = m.gpu;
        newStats = m.stats;
    }
    CHECK(oldContext.expired());
    CHECK_EQ(oldStats->live, 0u);
    CHECK(!newContext.expired());
    saved = {};
    CHECK(newContext.expired());
    CHECK_EQ(newStats->live, 0u);
    CHECK_EQ(oldStats->unexpected + newStats->unexpected, 0u);
}

TEST(a5xx_air_dispatch_sparse_push_reads_only_authorized_intersections) {
    Fixture f;
    f.constant = f.memory.allocate(132, &f.why);
    std::memset(f.constant->contents(), 0x5a, 132);
    f.args[1] = f.memory.argument(f.constant, 132);
    f.c.resources[1].sizeBytes = 132;
    f.c.kernel.constlen = 16;
    f.c.kernel.immediatesOffsetVec4 = 12;
    f.c.kernel.numUboRanges = 2;
    f.c.kernel.uboRanges[1] = {1, 8, 32, 16};
    f.memory.stats->allocations = 0;
    CHECK(f.prepare());
    CHECK(f.memory.stats->reads == (std::vector<std::pair<uint64_t, uint64_t>>{{0, 64}, {128, 4}}));
    const auto* bytes = static_cast<const uint8_t*>(f.prepared.stagedUbos.at(0).buffer->contents());
    CHECK_EQ(f.prepared.stagedUbos.at(0).buffer->length(), 192u);
    for (unsigned i = 0; i < 192; ++i)
        CHECK_EQ(bytes[i], (i < 64 || (i >= 128 && i < 132)) ? 0x5a : 0);
}

TEST(a5xx_air_dispatch_constant_offset_reads_four_bytes_and_zeros_padding) {
    Fixture f;
    f.constant = f.memory.allocate(132, &f.why);
    std::memset(f.constant->contents(), 0xa7, 132);
    const uint32_t value = 0x1234;
    std::memcpy(static_cast<uint8_t*>(f.constant->contents()) + 128, &value, 4);
    f.args[1] = f.memory.argument(f.constant, 4, 128);
    f.memory.stats->allocations = 0;
    CHECK(f.prepare());
    CHECK(f.memory.stats->reads == (std::vector<std::pair<uint64_t, uint64_t>>{{128, 4}}));
    checkPrepared(f.prepared);
    for (unsigned i = 0; i < 128; ++i)
        CHECK_EQ(static_cast<const uint8_t*>(f.constant->contents())[i], 0xa7);
}

TEST(a5xx_air_dispatch_encoder_refusal_releases_new_bos_keeps_previous_result) {
    Fixture f;
    CHECK(f.prepare());
    auto previous = f.prepared.program;
    const auto words = f.prepared.stream.words();
    const unsigned allocations = f.memory.stats->allocations;
    // Bridge validation accepts the owned layout; encodeAir then refuses the
    // invalid register footprint after program, staging and scratch allocation.
    f.c.kernel.maxReg = 48;
    CHECK(!f.prepare());
    CHECK(f.why.find("register footprint") != std::string::npos);
    CHECK_EQ(f.memory.stats->allocations, allocations + 3);
    CHECK_EQ(f.memory.stats->live, 5u);
    CHECK(f.prepared.program == previous);
    CHECK(f.prepared.stream.words() == words);
    checkPrepared(f.prepared);
    CHECK_EQ(f.memory.stats->unexpected, 0u);
}

#ifdef HAKIOS_A5XX_AIR_REAL_COMPILER
TEST(a5xx_air_dispatch_actual_compileAir_metadata_integration) {
    const char* file = std::getenv("A5XX_AIR_BITCODE");
    CHECK(file != nullptr);
    if (!file) return;
    std::ifstream input(file, std::ios::binary);
    std::vector<char> bitcode((std::istreambuf_iterator<char>(input)), {});
    CHECK(!bitcode.empty());
    std::string why;
    auto actual = compileAir(bitcode.data(), bitcode.size(), &why);
    CHECK(actual != nullptr);
    if (!actual) return;
    CHECK(actual->name == "fill_id");
    CHECK_EQ(actual->words.size(), 64u);
    Fixture f;
    ad::Metadata metadata;
    const bool exported = ad::metadataFromKernel(actual->kernel, &metadata, &why);
    CHECK(exported);
    if (!exported) {
        std::fprintf(stderr, "real compiler metadata refusal: %s\n", why.c_str());
        return;
    }
    CHECK_EQ(metadata.compiledChipId, 0x05030004u);
    CHECK_EQ(metadata.driverParamsDwords, 12u);
    CHECK_EQ(metadata.subgroupSize, 64u);
    CHECK(metadata.a5xxBoundUboPushes);
    CHECK(ad::prepare(*actual, f.grid, f.args, f.memory, &f.prepared, &why));
    if (!f.prepared.program) { std::fprintf(stderr, "real bridge refusal: %s\n", why.c_str()); return; }
    checkPrepared(f.prepared);
    CHECK(std::memcmp(f.prepared.program->contents(), actual->words.data(), 256) == 0);
    CHECK_EQ(f.memory.stats->allocations, 3u);
    CHECK(f.memory.stats->reads == (std::vector<std::pair<uint64_t, uint64_t>>{{0, 4}}));
    CHECK_EQ(f.memory.stats->unexpected, 0u);
    if (const char* directory = std::getenv("A5XX_AIR_ARTIFACT_DIR")) {
        const std::string base(directory);
        std::ofstream program(base + "/actual-fill_id-program.bin", std::ios::binary);
        program.write(static_cast<const char*>(f.prepared.program->contents()), std::streamsize(f.prepared.program->length()));
        program.close(); CHECK(bool(program));
        std::ofstream pm4(base + "/actual-fill_id-pm4.bin", std::ios::binary);
        pm4.write(reinterpret_cast<const char*>(f.prepared.stream.words().data()), std::streamsize(f.prepared.stream.size() * 4));
        pm4.close(); CHECK(bool(pm4));
        const auto& staged = *f.prepared.stagedUbos.at(0).buffer;
        std::ofstream ubo(base + "/actual-fill_id-ubo.bin", std::ios::binary);
        ubo.write(static_cast<const char*>(staged.contents()), std::streamsize(staged.length()));
        ubo.close(); CHECK(bool(ubo));
        std::ofstream data(base + "/actual-fill_id-metadata.json");
        data << "{\n  \"compiledChipId\": " << metadata.compiledChipId
             << ",\n  \"driverParamsDwords\": " << metadata.driverParamsDwords
             << ",\n  \"subgroupSize\": " << metadata.subgroupSize
             << ",\n  \"a5xxBoundUboPushes\": " << (metadata.a5xxBoundUboPushes ? "true" : "false")
             << ",\n  \"numWords\": " << actual->kernel.numWords
             << ",\n  \"instrlen\": " << actual->kernel.instrlen
             << ",\n  \"constlen\": " << actual->kernel.constlen
             << ",\n  \"numUbos\": " << actual->kernel.numUbos
             << ",\n  \"numSsbos\": " << actual->kernel.numSsbos
             << ",\n  \"numUboRanges\": " << actual->kernel.numUboRanges
             << ",\n  \"numImmediates\": " << actual->kernel.numImmediates
             << ",\n  \"driverParamsOffsetVec4\": " << actual->kernel.driverParamsOffsetVec4
             << ",\n  \"streamDwords\": " << f.prepared.stream.size()
             << ",\n  \"residentCount\": " << f.prepared.residents.size()
             << ",\n  \"snapshotCount\": " << f.memory.stats->snapshots
             << ",\n  \"uboLogicalBytes\": 4,\n  \"uboPaddedBytes\": 64,"
                "\n  \"deviceOperations\": 0,\n  \"submissions\": 0,\n  \"shaderExecutionTested\": false\n}\n";
        data.close(); CHECK(bool(data));
    }
    std::printf("REAL compileAir -> prepare -> encodeAir PASS words=64 stream=%zu params=%u subgroup=%u; device=0 submit=0 shader_execution=UNTESTED\n",
                f.prepared.stream.size(), metadata.driverParamsDwords, metadata.subgroupSize);
    // Drop the compiler, source arguments and provider before the result. The
    // real program and snapshotted bytes must still be usable on the host.
    const auto programWords = actual->words;
    actual.reset();
    std::weak_ptr<Gpu> context = f.memory.gpu;
    f.args.clear();
    f.output.reset();
    f.constant.reset();
    f.memory.gpu.reset();
    CHECK(!context.expired());
    CHECK_EQ(f.memory.stats->live, 4u);
    CHECK(std::memcmp(f.prepared.program->contents(), programWords.data(), 256) == 0);
    checkPrepared(f.prepared);
    f.prepared = {};
    CHECK(context.expired());
    CHECK_EQ(f.memory.stats->live, 0u);
    CHECK_EQ(f.memory.stats->unexpected, 0u);
}
#endif
