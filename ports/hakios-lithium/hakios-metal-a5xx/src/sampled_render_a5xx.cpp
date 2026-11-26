// SPDX-License-Identifier: MIT
// Fixed Mesa 25.0.7: fd5_texture.c:55-111 (sampler), fd5_emit.c:314-394,
// 825-843 (12/4-dword direct tables/counts), fd5_emit.h:93-105 (UCHE+WFI),
// fd5_program.c:391-405,547-557 (PIXLODENABLE). Exact hashes/anchors in
// evidence/gpu-metal-20261126/a5xx-sampled-render/source-anchors.json.
#include "hakios-metal/sampled_render_a5xx.hpp"
#include "hakios-metal/command.hpp"
#include "hakios-metal/a5xx.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

namespace hakios::metal::sampled_render_a5xx {
namespace {
namespace hw = hakios::metal::a5xx;
constexpr uint64_t kLimit = uint64_t(1) << 49;
bool fail(std::string* why, const char* text) { if (why) *why = text; return false; }
bool owner(const BufferRef& a, const BufferRef& b) {
    return a.backingOwner && b.backingOwner &&
        !a.backingOwner.owner_before(b.backingOwner) && !b.backingOwner.owner_before(a.backingOwner);
}
bool valid(const BufferRef& r, uint64_t alignment, uint64_t needed) {
    if (!r.backingOwner || !r.buffer || !r.sizeBytes || r.sizeBytes < needed) return false;
    const auto& b = *r.buffer;
    return b.length() && b.length() <= b.bo().size() && b.gpuAddress() &&
        b.gpuAddress() < kLimit && b.bo().size() <= kLimit - b.gpuAddress() &&
        r.offset <= b.length() && r.sizeBytes <= b.length() - r.offset &&
        (b.gpuAddress() + r.offset) % alignment == 0;
}
uint64_t address(const BufferRef& r) { return r.buffer->gpuAddress() + r.offset; }
bool overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    return a < b + bn && b < a + an;
}
unsigned span(uint32_t mask) {
    unsigned n = 0; while (mask) { ++n; mask >>= 1; } return n;
}
void reg(pm4::Stream& s, uint32_t r, uint32_t v) { s.registers(r, &v, 1); }
void keep(Prepared& p, const BufferRef& r) {
    for (const auto& old : p.residents) if (&old.buffer->bo() == &r.buffer->bo()) return;
    p.residents.push_back({r.backingOwner, r.buffer, false});
}

// Only sampled metadata is delegated to THIS module. No buffer/private/etc.
// fields are cleared: the existing link emitter continues to reject them.
// The projection exists solely to reuse its verified numeric VPC/program
// emission; the immutable original metadata remains authoritative below.
bool project(const CompiledStage& c, CompiledStage* projected, std::string* why) {
    const auto& k = c.stage.k;
    if (!c.inlineSamplers.empty())
        return fail(why, "A530 sampled: compiler inline sampler stores legacy encoded words; portable constexpr state is required");
    if (c.resources.size() > 2 * kMaxTextureSlots || k.numTextureSlots > kMaxTextureSlots ||
        (k.samplerMask >> kMaxTextureSlots) || c.stage.needPixlod > 1 || c.stage.needFullQuad > 1)
        return fail(why, "A530 sampled: out-of-range sampled metadata");
    bool textures[kMaxTextureSlots]{}, samplers[kMaxTextureSlots]{};
    for (const auto& r : c.resources) {
        if (r.index >= kMaxTextureSlots || r.argbufSet != ~0u || r.inlineSampler ||
            r.write || r.irWrite || r.nullTexture)
            return fail(why, "A530 sampled: unsupported writable/null/argument-buffer/inline resource");
        if (r.kind == Resource::kTexture) {
            if (textures[r.index] || r.textureDim != kTexDim2D || r.textureArray || r.element != kElemFloat)
                return fail(why, "A530 sampled: texture reflection must describe unique float 2D slots");
            textures[r.index] = true;
        } else if (r.kind == Resource::kSampler) {
            if (samplers[r.index]) return fail(why, "A530 sampled: duplicate sampler reflection");
            samplers[r.index] = true;
        } else return fail(why, "A530 sampled: only sampled texture and sampler resources supported");
    }
    uint32_t highest = 0;
    for (unsigned i = 0; i < kMaxTextureSlots; ++i) {
        const auto& t = k.textures[i];
        if (t.kind == kTexNone) {
            if (t.dim || t.isArray || t.element || t.bits || t.read || t.write || t.isNull || t.fbTarget)
                return fail(why, "A530 sampled: hidden metadata in empty texture slot");
        } else {
            if (!textures[i] || t.kind != kTexSampled || t.dim != kTexDim2D ||
                t.isArray || t.element != kElemFloat || t.bits || t.write || t.isNull || t.fbTarget || t.read > 1)
                return fail(why, "A530 sampled: instruction/reflection shape or access mismatch");
            highest = i + 1;
        }
        if ((k.samplerMask & (1u << i)) && !samplers[i])
            return fail(why, "A530 sampled: active sampler lacks compiler reflection");
    }
    if (k.numTextureSlots != highest || k.numSamp != std::max(highest, span(k.samplerMask)) ||
        (!highest && k.samplerMask))
        return fail(why, "A530 sampled: compiler table extents disagree; slot inference refused");
    *projected = c;
    projected->resources.clear();
    auto& pk = projected->stage.k;
    pk.numTextureSlots = pk.samplerMask = pk.numSamp = 0;
    std::fill(std::begin(pk.textures), std::end(pk.textures), KernelTexture{});
    projected->stage.needPixlod = projected->stage.needFullQuad = 0;
    // fd5_program consumes need_pixlod; it does NOT consume need_full_quad
    // or emit an A6xx LODPIXMASK field. Do not transplant that sequence.
    return true;
}

bool copyPackets(pm4::Stream& to, const pm4::Stream& from, size_t start, size_t end,
                 const CompiledStage* stages, const pm4::Stream* beforeDraw,
                 unsigned* draws, std::string* why) {
    for (size_t i = start; i < end;) {
        const auto h = pm4::decode(from.words()[i]);
        if (h.kind == pm4::Header::kBad || h.count > end - i - 1)
            return fail(why, "A530 sampled: internal packet framing failure");
        const auto* data = from.words().data() + i + 1;
        if (h.kind == pm4::Header::kRegisters) {
            std::vector<uint32_t> words(data, data + h.count);
            if (stages) for (unsigned n = 0; n < h.count; ++n) {
                if (h.reg + n == hw::REG_A5XX_SP_VS_CTRL_REG0 && stages[0].stage.needPixlod)
                    words[n] |= hw::A5XX_SP_VS_CTRL_REG0_PIXLODENABLE;
                if (h.reg + n == hw::REG_A5XX_SP_FS_CTRL_REG0 && stages[1].stage.needPixlod)
                    words[n] |= hw::A5XX_SP_FS_CTRL_REG0_PIXLODENABLE;
            }
            to.registers(h.reg, words.data(), h.count);
        } else {
            if (beforeDraw && h.opcode == hw::CP_DRAW_INDX_OFFSET) {
                if (!copyPackets(to, *beforeDraw, 0, beforeDraw->size(), nullptr, nullptr, nullptr, why)) return false;
                ++*draws;
            }
            if (!h.count && h.opcode == pm4::kWaitForIdle) to.waitForIdle();
            else to.packet(h.opcode, data, h.count);
        }
        i += size_t(h.count) + 1;
    }
    return true;
}
bool budget(size_t n, size_t limit, std::string* why) {
    return (limit <= link_a5xx::kDefaultMaxWords && n <= limit) ||
        fail(why, "A530 sampled: complete destination word budget exceeded");
}
void table(pm4::Stream& stream, hw::a4xx_state_block block, hw::a4xx_state_type type,
           uint32_t count, const std::vector<uint32_t>& data) {
    if (!count) return;
    std::vector<uint32_t> p{hw::CP_LOAD_STATE4_0_STATE_BLOCK(block) |
        hw::CP_LOAD_STATE4_0_STATE_SRC(hw::SS4_DIRECT) | hw::CP_LOAD_STATE4_0_NUM_UNIT(count),
        hw::CP_LOAD_STATE4_1_STATE_TYPE(type), 0};
    p.insert(p.end(), data.begin(), data.end());
    stream.packet(hw::CP_LOAD_STATE4, p.data(), uint32_t(p.size()));
}
} // namespace

