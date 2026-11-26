// SPDX-License-Identifier: MIT
#include "hakios-metal/air_dispatch_a5xx.hpp"
#include "hakios-metal/command.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace hakios::metal::air_dispatch_a5xx {
namespace {

bool refuse(std::string* why, const char* message) {
    if (why) *why = message;
    return false;
}

template<class T, class = void> struct HasMetadata : std::false_type {};
template<class T> struct HasMetadata<T, std::void_t<
    decltype(std::declval<T>().compiledChipId),
    decltype(std::declval<T>().driverParamsDwords),
    decltype(std::declval<T>().subgroupSize),
    decltype(std::declval<T>().a5xxBoundUboPushes)>> : std::true_type {};

template<class T>
bool readMetadata(const T& kernel, Metadata* out, std::string* why) {
    if (!out) return refuse(why, "A5xx AIR bridge: missing metadata output");
    if constexpr (HasMetadata<T>::value) {
        Metadata next{kernel.compiledChipId, kernel.driverParamsDwords,
                      kernel.subgroupSize, kernel.a5xxBoundUboPushes};
        if (next.compiledChipId != 0x05030004 || !next.a5xxBoundUboPushes)
            return refuse(why, "A5xx AIR bridge: absent A530 compiler provenance or verified UBO push metadata");
        *out = next;
        if (why) why->clear();
        return true;
    } else {
        (void)kernel;
        return refuse(why, "A5xx AIR bridge: Kernel lacks compiledChipId/driverParamsDwords/subgroupSize/a5xxBoundUboPushes; compiler export required");
    }
}

bool range(uint64_t base, uint64_t size) {
    return base && size && size <= UINT64_MAX - base;
}

bool mapped(const Buffer& buffer) {
    return buffer.length() && buffer.length() <= buffer.bo().size() &&
           range(buffer.gpuAddress(), buffer.length());
}

bool overlap(const Buffer& a, const Buffer& b) {
    return a.gpuAddress() < b.gpuAddress() + b.length() &&
           b.gpuAddress() < a.gpuAddress() + a.length();
}

bool validArgument(const Argument& a) {
    return a.backingOwner && a.buffer && mapped(*a.buffer) && a.sizeBytes &&
           a.offset <= a.buffer->length() && a.sizeBytes <= a.buffer->length() - a.offset;
}

struct Plan {
    std::array<const Resource*, kMaxArgumentSlots> resources{};
    std::array<uint64_t, kMaxArgumentSlots> stagingBytes{};
};

bool validate(const CompiledKernel& c, const Metadata& m, const compute_a5xx::Grid& g,
              const std::vector<Argument>& args, Plan* plan, std::string* why) {
    const auto& k = c.kernel;
    if (m.compiledChipId != 0x05030004 || !m.a5xxBoundUboPushes)
        return refuse(why, "A5xx AIR bridge: missing exact A530 compiler/push provenance");
    if (args.size() > kMaxArgumentSlots || c.resources.size() > kMaxArgumentSlots)
        return refuse(why, "A5xx AIR bridge: argument/resource count exceeds 32");
    if (c.words.empty() || c.words.size() > kMaxProgramBytes / 4 ||
        k.numWords != c.words.size() || k.numImmediates != c.immediates.size() ||
        c.immediates.size() > kMaxStagingBytes / 4 ||
        k.instrlen == 0 || uint64_t(k.instrlen) * 32 > c.words.size())
        return refuse(why, "A5xx AIR bridge: bounded owned program/immediate counts disagree");
    if (c.hasThreadgroupArg || c.threadgroupBytes || c.threadgroupArgBase ||
        k.sharedSize || k.pvtmemSize || !c.inlineSamplers.empty())
        return refuse(why, "A5xx AIR bridge: threadgroup/private memory or inline samplers not covered");
    if (k.numBufs || k.numUbos || k.numIbos != k.numSsbos || k.numSsbos > 24 ||
        k.numUboRanges > kMaxUboRanges || k.constantDataSize || k.constantDataOffset ||
        k.numSamp || k.numTextureSlots || k.samplerMask || k.mergedRegs || k.earlyPreamble ||
        k.numwgConstRegid != kNoConstRegid)
        return refuse(why, "A5xx AIR bridge: only buffer-only AIR with fully pushed UBOs is covered");
    for (const auto& t : k.textures)
        if (t.kind || t.dim || t.isArray || t.element || t.bits || t.read ||
            t.write || t.isNull || t.fbTarget)
            return refuse(why, "A5xx AIR bridge: texture/image metadata not covered");
    if (k.constlen > 512 || k.constlen % 4 || k.immediatesOffsetVec4 > k.constlen ||
        (k.numImmediates + 3) / 4 > k.constlen - k.immediatesOffsetVec4 ||
        k.driverParamsOffsetVec4 < -1)
        return refuse(why, "A5xx AIR bridge: constant metadata exceeds A5xx bounds");
    if (k.driverParamsOffsetVec4 >= 0) {
        if (!m.driverParamsDwords || m.driverParamsDwords > 16 ||
            (k.localSizeVariable && m.driverParamsDwords < 12) ||
            (m.subgroupSize != 32 && m.subgroupSize != 64 && m.subgroupSize != 128))
            return refuse(why, "A5xx AIR bridge: driver-param/subgroup metadata absent or invalid");
    } else if (m.driverParamsDwords || k.localSizeVariable) {
        return refuse(why, "A5xx AIR bridge: variable local size requires compiler driver params");
    }
    if (g.workDim < 1 || g.workDim > 3)
        return refuse(why, "A5xx AIR bridge: workDim must be 1..3");
    uint64_t threads = 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (!g.local[i] || g.local[i] > (i == 2 ? 64u : 1024u) ||
            !g.groups[i] || g.groups[i] > 65535 ||
            (!k.localSizeVariable && g.local[i] != k.localSize[i]) ||
            (i >= g.workDim && (g.local[i] != 1 || g.groups[i] != 1)))
            return refuse(why, "A5xx AIR bridge: invalid grid/fixed-local dimensions");
        threads *= g.local[i];
    }
    if (threads > 1024) return refuse(why, "A5xx AIR bridge: more than 1024 local threads");

