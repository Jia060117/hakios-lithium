// SPDX-License-Identifier: MIT
// A530 sampled graphics state; Mesa 25.0.7 fd5, never an A6xx fallback.
#pragma once
#include "hakios-metal/air_dispatch_a5xx.hpp"
#include "hakios-metal/link_a5xx.hpp"
#include "hakios-metal/texture_a5xx.hpp"

namespace hakios::metal::sampled_render_a5xx {
using BufferRef = air_dispatch_a5xx::Argument;
using Resident = air_dispatch_a5xx::Resident;
struct Images { BufferRef vertex, fragment; };
struct TextureBinding {
    uint32_t slot = 0; // Kernel::textures / AIR texture index, not sampler index
    a5xx::LinearTexture layout{};
    BufferRef storage;
};
struct SamplerBinding {
    uint32_t slot = 0; // bit in Kernel::samplerMask / AIR sampler index
    SamplerDesc descriptor{}; // portable Metal fields, NOT Sampler::words
};
struct StageBindings {
    std::vector<TextureBinding> textures;
    std::vector<SamplerBinding> samplers;
};
struct Bindings { StageBindings vertex, fragment; };

// Descriptor words are copied into PM4; images and sampled pixels stay resident
// until the eventual fence. This covers only this module's READ references.
// Backend must also retain the pass's writable target/scratch/command BOs.
struct Prepared {
    pm4::Stream stream; // emitted fragment, not a copy of the preceding pass
    std::vector<Resident> residents;
    Prepared() = default;
    Prepared(const Prepared&) = default;
    Prepared(Prepared&&) noexcept = default;
    Prepared& operator=(Prepared other) noexcept;
    void swap(Prepared& other) noexcept;
    std::vector<BoUse> boUses() const;
};

// Exact fd5 sampler subset: nearest/linear min/mag, clamp-to-edge in all
// dimensions, one mip, anisotropy1, no comparison/border. Pixel coordinates
// are supported. The fd5 nonmip MAX_LOD is min(lodMaxClamp, 0.125), NOT the
// A6xx MIPFILTER_LINEAR_FAR bit. Invalid input leaves *words unchanged.
bool samplerDescriptor(const SamplerDesc& descriptor,
                       std::array<uint32_t, 4>* words,
                       std::string* why = nullptr);

class Program final {
public:
    // Owning snapshots of the actual compiler stages. Varying proof and all
    // non-texture restrictions are checked by the existing fd5 linker. The
    // shader binaries, resource indices, register IDs and varying slots are
    // NEVER remapped. Uploaded shader bytes must equal their owned binaries;
    // caller supplies that upload/publication fact, as with link_a5xx.
    static std::unique_ptr<Program> link(const CompiledStage& vertex,
        const CompiledStage& fragment, const Images& images,
        link_a5xx::ColorFormat color, link_a5xx::VaryingInterface varyingProof,
        std::string* why = nullptr);
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    const CompiledStage& vertex() const { return stages_[0]; }
    const CompiledStage& fragment() const { return stages_[1]; }
    const Linkage& linkage() const { return linked_->linkage(); }

    bool encodeState(const Bindings& bindings, Prepared* out,
        std::string* why = nullptr, size_t maxWords = link_a5xx::kDefaultMaxWords) const;

    // Same triangle/sysmem subset as link_a5xx. Resources are inserted BEFORE
    // the draw after program/raster state. Input stream and *out are unchanged
    // on failure. scratch MUST be the same full writable allocation used for
    // pass.begin: its range is needed to reject sampled/shader alias hazards.
    // Pass cannot expose its private scratch; caller's pairing is a contract.
    bool drawTriangles(pm4::Stream& stream, const render_a5xx::SysmemPass& pass,
        const BufferRef& scratch, const Bindings& bindings,
        uint32_t firstVertex, uint32_t vertexCount, Prepared* out,
        std::string* why = nullptr, size_t maxWords = link_a5xx::kDefaultMaxWords) const;
private:
    Program() = default;
    bool resources(const Bindings&, Prepared*, std::string*) const;
    // Context owners are declared before (and destroyed after) their BOs.
    Images images_;
    CompiledStage stages_[2];
    std::string sources_[2];
    std::unique_ptr<link_a5xx::Program> linked_;
};
} // namespace hakios::metal::sampled_render_a5xx
