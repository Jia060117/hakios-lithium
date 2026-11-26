// SPDX-License-Identifier: MIT
// Mesa 25.0.7: fd5_program.c:25-85,161-752; fd5_emit.c:494-525,630-800;
// ir3_shader.h:62-77,1289-1361; ir3_const.h:447-532,578-606.
#include "hakios-metal/program_a5xx.hpp"
#include "hakios-metal/a5xx.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace hakios::metal::program_a5xx {
namespace {
using namespace a5xx;
// shader_enums.h, pinned Mesa; verified against C oracle's actual enum values.
constexpr uint32_t kVertexIdZeroBase = 12, kInstanceId = 10;
constexpr uint32_t kBaryPerspPixel = 56, kBaryLinearPixel = 60;
constexpr uint32_t kColor0 = 4, kGeneric0 = 32;
constexpr uint64_t kAddressLimit = uint64_t(1) << 49;

bool fail(std::string* why, const char* message) {
    if (why) *why = message;
    return false;
}
void reg(pm4::Stream& s, uint32_t r, uint32_t v) { s.registers(r, &v, 1); }
template<size_t N> void regs(pm4::Stream& s, uint32_t r, const uint32_t (&v)[N]) {
    s.registers(r, v, uint32_t(N));
}
template<size_t N> void packet(pm4::Stream& s, uint32_t op, const uint32_t (&v)[N]) {
    s.packet(op, v, uint32_t(N));
}
bool range(const Image& image, uint64_t needed) {
    return image.address && image.address % 128 == 0 &&
        image.address < kAddressLimit && image.bytes >= needed &&
        image.bytes <= kAddressLimit - image.address;
}
bool overlap(uint64_t a, uint64_t as, uint64_t b, uint64_t bs) {
    return a < b + bs && b < a + as;
}
bool registerRange(uint32_t id, uint32_t count, int32_t footprint, bool absent = false) {
    return (absent && id == kNoRegid) ||
        (id < kNoRegid && footprint >= 0 && uint64_t(id) + count <= uint64_t(footprint + 1) * 4);
}
unsigned span(uint32_t mask) {
    unsigned result = 0;
    while (mask) { ++result; mask >>= 1; }
    return result;
}

struct Validated {
    const CompiledStage* vs = nullptr;
    const CompiledStage* fs = nullptr;
    Linkage linkage{};
    uint32_t vertexId = kNoRegid, instanceId = kNoRegid;
};

bool stageValid(const CompiledStage& c, uint8_t kind, std::string* why) {
    const Stage& s = c.stage; const Kernel& k = s.k;
    if (s.kind != kind || k.compiledChipId != 0x05030004 ||
        !k.a5xxBoundUboPushes || k.mergedRegs || k.earlyPreamble)
        return fail(why, "A5xx program: exact compiler-produced A530 stage provenance required");
    if (c.words.empty() || c.words.size() > 64 * 32 ||
        k.numWords != c.words.size() || !k.instrlen || k.instrlen > 64 ||
        uint64_t(k.instrlen) * 32 != c.words.size() ||
        k.numImmediates != c.immediates.size() || c.immediates.size() > 2048)
        return fail(why, "A5xx program: bounded owned binary/immediate sizes disagree");
    if (k.constlen > 512 || k.constlen % 4 ||
        k.immediatesOffsetVec4 > k.constlen ||
        (k.numImmediates + 3) / 4 > k.constlen - k.immediatesOffsetVec4)
        return fail(why, "A5xx program: invalid constant extent/alignment");
    if (k.maxReg < -1 || k.maxReg >= 48 || k.maxHalfReg < -1 || k.maxHalfReg >= 48 ||
        (k.doubleThreadsize && (k.maxReg >= 24 || k.maxHalfReg >= 24)) ||
        (kind == kStageVertex && k.doubleThreadsize) || k.branchstackHw > 32)
        return fail(why, "A5xx program: unsupported register footprint/threadsize/branchstack");
    if (!c.resources.empty() || !c.inlineSamplers.empty() || c.iboBase || c.writesMemory ||
        k.sharedSize || k.pvtmemSize || k.pvtmemPerWave || k.numBufs || k.numUbos ||
        k.numSsbos || k.numIbos || k.numSamp || k.numUboRanges || k.numTextureSlots ||
        k.samplerMask || k.constantDataSize || k.constantDataOffset || k.constantDataUbo ||
        k.localSizeVariable || k.numwgConstRegid != kNoConstRegid)
        return fail(why, "A5xx program: resources/private/shared/constant-data layout not supported");
    for (const auto& t : k.textures)
        if (t.kind || t.dim || t.isArray || t.element || t.bits || t.read ||
            t.write || t.isNull || t.fbTarget)
            return fail(why, "A5xx program: texture metadata not supported");
    if (s.numInputs > kMaxStageInputs || s.numOutputs > kMaxStageOutputs ||
        s.numAttributes || s.clipMask || s.cullMask || s.writesLayer || s.writesViewport ||
        s.pointSizeRegid != kNoRegid || s.needPixlod || s.needFullQuad ||
        s.numSamplerPrefetch || s.prefetchEndOfQuad)
        return fail(why, "A5xx program: attributes/clip/point/quad/prefetch metadata not supported");
    if (k.driverParamsOffsetVec4 < -1 ||
        (k.driverParamsOffsetVec4 == -1 && k.driverParamsDwords) ||
        (k.driverParamsOffsetVec4 >= 0 && (kind != kStageVertex ||
            !k.driverParamsDwords || k.driverParamsDwords > 4 ||
            uint32_t(k.driverParamsOffsetVec4) >= k.constlen)))
        return fail(why, "A5xx program: only first-vec4 VS driver params are covered");
    if (k.driverParamsOffsetVec4 >= 0) {
        const uint32_t begin = uint32_t(k.driverParamsOffsetVec4);
        const uint32_t immediateEnd = k.immediatesOffsetVec4 + (k.numImmediates + 3) / 4;
        if (k.numImmediates && begin < immediateEnd && k.immediatesOffsetVec4 < begin + 1)
            return fail(why, "A5xx program: overlapping immediates and VS driver params");
    }
    std::array<bool, 256> outputs{};
    for (uint32_t i = 0; i < s.numOutputs; ++i) {
        const auto& o = s.outputs[i];
        if (outputs[o.slot] || o.half || o.pad != kElemFloat ||
            !registerRange(o.regid, 4, k.maxReg))
            return fail(why, "A5xx program: duplicate/non-float/half/out-of-footprint output");
        outputs[o.slot] = true;
    }
    return true;
}

bool validate(const CompiledProgram& c, const Images& images, Validated* out, std::string* why) {
    if (!c.vs || !c.fs) return fail(why, "A5xx program: linked compiled VS/FS owners required");
    if (!stageValid(*c.vs, kStageVertex, why) || !stageValid(*c.fs, kStageFragment, why))
        return false;
    const auto& v = c.vs->stage; const auto& f = c.fs->stage;
    const auto& p = c.program;
    if (p.samples != 1 || p.fsIboBase || p.ssboWrites ||
        p.blend.enabled || p.blend.writeMask != 15 ||
        p.blend.rgbOp || p.blend.alphaOp || p.blend.srcRgb != 1 ||
        p.blend.dstRgb || p.blend.srcAlpha != 1 || p.blend.dstAlpha)
        return fail(why, "A5xx program: single-sample, full-mask, blend-off pipeline required");
    for (const auto& b : p.moreBlend)
        if (b.enabled || b.writeMask != 15 || b.rgbOp || b.alphaOp ||
            b.srcRgb != 1 || b.dstRgb || b.srcAlpha != 1 || b.dstAlpha)
            return fail(why, "A5xx program: additional blend state not supported");
    for (const auto& a : p.vertexDescriptor.attributes)
        if (a.format != VertexFormat::kInvalid || a.offset || a.bufferIndex)
            return fail(why, "A5xx program: vertex descriptor attributes not supported");
    for (const auto& l : p.vertexDescriptor.layouts)
        if (l.stride || l.step != VertexStep::kPerVertex || l.stepRate != 1)
            return fail(why, "A5xx program: vertex fetch layouts not supported");
    if (v.k.instrlen + f.k.instrlen > 64 || v.k.constlen + f.k.constlen > 512)
        return fail(why, "A5xx program: combined instruction/constant budget exceeded");
    if (!range(images.vs, c.vs->words.size() * 4) ||
        !range(images.fs, c.fs->words.size() * 4) ||
        overlap(images.vs.address, images.vs.bytes, images.fs.address, images.fs.bytes))
        return fail(why, "A5xx program: shader images invalid or overlapping");

    Validated next;
    next.vs = c.vs.get(); next.fs = c.fs.get();
    bool vertexSeen = false, instanceSeen = false, positionSeen = false;
    for (uint32_t i = 0; i < v.numInputs; ++i) {
        const auto& in = v.inputs[i];
        if (!in.compmask) continue;
        if (!in.sysval || in.bary || in.flat || in.half || in.compmask != 1 ||
            !registerRange(in.regid, 1, v.k.maxReg))
            return fail(why, "A5xx program: VS inputs must be scalar full-register sysvals");
        if (in.slot == kVertexIdZeroBase && !vertexSeen) {
            next.vertexId = in.regid; vertexSeen = true;
        } else if (in.slot == kInstanceId && !instanceSeen) {
            next.instanceId = in.regid; instanceSeen = true;
        } else return fail(why, "A5xx program: duplicate/unsupported VS sysval");
    }
    // fillStage currently asks for SYSTEM_VALUE_VERTEX_ID, while fd5 asks for
    // ZERO_BASE. The raw exported input table above is the actual compiler fact.
    if ((v.vertexIdRegid != kNoRegid && v.vertexIdRegid != next.vertexId) ||
        v.instanceIdRegid != next.instanceId)
        return fail(why, "A5xx program: VS convenience regids disagree with raw sysval table");
    for (uint32_t i = 0; i < v.numOutputs; ++i) {
        const auto& o = v.outputs[i];
        if (o.slot == 0) {
            positionSeen = o.regid == v.positionRegid;
        } else if (o.slot < kGeneric0 || o.slot >= kGeneric0 + 32)
            return fail(why, "A5xx program: VS output slot not supported");
    }
    if (!positionSeen || !registerRange(v.positionRegid, 4, v.k.maxReg))
        return fail(why, "A5xx program: VS position output missing or inconsistent");
    if (f.numColorOutputs != 1 || f.numOutputs != 1 || f.outputs[0].slot != kColor0 ||
        f.outputs[0].regid != f.colorRegid[0] || f.colorHalf[0] ||
        f.depthRegid != kNoRegid || f.sampleMaskRegid != kNoRegid ||
        f.stencilRefRegid != kNoRegid || f.writesPos || f.writesSmask || f.writesStencilref ||
        f.dualSrcBlend || f.hasKill || f.perSamp || f.readsPrimid || f.fbRead ||
        f.fragCoordRegid != kNoRegid || f.fragcoordCompmask ||
        f.faceRegid != kNoRegid || f.sampleIdRegid != kNoRegid || f.sampleMaskInRegid != kNoRegid)
        return fail(why, "A5xx program: only plain float color0 FS output and pixel barycentrics covered");
    for (uint32_t i = 1; i < kMaxColorOutputs; ++i)
        if (f.colorRegid[i] != kNoRegid || f.colorHalf[i])
            return fail(why, "A5xx program: extra FS color output metadata");
    for (uint32_t i = 0; i < kIjCount; ++i)
        if ((i != 0 && i != 4 && f.ijRegid[i] != kNoRegid) ||
            !registerRange(f.ijRegid[i], 2, f.k.maxReg, true))
            return fail(why, "A5xx program: unsupported/out-of-footprint FS barycentric registers");
    if (f.totalIn > 124) return fail(why, "A5xx program: FS varyings leave no room for position");
    std::array<bool, 128> locations{};
    std::array<bool, 256> slots{};
    bool perspSeen = false, linearSeen = false;
    auto& l = next.linkage;
    for (uint32_t i = 0; i < f.numInputs; ++i) {
        const auto& in = f.inputs[i];
        if (!in.compmask) continue;
        if (in.sysval) {
            bool* seen = in.slot == kBaryPerspPixel ? &perspSeen :
                         in.slot == kBaryLinearPixel ? &linearSeen : nullptr;
            const uint32_t id = in.slot == kBaryPerspPixel ? f.ijRegid[0] : f.ijRegid[4];
            if (!seen || *seen || in.bary || in.flat || in.half || in.compmask != 3 ||
                in.regid != id || !registerRange(in.regid, 2, f.k.maxReg))
                return fail(why, "A5xx program: FS raw sysval table inconsistent with pixel barycentrics");
            *seen = true;
            continue;
        }
        const unsigned n = span(in.compmask);
        if (!in.bary || in.flat || in.half || in.slot < kGeneric0 ||
            in.slot >= kGeneric0 + 32 || slots[in.slot] ||
            n > 4 || in.compmask != ((1u << n) - 1) ||
            in.inloc >= f.totalIn || n > f.totalIn - in.inloc ||
            l.cnt >= kMaxStageOutputs - 1)
            return fail(why, "A5xx program: only bounded contiguous smooth generic varying masks covered");
        slots[in.slot] = true;
        const StageOutput* output = nullptr;
        for (uint32_t j = 0; j < v.numOutputs; ++j)
            if (v.outputs[j].slot == in.slot) output = &v.outputs[j];
        if (!output) return fail(why, "A5xx program: FS varying missing VS output");
        for (unsigned j = 0; j < n; ++j) {
            const unsigned loc = in.inloc + j;
            if (locations[loc]) return fail(why, "A5xx program: overlapping varying locations");
            locations[loc] = true;
            l.varmask[loc / 32] |= 1u << (loc % 32);
        }
        l.var[l.cnt++] = {in.slot, output->regid, in.compmask, in.inloc};
        l.maxLoc = std::max(l.maxLoc, uint32_t(in.inloc + n));
    }
    if ((perspSeen != (f.ijRegid[0] != kNoRegid)) ||
        (linearSeen != (f.ijRegid[4] != kNoRegid)) ||
        (f.totalIn && (!perspSeen && !linearSeen)) || l.maxLoc != f.totalIn)
        return fail(why, "A5xx program: varying span/pixel barycentric metadata inconsistent");
    l.positionLoc = l.maxLoc;
    l.var[l.cnt++] = {0, uint8_t(v.positionRegid), 15, uint8_t(l.maxLoc)};
    l.maxLoc += 4; l.mrtComponents = 15;
    const auto& supplied = p.link;
    if (supplied.cnt != l.cnt || supplied.maxLoc != l.maxLoc ||
        supplied.positionLoc != l.positionLoc || supplied.psizeLoc != 255 ||
        supplied.mrtComponents != 15)
        return fail(why, "A5xx program: linked program summary disagrees with compiler stages");
    for (uint32_t i = 0; i < l.cnt; ++i) {
        const auto& a = supplied.var[i]; const auto& b = l.var[i];
        if (a.slot != b.slot || a.regid != b.regid || a.compmask != b.compmask || a.loc != b.loc)
            return fail(why, "A5xx program: linked varying map disagrees with compiler stages");
    }
    for (uint32_t i = 0; i < 4; ++i)
        if (supplied.varmask[i] != l.varmask[i])
            return fail(why, "A5xx program: linked varying disable mask disagrees");
    for (uint32_t i = 0; i < 8; ++i)
        if (supplied.interpMode[i] || supplied.psReplMode[i])
            return fail(why, "A5xx program: flat/point-sprite interpolation not covered");
    *out = next;
    return true;
}

void shader(pm4::Stream& s, const CompiledStage& c, const Image& image,
            uint32_t object, a4xx_state_block block) {
    const uint32_t objectWords[]{uint32_t(image.address), uint32_t(image.address >> 32), 0, 0, 0, 0};
    regs(s, object, objectWords); // no private memory or hardware stack
    const uint32_t load[]{CP_LOAD_STATE4_0_STATE_SRC(SS4_INDIRECT) |
        CP_LOAD_STATE4_0_STATE_BLOCK(block) | CP_LOAD_STATE4_0_NUM_UNIT(c.stage.k.instrlen),
        uint32_t(image.address), uint32_t(image.address >> 32)};
    packet(s, CP_LOAD_STATE4, load);
}
void constants(pm4::Stream& s, a4xx_state_block block, uint32_t offset,
               const std::vector<uint32_t>& words) {
    if (words.empty()) return;
    const uint32_t count = uint32_t((words.size() + 3) & ~size_t(3));
    std::vector<uint32_t> payload(3 + count, 0);
    payload[0] = CP_LOAD_STATE4_0_DST_OFF(offset) | CP_LOAD_STATE4_0_STATE_SRC(SS4_DIRECT) |
        CP_LOAD_STATE4_0_STATE_BLOCK(block) | CP_LOAD_STATE4_0_NUM_UNIT(count / 4);
    payload[1] = CP_LOAD_STATE4_1_STATE_TYPE(ST4_CONSTANTS);
    std::copy(words.begin(), words.end(), payload.begin() + 3);
    s.packet(CP_LOAD_STATE4, payload.data(), uint32_t(payload.size()));
}

void state(pm4::Stream& s, const Validated& x, const Images& images) {
    const auto& v = x.vs->stage; const auto& f = x.fs->stage;
    const auto& l = x.linkage;
    const uint32_t vc = v.k.constlen / 4, fc = f.k.constlen / 4;
    const uint32_t fsOffset = 64 - f.k.instrlen;
    // fd5 setup_stages: constants bottom-up, FS instructions at buffer top.
    uint32_t hlsq[5]{}, sp[5]{}, control[5]{};
    for (unsigned i = 0; i < 5; ++i) {
        const uint32_t coff = i == 0 ? 0 : i == 1 ? vc : vc + fc;
        const uint32_t ioff = i == 0 ? 0 : fsOffset;
        hlsq[i] = A5XX_HLSQ_VS_CONFIG_CONSTOBJECTOFFSET(coff) |
            A5XX_HLSQ_VS_CONFIG_SHADEROBJOFFSET(ioff) |
            (i < 2 ? A5XX_HLSQ_VS_CONFIG_ENABLED : 0);
        sp[i] = A5XX_SP_VS_CONFIG_CONSTOBJECTOFFSET(coff) |
            A5XX_SP_VS_CONFIG_SHADEROBJOFFSET(ioff) |
            (i < 2 ? A5XX_SP_VS_CONFIG_ENABLED : 0);
        control[i] = i == 0 ? v.k.instrlen : i == 1 ? f.k.instrlen : 0;
    }
    regs(s, REG_A5XX_HLSQ_VS_CONFIG, hlsq);
    reg(s, REG_A5XX_HLSQ_CS_CONFIG, 0);
    regs(s, REG_A5XX_HLSQ_VS_CNTL, control);
    regs(s, REG_A5XX_SP_VS_CONFIG, sp);
    reg(s, REG_A5XX_SP_CS_CONFIG, 0);
    const uint32_t vsSizes[]{vc, v.k.instrlen}, fsSizes[]{fc, f.k.instrlen}, zero[2]{};
    regs(s, REG_A5XX_HLSQ_VS_CONSTLEN, vsSizes);
    regs(s, REG_A5XX_HLSQ_FS_CONSTLEN, fsSizes);
    regs(s, REG_A5XX_HLSQ_HS_CONSTLEN, zero);
    regs(s, REG_A5XX_HLSQ_DS_CONSTLEN, zero);
    regs(s, REG_A5XX_HLSQ_GS_CONSTLEN, zero);
    regs(s, REG_A5XX_HLSQ_CS_CONSTLEN, zero);
    reg(s, REG_A5XX_SP_VS_CTRL_REG0,
        A5XX_SP_VS_CTRL_REG0_HALFREGFOOTPRINT(v.k.maxHalfReg + 1) |
        A5XX_SP_VS_CTRL_REG0_FULLREGFOOTPRINT(v.k.maxReg + 1) |
        A5XX_SP_VS_CTRL_REG0_BUFFER | 2 | A5XX_SP_VS_CTRL_REG0_BRANCHSTACK(v.k.branchstackHw));
    uint32_t disable[4];
    for (unsigned i = 0; i < 4; ++i) disable[i] = ~l.varmask[i];
    regs(s, REG_A5XX_VPC_VAR_DISABLE(0), disable);
    // Mesa's zero-initialized ir3_shader_linkage supplies zero tail entries.
    for (uint32_t i = 0; i < (l.cnt + 1) / 2; ++i)
        reg(s, REG_A5XX_SP_VS_OUT_REG(i),
            A5XX_SP_VS_OUT_REG_A_REGID(l.var[2 * i].regid) |
            A5XX_SP_VS_OUT_REG_A_COMPMASK(l.var[2 * i].compmask) |
            A5XX_SP_VS_OUT_REG_B_REGID(l.var[2 * i + 1].regid) |
            A5XX_SP_VS_OUT_REG_B_COMPMASK(l.var[2 * i + 1].compmask));
    for (uint32_t i = 0; i < (l.cnt + 3) / 4; ++i) {
        uint32_t destinations = 0;
        for (uint32_t j = 0; j < 4; ++j) {
            const uint32_t n = i * 4 + j;
            if (n < l.cnt) destinations |= uint32_t(l.var[n].loc) << (j * 8);
        }
        reg(s, REG_A5XX_SP_VS_VPC_DST_REG(i), destinations);
    }
    shader(s, *x.vs, images.vs, REG_A5XX_SP_VS_OBJ_START_LO, SB4_VS_SHADER);
    reg(s, REG_A5XX_PC_PRIM_VTX_CNTL, 0);
    reg(s, REG_A5XX_SP_PRIMITIVE_CNTL, A5XX_SP_PRIMITIVE_CNTL_VSOUT(l.cnt));
    reg(s, REG_A5XX_VPC_CNTL_0, A5XX_VPC_CNTL_0_STRIDE_IN_VPC(l.maxLoc) |
        (f.totalIn ? A5XX_VPC_CNTL_0_VARYING : 0) | 0x10000);
    // Mesa emits FS object here, but defers its shader preload until below.
    const uint32_t fsObject[]{uint32_t(images.fs.address), uint32_t(images.fs.address >> 32), 0, 0, 0, 0};
    regs(s, REG_A5XX_SP_FS_OBJ_START_LO, fsObject);
    const a3xx_threadsize threads = f.k.doubleThreadsize ? FOUR_QUADS : TWO_QUADS;
    const uint32_t hlsqControls[]{
        A5XX_HLSQ_CONTROL_0_REG_FSTHREADSIZE(threads) |
            A5XX_HLSQ_CONTROL_0_REG_CSTHREADSIZE(TWO_QUADS) | 0x880,
        A5XX_HLSQ_CONTROL_1_REG_PRIMALLOCTHRESHOLD(63),
        A5XX_HLSQ_CONTROL_2_REG_FACEREGID(kNoRegid) |
            A5XX_HLSQ_CONTROL_2_REG_SAMPLEID(kNoRegid) |
            A5XX_HLSQ_CONTROL_2_REG_SAMPLEMASK(kNoRegid) |
            A5XX_HLSQ_CONTROL_2_REG_CENTERRHW(kNoRegid),
        A5XX_HLSQ_CONTROL_3_REG_IJ_PERSP_PIXEL(f.ijRegid[0]) |
            A5XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_PIXEL(f.ijRegid[4]) |
            A5XX_HLSQ_CONTROL_3_REG_IJ_PERSP_CENTROID(kNoRegid),
        A5XX_HLSQ_CONTROL_4_REG_XYCOORDREGID(kNoRegid) |
            A5XX_HLSQ_CONTROL_4_REG_ZWCOORDREGID(kNoRegid) |
            A5XX_HLSQ_CONTROL_4_REG_IJ_PERSP_SAMPLE(kNoRegid) |
            A5XX_HLSQ_CONTROL_4_REG_IJ_LINEAR_SAMPLE(kNoRegid)};
    regs(s, REG_A5XX_HLSQ_CONTROL_0_REG, hlsqControls);
    reg(s, REG_A5XX_SP_FS_CTRL_REG0, (f.totalIn ? A5XX_SP_FS_CTRL_REG0_VARYING : 0) |
        0x40002 | A5XX_SP_FS_CTRL_REG0_BUFFER | A5XX_SP_FS_CTRL_REG0_THREADSIZE(threads) |
        A5XX_SP_FS_CTRL_REG0_HALFREGFOOTPRINT(f.k.maxHalfReg + 1) |
        A5XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(f.k.maxReg + 1) |
        A5XX_SP_FS_CTRL_REG0_BRANCHSTACK(f.k.branchstackHw));
    reg(s, REG_A5XX_HLSQ_UPDATE_CNTL, 0x020fffff);
    reg(s, REG_A5XX_VPC_GS_SIV_CNTL, 0xffff);
    reg(s, REG_A5XX_SP_SP_CNTL, 0x10);
    reg(s, REG_A5XX_GRAS_CNTL,
        (f.ijRegid[0] != kNoRegid ? A5XX_GRAS_CNTL_IJ_PERSP_PIXEL : 0) |
        (f.ijRegid[4] != kNoRegid ? A5XX_GRAS_CNTL_IJ_LINEAR_PIXEL : 0));
    const uint32_t rbControls[]{
        (f.ijRegid[0] != kNoRegid ? A5XX_RB_RENDER_CONTROL0_IJ_PERSP_PIXEL : 0) |
        (f.ijRegid[4] != kNoRegid ? A5XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL : 0), 0};
    regs(s, REG_A5XX_RB_RENDER_CONTROL0, rbControls);
    uint32_t colors[8];
    for (unsigned i = 0; i < 8; ++i)
        colors[i] = A5XX_SP_FS_OUTPUT_REG_REGID(i ? kNoRegid : f.colorRegid[0]);
    regs(s, REG_A5XX_SP_FS_OUTPUT_REG(0), colors);
    reg(s, REG_A5XX_VPC_PACK, A5XX_VPC_PACK_NUMNONPOSVAR(f.totalIn) | A5XX_VPC_PACK_PSIZELOC(255));
    const uint32_t modes[8]{};
    regs(s, REG_A5XX_VPC_VARYING_INTERP_MODE(0), modes);
    regs(s, REG_A5XX_VPC_VARYING_PS_REPL_MODE(0), modes);
    reg(s, REG_A5XX_GRAS_VS_CL_CNTL, 0);
    reg(s, REG_A5XX_VPC_CLIP_CNTL,
        A5XX_VPC_CLIP_CNTL_CLIP_DIST_03_LOC(255) | A5XX_VPC_CLIP_CNTL_CLIP_DIST_47_LOC(255));
    reg(s, REG_A5XX_PC_CLIP_CNTL, 0);
    const uint32_t fsLoad[]{CP_LOAD_STATE4_0_STATE_SRC(SS4_INDIRECT) |
        CP_LOAD_STATE4_0_STATE_BLOCK(SB4_FS_SHADER) | CP_LOAD_STATE4_0_NUM_UNIT(f.k.instrlen),
        uint32_t(images.fs.address), uint32_t(images.fs.address >> 32)};
    packet(s, CP_LOAD_STATE4, fsLoad);
    const uint32_t vfd[]{A5XX_VFD_CONTROL_1_REGID4VTX(x.vertexId) |
        A5XX_VFD_CONTROL_1_REGID4INST(x.instanceId) | 0xfc0000, 0xfcfc, 0xfcfc, 0xfc, 0};
    regs(s, REG_A5XX_VFD_CONTROL_1, vfd);
}

void constState(pm4::Stream& s, const Validated& x) {
    // fd5 common consts: direct uploads have one vec4 alignment; no 64-byte
    // UBO padding here. Source immediates come only from the owned vectors.
    if (!x.vs->immediates.empty() || x.vs->stage.k.driverParamsDwords ||
        !x.fs->immediates.empty()) s.waitForIdle();
    constants(s, SB4_VS_SHADER, x.vs->stage.k.immediatesOffsetVec4, x.vs->immediates);
    if (x.vs->stage.k.driverParamsDwords)
        constants(s, SB4_VS_SHADER, uint32_t(x.vs->stage.k.driverParamsOffsetVec4),
                  std::vector<uint32_t>(4, 0)); // draw_id, vtxid_base, instid_base, vtxcnt_max
    constants(s, SB4_FS_SHADER, x.fs->stage.k.immediatesOffsetVec4, x.fs->immediates);
}

bool append(pm4::Stream& stream, const pm4::Stream& added, size_t limit, std::string* why) {
    if (limit > kDefaultMaxWords || stream.size() > limit || added.size() > limit - stream.size())
        return fail(why, "A5xx program: bounded destination stream budget exceeded");
    pm4::Stream next = stream;
    for (size_t i = 0; i < added.size();) {
        const auto h = pm4::decode(added.words()[i]);
        if (h.kind == pm4::Header::kBad || h.count > added.size() - i - 1)
            return fail(why, "A5xx program: internal packet framing error");
        const auto* payload = added.words().data() + i + 1;
        if (h.kind == pm4::Header::kRegisters) next.registers(h.reg, payload, h.count);
        else if (!h.count) next.waitForIdle();
        else next.packet(h.opcode, payload, h.count);
        i += size_t(h.count) + 1;
    }
    stream = std::move(next);
    if (why) why->clear();
    return true;
}
} // namespace

