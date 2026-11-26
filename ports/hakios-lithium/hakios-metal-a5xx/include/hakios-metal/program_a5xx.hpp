// SPDX-License-Identifier: MIT
// Bounded A530 VS/FS sysmem state, ported from Mesa 25.0.7 fd5_program.c.
// Host encoding only: no device, allocation, transport, submit or shader execution.
#pragma once
#include "hakios-metal/compiler.hpp"
#include "hakios-metal/render_a5xx.hpp"

namespace hakios::metal::program_a5xx {

constexpr size_t kDefaultMaxWords = 4096;
constexpr uint32_t kMaxVertices = 65535; // software subset, not a measured limit

// Caller uploads the EXACT owned CompiledStage::words here, in a single address
// space, and keeps both BOs resident/readable through any future completion.
// Addresses are 128-aligned, nonzero, below 2^49. This module cannot inspect the
// actual mapping, its contents, cache readiness or ownership.
struct Image { uint64_t address = 0, bytes = 0; };
struct Images { Image vs, fs; };

// Uses compiler-produced stages (compiled.vs/fs) and verifies their A530 tags,
// raw input/output tables and the supplied linked Program::link. Kernel raw
// words/immediates pointers and Program's copied Stage snapshots are not used.
// Program's samples, vertex descriptor and blend state remain authoritative.
//
// Subset: both shaders fit the 64-unit instruction buffer; one float color0;
// no vertex fetch, resources, UBOs, textures, blend, depth, kill, sample/centroid
// barycentrics, flat/half varyings, clip/cull, streamout, tessellation/geometry.
// VS allows only ZERO_BASE vertex ID and instance ID from raw inputs. FS
// allows smooth generic varyings and perspective/linear pixel barycentrics.
// VS driver params cover the first vec4 of ir3_driver_params_vs; single draw,
// firstVertex=0, one instance, no streamout. Unsupported metadata is refused.
//
// Appends ONLY fd5 program/linkage/VFD state plus owned immediates/VS params.
// Does not start a pass or issue a draw. Failure leaves stream unchanged.
bool encodeState(pm4::Stream& stream, const CompiledProgram& compiled,
                 const Images& images, std::string* why = nullptr,
                 size_t maxWords = kDefaultMaxWords);

// Complete supported draw fragment inside an already begun SysmemPass:
// program state, no-fetch VFD, one MRT output, blend/depth off, fill triangles,
// half-Z clipping and CP_DRAW_INDX_OFFSET auto-index. Viewport/scissor/target/
// restore/CCU events belong to SysmemPass. No attachment clear is implicit.
// stream MUST be the same stream owned by pass, with no concurrent mutation.
// The caller also keeps color, scratch, shader and command BOs disjoint/live.
// Shader/color overlap is checked; scratch/command BOs are not exposed by pass.
//
// Existing SysmemPass::drawTriangles(first,count) remains a refusal; the
// concrete overload patch in evidence/.../program/integration.patch connects
// this entry point without altering shared files in this task.
bool drawTriangles(pm4::Stream& stream, const render_a5xx::SysmemPass& pass,
                   const CompiledProgram& compiled, const Images& images,
                   uint32_t firstVertex, uint32_t vertexCount,
                   std::string* why = nullptr,
                   size_t maxWords = kDefaultMaxWords);

} // namespace hakios::metal::program_a5xx