Prepared& Prepared::operator=(Prepared other) noexcept { swap(other); return *this; }
void Prepared::swap(Prepared& other) noexcept {
    using std::swap; swap(stream, other.stream); residents.swap(other.residents);
}
std::vector<BoUse> Prepared::boUses() const {
    std::vector<BoUse> out;
    for (const auto& r : residents) out.push_back({&r.buffer->bo(), false});
    return out;
}

bool samplerDescriptor(const SamplerDesc& d, std::array<uint32_t, 4>* words, std::string* why) {
    if (!words || d.minFilter > 1 || d.magFilter > 1 || d.mipFilter ||
        d.sAddress || d.tAddress || d.rAddress || d.maxAnisotropy != 1 ||
        d.compareFunction || d.borderColor || !std::isfinite(d.lodMinClamp) ||
        !std::isfinite(d.lodMaxClamp) || d.lodMinClamp < 0 || d.lodMaxClamp < d.lodMinClamp)
        return fail(why, "A530 sampled: nearest/linear clamp-to-edge nonmip sampler required");
    const std::array<uint32_t, 4> next{{
        hw::A5XX_TEX_SAMP_0_XY_MAG(d.magFilter ? hw::A5XX_TEX_LINEAR : hw::A5XX_TEX_NEAREST) |
        hw::A5XX_TEX_SAMP_0_XY_MIN(d.minFilter ? hw::A5XX_TEX_LINEAR : hw::A5XX_TEX_NEAREST) |
        hw::A5XX_TEX_SAMP_0_WRAP_S(hw::A5XX_TEX_CLAMP_TO_EDGE) |
        hw::A5XX_TEX_SAMP_0_WRAP_T(hw::A5XX_TEX_CLAMP_TO_EDGE) |
        hw::A5XX_TEX_SAMP_0_WRAP_R(hw::A5XX_TEX_CLAMP_TO_EDGE),
        hw::A5XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF |
        (!d.normalizedCoordinates ? hw::A5XX_TEX_SAMP_1_UNNORM_COORDS : 0) |
        hw::A5XX_TEX_SAMP_1_MIN_LOD(std::min(d.lodMinClamp, 0.125f)) |
        hw::A5XX_TEX_SAMP_1_MAX_LOD(std::min(d.lodMaxClamp, 0.125f)), 0, 0}};
    *words = next;
    if (why) why->clear();
    return true;
}