    for (const auto& r : c.resources) {
        if (r.index >= kMaxArgumentSlots || plan->resources[r.index] ||
            r.argbufSet != ~0u || r.inlineSampler || r.nullTexture ||
            (r.kind != Resource::kDeviceBuffer && r.kind != Resource::kConstantBuffer))
            return refuse(why, "A5xx AIR bridge: duplicate/unsupported resource or argument-buffer layout");
        if (r.index >= args.size() || !validArgument(args[r.index]) ||
            r.sizeBytes > args[r.index].sizeBytes)
            return refuse(why, "A5xx AIR bridge: resource needs its complete authorized argument range");
        if (r.kind == Resource::kConstantBuffer && (r.write || r.irWrite))
            return refuse(why, "A5xx AIR bridge: constant-buffer writes cannot use snapshot staging");
        plan->resources[r.index] = &r;
    }
    for (uint32_t i = 0; i < k.numSsbos; ++i) {
        const auto* r = plan->resources[i];
        if (!r || r->kind != Resource::kDeviceBuffer || !validArgument(args[i]))
            return refuse(why, "A5xx AIR bridge: every SSBO slot needs an owned device argument");
        const auto& a = args[i];
        if (a.sizeBytes > UINT32_MAX || a.offset % 4 || a.sizeBytes % 4 ||
            (a.buffer->gpuAddress() + a.offset) % 4 ||
            a.sizeBytes != a.buffer->length() - a.offset)
            return refuse(why, "A5xx AIR bridge: SSBO view must be word aligned and end at Buffer::length");
    }
    // ir3_const.h:137-182 and fd5_emit_const_bo:59-76. Stage by UBO block,
    // preserving source offsets. Constant destination/count are in vec4 units.
    std::array<bool, 512> occupied{};
    auto claim = [&](uint32_t start, uint32_t count) {
        if (start > k.constlen || count > k.constlen - start) return false;
        for (uint32_t i = start; i < start + count; ++i) if (occupied[i]) return false;
        for (uint32_t i = start; i < start + count; ++i) occupied[i] = true;
        return true;
    };
    if (!claim(k.immediatesOffsetVec4, (k.numImmediates + 3) / 4) ||
        (k.driverParamsOffsetVec4 >= 0 && !claim(uint32_t(k.driverParamsOffsetVec4), 4)))
        return refuse(why, "A5xx AIR bridge: overlapping or out-of-range driver constants");
    uint64_t totalStaging = 0;
    for (uint32_t i = 0; i < k.numUboRanges; ++i) {
        const auto& r = k.uboRanges[i];
        if (r.block >= kMaxArgumentSlots || !plan->resources[r.block] ||
            plan->resources[r.block]->kind != Resource::kConstantBuffer ||
            !r.sizeDwords || r.sizeDwords % 16 || r.startDword % 16 || r.offsetVec4 % 4 ||
            (k.constantDataUbo && r.block == k.constantDataUbo - 1) ||
            !claim(r.offsetVec4, r.sizeDwords / 4))
            return refuse(why, "A5xx AIR bridge: unsupported/overlapping pushed UBO range");
        const uint64_t start = uint64_t(r.startDword) * 4;
        const uint64_t end = start + uint64_t(r.sizeDwords) * 4;
        // Bounds also prevent requests for gigantic zero padding. No narrowing
        // or wrapping arithmetic is used on compiler-provided range offsets.
        if (end > kMaxStagingBytes || start >= args[r.block].sizeBytes)
            return refuse(why, "A5xx AIR bridge: pushed UBO has no logical source bytes or exceeds staging budget");
        auto& bytes = plan->stagingBytes[r.block];
        if (end > bytes) { totalStaging += end - bytes; bytes = end; }
        if (totalStaging > kMaxStagingBytes)
            return refuse(why, "A5xx AIR bridge: total padded UBO staging exceeds 8192 bytes");
    }
    for (uint32_t i = 0; i < kMaxArgumentSlots; ++i) {
        const auto* r = plan->resources[i];
        if (r && ((r->kind == Resource::kDeviceBuffer && i >= k.numSsbos) ||
                  (r->kind == Resource::kConstantBuffer && !plan->stagingBytes[i])))
            return refuse(why, "A5xx AIR bridge: declared resource has no covered shader binding");
    }
    return true;
}

} // namespace