bool encodeState(pm4::Stream& stream, const CompiledProgram& c, const Images& images,
                 std::string* why, size_t maxWords) {
    try {
        Validated x;
        if (!validate(c, images, &x, why)) return false;
        pm4::Stream added;
        state(added, x, images);
        constState(added, x);
        reg(added, REG_A5XX_VFD_CONTROL_0, 0);
        return append(stream, added, maxWords, why);
    } catch (const std::bad_alloc&) { return fail(why, "A5xx program: host allocation failed"); }
      catch (const std::length_error&) { return fail(why, "A5xx program: host allocation size failed"); }
}

bool drawTriangles(pm4::Stream& stream, const render_a5xx::SysmemPass& pass,
                   const CompiledProgram& c, const Images& images, uint32_t first, uint32_t count,
                   std::string* why, size_t maxWords) {
    if (!pass.active()) return fail(why, "A5xx program: draw requires active sysmem pass");
    if (first || !count || count % 3 || count > kMaxVertices)
        return fail(why, "A5xx program: bounded triangle list requires first=0 and nonzero count divisible by three");
    const auto& t = pass.target();
    if (!render_a5xx::validTarget(t, why)) return false;
    try {
        Validated x;
        if (!validate(c, images, &x, why)) return false;
        if (overlap(images.vs.address, images.vs.bytes, t.address, t.allocationBytes) ||
            overlap(images.fs.address, images.fs.bytes, t.address, t.allocationBytes))
            return fail(why, "A5xx program: shader image overlaps color allocation");
        pm4::Stream added;
        state(added, x, images);
        // fd5_emit.c's no-depth, one-color, non-blended triangle fill subset.
        reg(added, REG_A5XX_RB_RENDER_COMPONENTS, A5XX_RB_RENDER_COMPONENTS_RT0(15));
        reg(added, REG_A5XX_RB_FS_OUTPUT_CNTL, A5XX_RB_FS_OUTPUT_CNTL_MRT(1));
        reg(added, REG_A5XX_SP_FS_OUTPUT_CNTL, A5XX_SP_FS_OUTPUT_CNTL_MRT(1) |
            A5XX_SP_FS_OUTPUT_CNTL_DEPTH_REGID(kNoRegid) |
            A5XX_SP_FS_OUTPUT_CNTL_SAMPLEMASK_REGID(kNoRegid));
        reg(added, REG_A5XX_GRAS_SU_CNTL, A5XX_GRAS_SU_CNTL_LINEHALFWIDTH(0.5f) |
            A5XX_GRAS_SU_CNTL_LINE_MODE(BRESENHAM));
        reg(added, REG_A5XX_PC_RASTER_CNTL,
            A5XX_PC_RASTER_CNTL_POLYMODE_FRONT_PTYPE(PC_DRAW_TRIANGLES) |
            A5XX_PC_RASTER_CNTL_POLYMODE_BACK_PTYPE(PC_DRAW_TRIANGLES));
        reg(added, REG_A5XX_GRAS_CL_CNTL, A5XX_GRAS_CL_CNTL_ZERO_GB_SCALE_Z);
        reg(added, REG_A5XX_PC_PRIMITIVE_CNTL, A5XX_PC_PRIMITIVE_CNTL_STRIDE_IN_VPC(x.linkage.maxLoc));
        for (unsigned i = 0; i < 8; ++i) {
            reg(added, REG_A5XX_RB_MRT_CONTROL(i), A5XX_RB_MRT_CONTROL_ROP_CODE(ROP_COPY) |
                A5XX_RB_MRT_CONTROL_COMPONENT_ENABLE(i ? 0 : 15));
            reg(added, REG_A5XX_RB_MRT_BLEND_CONTROL(i),
                A5XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(FACTOR_ONE) |
                A5XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(FACTOR_ZERO) |
                A5XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(FACTOR_ONE) |
                A5XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(FACTOR_ZERO));
        }
        reg(added, REG_A5XX_SP_BLEND_CNTL, A5XX_SP_BLEND_CNTL_UNK8);
        reg(added, REG_A5XX_RB_BLEND_CNTL, A5XX_RB_BLEND_CNTL_SAMPLE_MASK(0xffff));
        reg(added, REG_A5XX_RB_DEPTH_CNTL, 0);
        reg(added, REG_A5XX_RB_DEPTH_PLANE_CNTL, 0);
        reg(added, REG_A5XX_GRAS_SU_DEPTH_PLANE_CNTL, 0);
        reg(added, REG_A5XX_RB_STENCIL_CONTROL, 0);
        reg(added, REG_A5XX_RB_ALPHA_CONTROL, 0);
        constState(added, x);
        reg(added, REG_A5XX_VFD_CONTROL_0, 0);
        const uint32_t indices[]{0, 0};
        regs(added, REG_A5XX_VFD_INDEX_OFFSET, indices);
        reg(added, REG_A5XX_PC_RESTART_INDEX, 0xffffffff);
        reg(added, REG_A5XX_RB_RENDER_CNTL, 8);
        reg(added, REG_A5XX_GRAS_SC_CNTL, 8);
        const uint32_t draw[]{DRAW4(DI_PT_TRILIST, DI_SRC_SEL_AUTO_INDEX, INDEX_SIZE_IGN, IGNORE_VISIBILITY), 1, count};
        packet(added, CP_DRAW_INDX_OFFSET, draw);
        return append(stream, added, maxWords, why);
    } catch (const std::bad_alloc&) { return fail(why, "A5xx program: host allocation failed"); }
      catch (const std::length_error&) { return fail(why, "A5xx program: host allocation size failed"); }
}

} // namespace hakios::metal::program_a5xx