std::unique_ptr<Program> Program::link(const CompiledStage& vs, const CompiledStage& fs,
    const Images& images, link_a5xx::ColorFormat color, link_a5xx::VaryingInterface proof,
    std::string* why) {
    try {
        if (!valid(images.vertex, 128, uint64_t(vs.words.size()) * 4) ||
            !valid(images.fragment, 128, uint64_t(fs.words.size()) * 4) || !owner(images.vertex, images.fragment)) {
            fail(why, "A530 sampled: bounded resident shader ranges from one context required"); return nullptr;
        }
        CompiledStage projected[2];
        if (!project(vs, &projected[0], why) || !project(fs, &projected[1], why)) return nullptr;
        link_a5xx::Images raw{{address(images.vertex), images.vertex.sizeBytes},
                              {address(images.fragment), images.fragment.sizeBytes}};
        auto linked = link_a5xx::Program::link(projected[0], projected[1], raw, color, proof, why);
        if (!linked) return nullptr;
        std::unique_ptr<Program> p(new Program);
        p->images_ = images;
        p->stages_[0] = vs; p->stages_[1] = fs;
        for (unsigned i = 0; i < 2; ++i) {
            auto& c = p->stages_[i];
            p->sources_[i] = c.stage.k.source ? c.stage.k.source : "";
            c.stage.k.words = c.words.data(); c.stage.k.immediates = c.immediates.data();
            c.stage.k.name = c.name.c_str(); c.stage.k.source = p->sources_[i].c_str();
        }
        p->linked_ = std::move(linked);
        return p;
    } catch (const std::bad_alloc&) { fail(why, "A530 sampled: allocation failed"); }
      catch (const std::length_error&) { fail(why, "A530 sampled: allocation extent failed"); }
    return nullptr;
}