bool metadataFromKernel(const Kernel& kernel, Metadata* out, std::string* why) {
    return readMetadata(kernel, out, why);
}

Argument::Argument(std::shared_ptr<const void> owner, std::shared_ptr<const Buffer> source,
                   uint64_t offsetBytes, uint64_t lengthBytes)
    : backingOwner(std::move(owner)), buffer(std::move(source)), offset(offsetBytes), sizeBytes(lengthBytes) {}

Argument::Argument(Argument&& other) noexcept { swap(other); }

Argument& Argument::operator=(Argument other) noexcept {
    swap(other);
    return *this;
}

void Argument::swap(Argument& other) noexcept {
    using std::swap;
    swap(backingOwner, other.backingOwner);
    swap(buffer, other.buffer);
    swap(offset, other.offset);
    swap(sizeBytes, other.sizeBytes);
}

Prepared::Prepared(Prepared&& other) noexcept { swap(other); }

Prepared& Prepared::operator=(Prepared other) noexcept {
    swap(other);
    return *this;
}

void Prepared::swap(Prepared& other) noexcept {
    using std::swap;
    // Swap keeps every old owner alive until the entire old result is destroyed;
    // memberwise assignment could release its Gpu before its last program BO.
    swap(memoryOwner, other.memoryOwner);
    swap(stream, other.stream);
    swap(residents, other.residents);
    swap(stagedUbos, other.stagedUbos);
    swap(program, other.program);
    swap(scratch, other.scratch);
    swap(metadata, other.metadata);
}

std::vector<BoUse> Prepared::boUses() const {
    std::vector<BoUse> result;
    result.reserve(residents.size());
    for (const auto& r : residents) result.push_back({&r.buffer->bo(), r.write});
    return result;
}

bool prepare(const CompiledKernel& compiled, const compute_a5xx::Grid& grid,
             const std::vector<Argument>& args, Memory& memory, Prepared* out, std::string* why) {
    Metadata metadata;
    if (!metadataFromKernel(compiled.kernel, &metadata, why)) return false;
    return prepareWithMetadata(compiled, metadata, grid, args, memory, out, why);
}

