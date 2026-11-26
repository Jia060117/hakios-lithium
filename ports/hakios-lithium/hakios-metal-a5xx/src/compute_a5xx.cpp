// SPDX-License-Identifier: MIT
// Source: official Mesa 25.0.7, src/gallium/drivers/freedreno/a5xx/.
// See evidence/gpu-metal-20261123/compute/sources.json for hashes and line spans.
#include "hakios-metal/compute_a5xx.hpp"
#include "hakios-metal/command.hpp" // Buffer's fixed, inline length/address/bo accessors

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace hakios::metal::compute_a5xx {
namespace {

// Minimal local values, independent of the concurrently maintained constants.
// adreno_pm4.xml:381,470,310 and freedreno_ringbuffer.h:387-390 establish these
// opcodes on A5xx. Stream supplies only the shared type4/type7 framing.
constexpr uint32_t kLoadState4 = 0x30;
constexpr uint32_t kExecCs = 0x33;
constexpr uint32_t kEventWrite = 0x46;
constexpr uint32_t kWfi = 0x26;
constexpr uint32_t kCacheFlushTs = 4; // adreno_pm4.xml:17; fd5_context.h:54-61
constexpr uint32_t kCsShader = 0xd;
constexpr uint32_t kCsSsbo = 0xf;
constexpr uint32_t kConstants = 1;
constexpr uint32_t kUbo = 2;
constexpr uint32_t kIndirect = 2;

// a5xx.xml:2452-2476,2485-2490,2578-2584,2666-2770.
constexpr uint32_t kSpSpCntl = 0xe580;
constexpr uint32_t kHlsqControl0 = 0xe784;
constexpr uint32_t kSpCsCtrl0 = 0xe5f0;
constexpr uint32_t kHlsqCsConfig = 0xe790;
constexpr uint32_t kHlsqCsCntl = 0xe796;
constexpr uint32_t kSpCsConfig = 0xe589;
constexpr uint32_t kHlsqCsConstlen = 0xe7dc;
constexpr uint32_t kSpCsObjStart = 0xe5f3;
constexpr uint32_t kHlsqUpdate = 0xe78a;
constexpr uint32_t kHlsqCsCntl0 = 0xe7b7;
constexpr uint32_t kCsTexCount = 0xe751;
constexpr uint32_t kNdrange0 = 0xe7b0;
constexpr uint32_t kKernelGroupX = 0xe7b9;
constexpr uint32_t kUcheInvalidateMin = 0x0e91; // a5xx.xml:1568-1572

bool refuse(std::string* why, const char* reason) {
    if (why) *why = reason;
    return false;
}

// Half-open ranges are checked before addition everywhere, including 64-bit
// overflow. The byte at UINT64_MAX is conservatively excluded.
bool range(uint64_t address, uint64_t bytes) {
    return address != 0 && bytes != 0 &&
           bytes <= std::numeric_limits<uint64_t>::max() - address;
}

bool overlaps(uint64_t a, uint64_t as, uint64_t b, uint64_t bs) {
    return a < b + bs && b < a + as;
}

void reg(pm4::Stream& s, uint32_t address, uint32_t value) {
    s.registers(address, &value, 1);
}

void wfi(pm4::Stream& s) {
    const uint32_t unused = 0;
    s.packet(kWfi, &unused, 0); // avoid nullptr arithmetic in Stream::packet
}

uint32_t state(uint32_t block, uint32_t source, uint32_t units, uint32_t dst = 0) {
    // adreno_pm4.xml:777-789: DST[13:0], SRC[17:16], BLOCK[21:18], NUM[31:22].
    return dst | source << 16 | block << 18 | units << 22;
}

bool localSysval(uint32_t id, int32_t maxReg) {
    if (id == kNoRegid) return true;
    // Ordinary xyz local ID must fit the complete footprint.
    return maxReg >= 0 && id < kNoRegid && id % 4 == 0 &&
           uint64_t(id) + 3 <= uint64_t(maxReg + 1) * 4;
}

bool workgroupSysval(uint32_t id) {
    // ir3_parser.y:846-851 requires gen>=5 and a high register. The shared
    // register file is r48.x-r55.w (ir3.h:1526-1533), excluded from maxReg
    // by ir3.c:423-427. No inference from the old API's gen6 comment.
    return id == kNoRegid || (id >= 48 * 4 && id % 4 == 0 && id + 3 <= 56 * 4);
}

struct BufferRange {
    uint64_t address;
    uint32_t bytes;
};

} // namespace

static bool encodeAny(pm4::Stream& stream, const Kernel& k, const Grid& g,
            const Bindings& b, const Program& p, uint64_t flushAddress,
            std::string* why, const Immediates& replacement, const AirLayout* air) {
    if (p.gpuId != 530)
        return refuse(why, "A5xx: program must explicitly declare the ir3 A530 compilation target");
    if (!k.words || k.numWords == 0 || k.instrlen == 0 ||
        uint64_t(k.instrlen) * 32 > k.numWords)
        return refuse(why, "A5xx: missing program or instrlen exceeds padded binary (128-byte units)");
    if (p.address % 128 != 0 || !range(p.address, p.sizeBytes) ||
        p.sizeBytes < uint64_t(k.numWords) * 4)
        return refuse(why, "A5xx: program mapping must cover words and have 128-byte alignment");
    if (!range(flushAddress, 4) || flushAddress % 4 != 0 ||
        overlaps(flushAddress, 4, p.address, p.sizeBytes))
        return refuse(why, "A5xx: flush scratch must be a separate aligned writable word");

    // Kernel does not expose A5xx UBO/SSBO-size constant allocations or image
    // mapping. Do not silently pretend the AIR/gen6 layout covers them.
    if (k.numUbos || k.constantDataSize || k.constantDataOffset || (!air && k.constantDataUbo) ||
        (!air && (k.numSsbos || k.numIbos || k.numUboRanges ||
                  k.driverParamsOffsetVec4 != -1 || k.localSizeVariable)))
        return refuse(why, "A5xx: AIR/UBO/driver-params/constant-data/variable-local layout is not covered");
    if (air && (k.numBufs || k.numSsbos > 24 || k.numIbos != k.numSsbos ||
                k.numUboRanges > kMaxUboRanges || !air->boundUboPushes ||
                k.driverParamsOffsetVec4 < -1))
        return refuse(why, "A5xx: AIR requires buffer-only SSBOs and verified non-bindless pushed UBO ranges");
    if (k.numSamp || k.numTextureSlots || k.samplerMask || b.nullMask)
        return refuse(why, "A5xx: textures, images and samplers are not covered");
    for (const auto& t : k.textures) {
        if (t.kind || t.dim || t.isArray || t.element || t.bits || t.read ||
            t.write || t.isNull || t.fbTarget)
            return refuse(why, "A5xx: texture/image metadata is not covered");
    }
    for (const auto* t : b.textures)
        if (t) return refuse(why, "A5xx: bound textures are not covered");
    for (const auto* s : b.samplers)
        if (s) return refuse(why, "A5xx: bound samplers are not covered");
    if (k.sharedSize || b.sharedBytes)
        return refuse(why, "A5xx: shared/threadgroup memory allocation is not covered");
    if (k.pvtmemSize || b.privateMemory.address || b.privateMemory.perFiber ||
        b.privateMemory.perSp || b.privateMemory.perWave)
        return refuse(why, "A5xx: private memory and hardware stack allocation are not covered");
    if (k.mergedRegs || k.earlyPreamble)
        return refuse(why, "A5xx: merged registers and early preamble are not A5xx program state");
    // ir3_compiler.c:279-290: 48 full vec4 registers, or 24 in double mode.
    if (k.maxReg < -1 || k.maxHalfReg < -1 || k.maxReg >= 48 ||
        k.maxHalfReg >= 48 || (k.doubleThreadsize && (k.maxReg >= 24 || k.maxHalfReg >= 24)) ||
        k.branchstackHw > 127)
        return refuse(why, "A5xx: register footprint/threadsize or branchstack exceeds supported fields");
    if (!localSysval(k.localIdRegid, k.maxReg) || !workgroupSysval(k.workGroupIdRegid))
        return refuse(why, "A5xx: local ID must fit ordinary registers, workgroup ID must fit shared r48-r55");
    const uint32_t bufferCount = air ? k.numSsbos : k.numBufs;
    if ((!air && k.numBufs > kMaxBufs) || b.buffers.size() < bufferCount)
        return refuse(why, "A5xx: text-kernel buffer count or bindings are invalid");

    // freedreno_screen.c:378-386: grid <=65535, local xyz <=1024/1024/64,
    // product <=1024. fd5_compute.c:124-149: nonzero local minus one.
    if (g.workDim < 1 || g.workDim > 3)
        return refuse(why, "A5xx: workDim must be 1..3");
    uint64_t threads = 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (g.local[i] == 0 || g.local[i] > (i == 2 ? 64u : 1024u) ||
            (!k.localSizeVariable && g.local[i] != k.localSize[i]) ||
            g.groups[i] == 0 || g.groups[i] > 65535)
            return refuse(why, "A5xx: grid/local dimension is invalid or disagrees with fixed kernel size");
        if (i >= g.workDim && (g.local[i] != 1 || g.groups[i] != 1))
            return refuse(why, "A5xx: dimensions outside workDim must be one");
        threads *= g.local[i];
    }
    if (threads > 1024)
        return refuse(why, "A5xx: threadgroup exceeds 1024 threads");

    // ir3_compiler.c:250-253: 512 vec4 compute constants; fd5_compute.c:64-69
    // programs CONSTLEN in groups of FOUR vec4, unlike gen6.
    if (k.constlen > 512 || k.constlen % 4 != 0 ||
        k.numImmediates > 2048 || k.immediatesOffsetVec4 > k.constlen)
        return refuse(why, "A5xx: constlen must be four-vec4 aligned and within 512 vec4");
    const uint32_t vec4s = (k.numImmediates + 3) / 4;
    if (vec4s > k.constlen - k.immediatesOffsetVec4)
        return refuse(why, "A5xx: immediates exceed constlen (no silent truncation)");
    if ((replacement.words && replacement.count != k.numImmediates) ||
        (!replacement.words && replacement.count != 0))
        return refuse(why, "A5xx: immediate override must supply exactly numImmediates words");
    const auto* initial = replacement.words ? replacement.words : k.immediates;
    if (k.numImmediates && !initial)
        return refuse(why, "A5xx: missing immediates");
    std::vector<uint32_t> constants(vec4s * 4, 0);
    if (k.numImmediates) std::copy_n(initial, k.numImmediates, constants.begin());
    std::vector<bool> patched(constants.size(), false);
    const uint32_t firstDword = k.immediatesOffsetVec4 * 4;
    auto patch = [&](uint32_t absolute, const uint32_t* words, uint32_t count) {
        if (absolute < firstDword ||
            uint64_t(absolute) + count > uint64_t(firstDword) + k.numImmediates)
            return false;
        const uint32_t start = absolute - firstDword;
        for (uint32_t i = 0; i < count; ++i)
            if (patched[start + i]) return false;
        for (uint32_t i = 0; i < count; ++i) {
            constants[start + i] = words[i];
            patched[start + i] = true;
        }
        return true;
    };
    auto bindingRange = [&](const Binding& binding, uint64_t bytes, BufferRange* result) {
        if (!binding.buffer || bytes == 0 || bytes > UINT32_MAX || bytes % 4 || binding.offset % 4)
            return false;
        const auto& buffer = *binding.buffer;
        if (buffer.length() > buffer.bo().size() || binding.offset > buffer.length() ||
            bytes > buffer.length() - binding.offset ||
            !range(buffer.gpuAddress(), buffer.length()))
            return false;
        const uint64_t address = buffer.gpuAddress() + binding.offset;
        if (address % 4 || overlaps(flushAddress, 4, buffer.gpuAddress(), buffer.length()) ||
            overlaps(p.address, p.sizeBytes, buffer.gpuAddress(), buffer.length()))
            return false;
        *result = {address, uint32_t(bytes)};
        return true;
    };
    std::vector<BufferRange> buffers;
    for (uint32_t i = 0; i < bufferCount; ++i) {
        const Binding& binding = b.buffers[i];
        uint64_t bytes = air && binding.buffer && binding.offset <= binding.buffer->length()
                             ? binding.buffer->length() - binding.offset : 0;
        if (!air) bytes = k.bufs[i].sizeBytes;
        BufferRange buffer{};
        if (!bindingRange(binding, bytes, &buffer))
            return refuse(why, "A5xx: buffer range is unaligned, out of backing allocation or aliases program/flush scratch");
        buffers.push_back(buffer);
        if (!air && k.bufs[i].constRegid != kNoConstRegid) {
            const uint32_t ptr[] = {uint32_t(buffer.address), uint32_t(buffer.address >> 32)};
            if (!patch(k.bufs[i].constRegid, ptr, 2))
                return refuse(why, "A5xx: buffer address constant patch is outside immediates or overlaps another patch");
        }
    }
    if (k.numwgConstRegid != kNoConstRegid && !patch(k.numwgConstRegid, g.groups, 3))
        return refuse(why, "A5xx: num-workgroups constant patch is outside immediates or overlaps an address");

    std::vector<uint32_t> driver;
    std::vector<std::array<uint32_t, 3>> pushes;
    std::vector<bool> occupied(k.constlen, false);
    auto claim = [&](uint32_t dst, uint32_t count) {
        if (dst > k.constlen || count > k.constlen - dst) return false;
        for (uint32_t i = dst; i < dst + count; ++i) if (occupied[i]) return false;
        for (uint32_t i = dst; i < dst + count; ++i) occupied[i] = true;
        return true;
    };
    claim(k.immediatesOffsetVec4, vec4s); // already range-checked
    if (air) {
        if (k.numwgConstRegid != kNoConstRegid)
            return refuse(why, "A5xx: AIR uses driver params, not text numwg constant patches");
        if (k.driverParamsOffsetVec4 == -1) {
            if (air->numDriverParams || k.localSizeVariable)
                return refuse(why, "A5xx: variable local size needs uploaded driver params");
        } else {
            // ir3_const.h:608-629,632-681; exact 16-dword struct, count aligned
            // to 16 dwords on fd5. No driver UBO or indirect-grid path here.
            if (air->numDriverParams == 0 || air->numDriverParams > 16 ||
                (k.localSizeVariable && air->numDriverParams < 12) ||
                (air->subgroupSize != 32 && air->subgroupSize != 64 && air->subgroupSize != 128) ||
                !claim(uint32_t(k.driverParamsOffsetVec4), 4))
                return refuse(why, "A5xx: driver-param count/subgroup/range missing, invalid or overlapping");
            const uint32_t shift = air->subgroupSize == 32 ? 5 : air->subgroupSize == 64 ? 6 : 7;
            driver = {g.groups[0], g.groups[1], g.groups[2], g.workDim,
                      0, 0, 0, air->subgroupSize, g.local[0], g.local[1], g.local[2], shift,
                      0, 0, 0, 0};
        }
        for (uint32_t i = 0; i < k.numUboRanges; ++i) {
            const auto& r = k.uboRanges[i];
            // compiler.cpp may export constDataUbo=1 from an unused reserved
            // consts_ubo.idx even when size is zero. The tag alone is harmless;
            // a pushed range referring to that tag is not an application UBO.
            if (k.constantDataUbo && r.block == k.constantDataUbo - 1)
                return refuse(why, "A5xx: pushed constant-data UBO range is not covered");
            if (r.block >= b.buffers.size() || r.sizeDwords == 0 ||
                r.sizeDwords % 16 || r.offsetVec4 % 4 || r.startDword % 16 ||
                !claim(r.offsetVec4, r.sizeDwords / 4))
                return refuse(why, "A5xx: pushed UBO destination/source/count alignment, binding or overlap invalid");
            const auto& binding = b.buffers[r.block];
            const uint64_t start = uint64_t(r.startDword) * 4;
            const uint64_t bytes = uint64_t(r.sizeDwords) * 4;
            if (start > UINT64_MAX - binding.offset)
                return refuse(why, "A5xx: pushed UBO source offset overflow");
            BufferRange mapped{};
            if (!bindingRange({binding.buffer, binding.offset + start}, bytes, &mapped) || mapped.address % 64)
                return refuse(why, "A5xx: pushed UBO needs a complete 64-aligned padded backing range");
            pushes.push_back({state(kCsShader, kIndirect, r.sizeDwords / 4, r.offsetVec4),
                              uint32_t(mapped.address) | kConstants, uint32_t(mapped.address >> 32)});
        }
    }

    // All validation precedes emission. Copy preserves the existing stream even
    // if vector allocation throws; successful assignment appends this fragment.
    pm4::Stream out = stream;
    // fd5_emit.h:94-104: UCHE full invalidation/flush setup + WFI. Context restore
    // is the caller's responsibility; this is just its cache operation.
    const uint32_t invalidate[] = {0, 0, 0, 0, 0x12};
    out.registers(kUcheInvalidateMin, invalidate, 5);
    wfi(out);

    // fd5_compute.c:20-96, cs_program_emit, in the same register order.
    const uint32_t preload = k.instrlen <= 32 ? k.instrlen : 0;
    const uint32_t threadsize = k.doubleThreadsize ? 1 : 0;
    reg(out, kSpSpCntl, 0);
    reg(out, kHlsqControl0, 0x880 | threadsize << 2);
    reg(out, kSpCsCtrl0, 0x2 | (preload ? 0x4 : 0) | threadsize << 3 |
        uint32_t(k.maxHalfReg + 1) << 4 | uint32_t(k.maxReg + 1) << 10 |
        k.branchstackHw << 25);
    reg(out, kHlsqCsConfig, 1);
    reg(out, kHlsqCsCntl, preload << 1 | (bufferCount ? 1 : 0));
    reg(out, kSpCsConfig, 1);
    const uint32_t lengths[] = {k.constlen / 4, preload};
    out.registers(kHlsqCsConstlen, lengths, 2);
    // fd5_program.c:67-90; no private allocation => zero private state.
    const uint32_t object[] = {uint32_t(p.address), uint32_t(p.address >> 32), 0, 0, 0, 0};
    out.registers(kSpCsObjStart, object, 6);
    reg(out, kHlsqUpdate, 0x1f00000);
    const uint32_t ids[] = {k.workGroupIdRegid | 0xfcfc00 | k.localIdRegid << 24, 1};
    out.registers(kHlsqCsCntl0, ids, 2);
    if (preload) {
        // fd5_program.c:43-54: ST4_SHADER=0 is in address word's low bits.
        const uint32_t shader[] = {state(kCsShader, kIndirect, preload),
                                  uint32_t(p.address), uint32_t(p.address >> 32)};
        out.packet(kLoadState4, shader, 3);
    }
    // fd5_emit.c:859-897: no texture or image state for this subset.
    reg(out, kCsTexCount, 0);
    if (!buffers.empty()) {
        // fd5_emit.c:398-450: two separate SSBO state blocks, not A6xx's
        // 16-dword IBO table. WIDTH is bytes[15:0] shifted LEFT by 16.
        std::vector<uint32_t> sizes = {state(kCsSsbo, 0, bufferCount), kConstants, 0};
        std::vector<uint32_t> addresses = {state(kCsSsbo, 0, bufferCount), kUbo, 0};
        for (const auto& buffer : buffers) {
            sizes.push_back((buffer.bytes & 0xffff) << 16);
            sizes.push_back(buffer.bytes >> 16);
            addresses.push_back(uint32_t(buffer.address));
            addresses.push_back(uint32_t(buffer.address >> 32));
        }
        out.packet(kLoadState4, sizes.data(), uint32_t(sizes.size()));
        out.packet(kLoadState4, addresses.data(), uint32_t(addresses.size()));
    }
    if (!pushes.empty() || !constants.empty() || !driver.empty()) wfi(out);
    for (const auto& push : pushes) out.packet(kLoadState4, push.data(), 3);
    if (!constants.empty()) {
        // ir3_const.h:413-423, fd5_emit.c:40-56: WFI before direct constants.
        std::vector<uint32_t> payload = {
            state(kCsShader, 0, vec4s, k.immediatesOffsetVec4), kConstants, 0};
        payload.insert(payload.end(), constants.begin(), constants.end());
        out.packet(kLoadState4, payload.data(), uint32_t(payload.size()));
    }
    if (!driver.empty()) {
        wfi(out); // ir3_emit_cs_driver_params calls ring_wfi before upload
        std::vector<uint32_t> payload = {
            state(kCsShader, 0, 4, uint32_t(k.driverParamsOffsetVec4)), kConstants, 0};
        payload.insert(payload.end(), driver.begin(), driver.end());
        out.packet(kLoadState4, payload.data(), uint32_t(payload.size()));
    }
    const uint32_t nd[] = {
        g.workDim | (g.local[0] - 1) << 2 | (g.local[1] - 1) << 12 | (g.local[2] - 1) << 22,
        g.local[0] * g.groups[0], 0, g.local[1] * g.groups[1], 0, g.local[2] * g.groups[2], 0};
    out.registers(kNdrange0, nd, 7);
    const uint32_t ones[] = {1, 1, 1};
    out.registers(kKernelGroupX, ones, 3);
    const uint32_t exec[] = {0, g.groups[0], g.groups[1], g.groups[2]};
    out.packet(kExecCs, exec, 4); // fd5_compute.c:164-170
    // fd5_context.h:54-61, fd5_emit_flush: event has NO gen6 timestamp bit
    // and NO gen6 WAIT_REG_MEM packet. Completion/fence is outside this module.
    const uint32_t flush[] = {kCacheFlushTs, uint32_t(flushAddress), uint32_t(flushAddress >> 32), 0};
    out.packet(kEventWrite, flush, 4);
    wfi(out);
    stream = std::move(out);
    if (why) why->clear();
    return true;
}

bool encode(pm4::Stream& stream, const Kernel& k, const Grid& g,
            const Bindings& b, const Program& p, uint64_t flushAddress,
            std::string* why, const Immediates& replacement) {
    return encodeAny(stream, k, g, b, p, flushAddress, why, replacement, nullptr);
}

bool encodeAir(pm4::Stream& stream, const Kernel& k, const Grid& g,
               const Bindings& b, const Program& p, uint64_t flushAddress,
               const AirLayout& layout, std::string* why, const Immediates& replacement) {
    return encodeAny(stream, k, g, b, p, flushAddress, why, replacement, &layout);
}

} // namespace hakios::metal::compute_a5xx