bool Program::resources(const Bindings& bindings, Prepared* p, std::string* why) const {
    using namespace hw;
    keep(*p, images_.vertex); keep(*p, images_.fragment);
    const bool active = stages_[0].stage.k.numTextureSlots || stages_[1].stage.k.numTextureSlots;
    if (active) {
        // fd5_cache_flush, verbatim values; this is NOT CPU cache publication
        // and does not replace a preceding render pass's CCU/timestamp tail.
        const uint32_t flush[]{0, 0, 0, 0, 0x12};
        p->stream.registers(REG_A5XX_UCHE_CACHE_INVALIDATE_MIN_LO, flush, 5);
        p->stream.waitForIdle();
    }
    const StageBindings* input[]{&bindings.vertex, &bindings.fragment};
    for (unsigned n = 0; n < 2; ++n) {
        const auto& b = *input[n]; const auto& k = stages_[n].stage.k;
        if (b.textures.size() > kMaxTextureSlots || b.samplers.size() > kMaxTextureSlots)
            return fail(why, "A530 sampled: too many stage bindings");
        const TextureBinding* textures[kMaxTextureSlots]{};
        const SamplerBinding* samplers[kMaxTextureSlots]{};
        for (const auto& t : b.textures) {
            if (t.slot >= kMaxTextureSlots || textures[t.slot] || k.textures[t.slot].kind != kTexSampled)
                return fail(why, "A530 sampled: duplicate/unused/out-of-range texture binding");
            textures[t.slot] = &t;
        }
        for (const auto& s : b.samplers) {
            if (s.slot >= kMaxTextureSlots || samplers[s.slot] || !(k.samplerMask & (1u << s.slot)))
                return fail(why, "A530 sampled: duplicate/unused/out-of-range sampler binding");
            samplers[s.slot] = &s;
        }
        const unsigned ns = k.numTextureSlots ? std::max(1u, span(k.samplerMask)) : 0;
        std::vector<uint32_t> tw(size_t(k.numTextureSlots) * 12, 0), sw(size_t(ns) * 4, 0);
        for (unsigned i = 0; i < k.numTextureSlots; ++i) {
            if (k.textures[i].kind == kTexNone) continue; // exact sparse holes, no compacting
            const auto* t = textures[i];
            if (!t || !valid(t->storage, 32, t->layout.layerBytes) || !owner(images_.vertex, t->storage))
                return fail(why, "A530 sampled: missing texture or invalid resident logical extent/context");
            if (t->layout.format != LinearFormat::RGBA8 && t->layout.format != LinearFormat::BGRA8)
                return fail(why, "A530 sampled: only linear RGBA8/BGRA8 UNORM sampled textures supported");
            std::array<uint32_t, 12> desc{};
            if (!textureDescriptor(t->layout, address(t->storage), t->storage.sizeBytes, &desc, why)) return false;
            std::copy(desc.begin(), desc.end(), tw.begin() + size_t(i) * 12);
            keep(*p, t->storage);
        }
        for (unsigned i = 0; i < ns; ++i) if (k.samplerMask & (1u << i)) {
            if (!samplers[i]) return fail(why, "A530 sampled: missing exact compiler sampler slot");
            std::array<uint32_t, 4> desc{};
            if (!samplerDescriptor(samplers[i]->descriptor, &desc, why)) return false;
            std::copy(desc.begin(), desc.end(), sw.begin() + size_t(i) * 4);
        }
        // isaml's unused s#0 and sparse holes deliberately use fd5's zero dummy.
        const auto block = n ? SB4_FS_TEX : SB4_VS_TEX;
        table(p->stream, block, ST4_SHADER, ns, sw);
        table(p->stream, block, ST4_CONSTANTS, k.numTextureSlots, tw);
        reg(p->stream, n ? REG_A5XX_TPL1_FS_TEX_COUNT : REG_A5XX_TPL1_VS_TEX_COUNT, k.numTextureSlots);
    }
    reg(p->stream, REG_A5XX_TPL1_CS_TEX_COUNT, 0);
    return true;
}