bool prepareWithMetadata(const CompiledKernel& compiled, const Metadata& metadata,
                         const compute_a5xx::Grid& grid, const std::vector<Argument>& args,
                         Memory& memory, Prepared* out, std::string* why) {
    if (!out) return refuse(why, "A5xx AIR bridge: missing prepared output");
    Plan plan;
    if (!validate(compiled, metadata, grid, args, &plan, why)) return false;
    Prepared next;
    next.memoryOwner = memory.backingOwner();
    if (!next.memoryOwner)
        return refuse(why, "A5xx AIR bridge: memory provider must retain the backing GO/Gpu context");
    next.metadata = metadata;
    Bindings bindings;
    bindings.buffers.resize(kMaxArgumentSlots);
    auto retain = [&](const std::shared_ptr<const Buffer>& buffer, bool write,
                      const std::shared_ptr<const void>& owner) {
        for (auto& r : next.residents) {
            if (r.buffer.get() == buffer.get()) { r.write |= write; return; }
        }
        next.residents.push_back({owner, buffer, write});
    };
    for (uint32_t i = 0; i < compiled.kernel.numSsbos; ++i) {
        const auto& a = args[i];
        bindings.buffers[i] = {a.buffer.get(), a.offset};
        const auto* r = plan.resources[i];
        retain(a.buffer, r->write || r->irWrite, a.backingOwner);
    }
    auto allocate = [&](uint64_t bytes, uint64_t alignment) -> std::shared_ptr<Buffer> {
        std::string allocationWhy;
        auto buffer = memory.allocate(bytes, &allocationWhy);
        if (!buffer) {
            if (why) *why = "A5xx AIR bridge: allocation failed: " + allocationWhy;
            return nullptr;
        }
        if (!mapped(*buffer) || buffer->length() != bytes || !buffer->contents() ||
            buffer->gpuAddress() % alignment) {
            refuse(why, "A5xx AIR bridge: allocator returned invalid size/mapping/alignment");
            return nullptr;
        }
        // A fresh allocation must not alias another live named BO or any input.
        // Pointer comparison alone would miss an allocator reusing a GPU range.
        for (const auto& a : args)
            if (a.buffer && mapped(*a.buffer) && overlap(*buffer, *a.buffer)) {
                refuse(why, "A5xx AIR bridge: allocation aliases an input BO");
                return nullptr;
            }
        for (const auto& r : next.residents)
            if (overlap(*buffer, *r.buffer)) {
                refuse(why, "A5xx AIR bridge: allocation aliases another resident BO");
                return nullptr;
            }
        return buffer;
    };
    const uint64_t programBytes = compiled.words.size() * 4;
    auto program = allocate(programBytes, 128);
    if (!program) return false;
    std::memcpy(program->contents(), compiled.words.data(), size_t(programBytes));
    next.program = program;
    retain(program, false, next.memoryOwner);
    for (uint32_t block = 0; block < kMaxArgumentSlots; ++block) {
        const uint64_t size = plan.stagingBytes[block];
        if (!size) continue;
        auto staged = allocate(size, 64);
        if (!staged) return false;
        std::memset(staged->contents(), 0, size_t(size));
        retain(staged, false, next.memoryOwner);
        const auto& a = args[block];
        // Copy only each pushed range's intersection with logical argument
        // bytes. Unused gaps and compiler padding stay zero, never over-read.
        for (uint32_t i = 0; i < compiled.kernel.numUboRanges; ++i) {
            const auto& r = compiled.kernel.uboRanges[i];
            if (r.block != block) continue;
            const uint64_t start = uint64_t(r.startDword) * 4;
            const uint64_t end = std::min(a.sizeBytes, start + uint64_t(r.sizeDwords) * 4);
            const uint64_t bytes = end - start; // start < logical size checked
            std::string snapshotWhy;
            if (!memory.snapshot(*a.buffer, a.offset + start, bytes,
                                 static_cast<uint8_t*>(staged->contents()) + start, &snapshotWhy)) {
                if (why) *why = "A5xx AIR bridge: CPU snapshot failed: " + snapshotWhy;
                return false;
            }
        }
        next.stagedUbos.push_back({block, a.sizeBytes, staged});
        bindings.buffers[block] = {staged.get(), 0};
    }
    auto scratch = allocate(4, 4);
    if (!scratch) return false;
    std::memset(scratch->contents(), 0, 4);
    next.scratch = scratch;
    retain(scratch, true, next.memoryOwner);
    // Owned vectors are authoritative, so copying/moving CompiledKernel cannot
    // cause a dereference of stale Kernel::words/immediates pointers.
    Kernel kernel = compiled.kernel;
    kernel.words = compiled.words.data();
    kernel.immediates = compiled.immediates.empty() ? nullptr : compiled.immediates.data();
    compute_a5xx::Program p{program->gpuAddress(), programBytes, 530};
    compute_a5xx::AirLayout layout{metadata.driverParamsDwords, metadata.subgroupSize,
                                 metadata.a5xxBoundUboPushes};
    if (!compute_a5xx::encodeAir(next.stream, kernel, grid, bindings, p, scratch->gpuAddress(), layout, why))
        return false;
    if (next.stream.size() > kMaxStreamDwords)
        return refuse(why, "A5xx AIR bridge: bounded command fragment exceeds 4096 dwords");
    *out = std::move(next);
    if (why) why->clear();
    return true;
}

} // namespace hakios::metal::air_dispatch_a5xx
