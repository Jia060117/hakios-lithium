// SPDX-License-Identifier: MIT
// A530 graphics link + fd5 program emission, using actual compileStage output.
#pragma once

#include "hakios-metal/compiler.hpp"
#include "hakios-metal/render_a5xx.hpp"

namespace hakios::metal::link_a5xx {

constexpr size_t kDefaultMaxWords = 4096;
constexpr uint32_t kMaxVertices = 65535; // bounded software subset
struct Image { uint64_t address = 0, bytes = 0; };
struct Images { Image vs, fs; };
using ColorFormat = render_a5xx::ColorFormat;
enum class VaryingInterface {
    // Caller guarantees the binaries were compiled with compatible slots.
    // This is the fd5/ir3 numerical-slot contract; no by-name claim is made.
    ExplicitSlots,
    // Require both compiler varyingNames tables to match at active slots.
    // Current compileStage has no FS mangled names: this mode refuses them.
    MatchingNames,
};

// True when this TU uses Mesa's actual ir3_link_shaders/ir3_link_add. Without
// HAKIOS_METAL_MESA it reuses the checked Stage port in src/link.cpp.
bool usesMesaLinker() noexcept;

// An immutable, owning snapshot: changing/destroying the input CompiledStages
// cannot invalidate its words, constants, names or link. It does not allocate
// GPU memory. Upload vertex().words / fragment().words EXACTLY to images(),
// then keep those mappings readable/resident until eventual GPU completion.
// IOVAs and uploads are the caller's facts, not verified by host encoding.
//
// Scope is a bounded fd5 sysmem subset: no vertex fetch, texture,
// buffer, depth, blend or MSAA; float color0; smooth full-width varyings;
// ZERO_BASE vertex ID is read from the raw compiler input table. This preserves
// compileStage's convenience vertexIdRegid (currently absent for ZERO_BASE).
// CompiledKernel alone cannot link: it has no stage kind, input/output tables,
// totalIn, position/color regids, ij regids or varying interface names.
class Program final {
public:
    // VS and FS must already use the SAME slot assignment for each varying.
    // Caller explicitly selects the proof: coordinated numerical slots, or
    // matching compiler names. No inferred/repaired names or slot remapping.
    // Fails before returning an object if any emitter prerequisite is absent.
    static std::unique_ptr<Program> link(const CompiledStage& vs,
                                         const CompiledStage& fs,
                                         const Images& images,
                                         ColorFormat color,
                                         VaryingInterface varyingInterface,
                                         std::string* why = nullptr);

    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    Program(Program&&) = delete;
    Program& operator=(Program&&) = delete;

    const CompiledStage& vertex() const { return *compiled_.vs; }
    const CompiledStage& fragment() const { return *compiled_.fs; }
    const Linkage& linkage() const { return compiled_.program.link; }
    const Images& images() const { return images_; }
    ColorFormat colorFormat() const { return color_; }

    // Appends A5xx shader loads, constants, HLSQ/SP/VPC/FS/VFD state
    // from fd5_program.c. Does not issue a draw or begin a pass.
    // maxWords bounds the ENTIRE destination stream (currently <= 4096).
    bool encodeState(pm4::Stream& stream, std::string* why = nullptr,
                     size_t maxWords = kDefaultMaxWords) const;

    // Callable replacement for the missing-state refusal: inside an active
    // SysmemPass, use linked->drawTriangles(stream, pass, 0, 3, &why).
    // Emits program + fixed raster/MRT state + CP_DRAW_INDX_OFFSET. The stream
    // must be pass's stream. Target format must match the link. first=0, one
    // instance, triangle list only. Call pass.end() afterwards for the tail.
    // GPU scratch/command residency and non-aliasing remain caller-owned.
    // Choose maxWords no greater than the pass's own destination budget.
    // A false result leaves stream unchanged. No device submit/execution here.
    bool drawTriangles(pm4::Stream& stream, const render_a5xx::SysmemPass& pass,
                       uint32_t firstVertex, uint32_t vertexCount,
                       std::string* why = nullptr,
                       size_t maxWords = kDefaultMaxWords) const;

private:
    Program() = default;
    CompiledProgram compiled_{};
    Images images_{};
    ColorFormat color_ = ColorFormat::RGBA8Unorm;
    std::string source_[2];
};

} // namespace hakios::metal::link_a5xx