bool Program::encodeState(const Bindings& b, Prepared* out, std::string* why, size_t maxWords) const {
    if (!out) return fail(why, "A530 sampled: retained output required");
    try {
        Prepared next, res; pm4::Stream base;
        if (!linked_->encodeState(base, why, maxWords) || !resources(b, &res, why) ||
            !copyPackets(next.stream, base, 0, base.size(), stages_, nullptr, nullptr, why) ||
            !copyPackets(next.stream, res.stream, 0, res.stream.size(), nullptr, nullptr, nullptr, why) ||
            !budget(next.stream.size(), maxWords, why)) return false;
        next.residents.swap(res.residents); out->swap(next);
        if (why) why->clear(); return true;
    } catch (const std::bad_alloc&) { return fail(why, "A530 sampled: allocation failed"); }
      catch (const std::length_error&) { return fail(why, "A530 sampled: allocation extent failed"); }
}

bool Program::drawTriangles(pm4::Stream& stream, const render_a5xx::SysmemPass& pass,
    const BufferRef& scratch, const Bindings& b, uint32_t first, uint32_t count,
    Prepared* out, std::string* why, size_t maxWords) const {
    if (!out || &out->stream == &stream) return fail(why, "A530 sampled: distinct retained fragment output required");
    try {
        Prepared next, res; pm4::Stream base = stream;
        if (!linked_->drawTriangles(base, pass, first, count, why, maxWords) ||
            !valid(scratch, 4, 4) || !owner(images_.vertex, scratch))
            return fail(why, "A530 sampled: invalid draw/pass or scratch resident context");
        if (!resources(b, &res, why)) return false;
        const auto& target = pass.target();
        for (const auto& r : res.residents) {
            const auto& bo = r.buffer->bo();
            if (overlaps(bo.gpuAddress(), bo.size(), target.address, target.allocationBytes) ||
                overlaps(bo.gpuAddress(), bo.size(), scratch.buffer->bo().gpuAddress(), scratch.buffer->bo().size()))
                return fail(why, "A530 sampled: sampled/shader BO aliases writable color or scratch allocation");
        }
        unsigned draws = 0;
        if (!copyPackets(next.stream, base, stream.size(), base.size(), stages_, &res.stream, &draws, why) || draws != 1 ||
            stream.size() > maxWords || !budget(stream.size() + next.stream.size(), maxWords, why)) return false;
        pm4::Stream complete = stream;
        if (!copyPackets(complete, next.stream, 0, next.stream.size(), nullptr, nullptr, nullptr, why)) return false;
        next.residents.swap(res.residents);
        stream = std::move(complete); out->swap(next);
        if (why) why->clear(); return true;
    } catch (const std::bad_alloc&) { return fail(why, "A530 sampled: allocation failed"); }
      catch (const std::length_error&) { return fail(why, "A530 sampled: allocation extent failed"); }
}
} // namespace hakios::metal::sampled_render_a5xx
