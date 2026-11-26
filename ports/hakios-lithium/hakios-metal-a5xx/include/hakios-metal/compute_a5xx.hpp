// SPDX-License-Identifier: MIT
// Standalone HOST-ONLY A530 command encoder. No allocation, submission or MMIO.
#pragma once

#include "hakios-metal/compute.hpp" // fixed Kernel / Bindings API; no A6xx encoder is called

namespace hakios::metal::compute_a5xx {

// Kernel has no target tag. The integrator MUST supply the target used by ir3
// when assembling words, and upload those exact words to address. gpuId=530 is
// an explicit caller contract, not inspection/proof of the shader binary.
// In particular, tests/fixtures/fill_id.hpp is gen6 and cannot be used as-is.
struct Program {
    uint64_t address = 0;
    uint64_t sizeBytes = 0;
    uint32_t gpuId = 0;
};

struct Grid {
    uint32_t groups[3] = {1, 1, 1};
    uint32_t local[3] = {1, 1, 1};
    uint32_t workDim = 3;
};

// Optional replacement of the kernel's immediates, before address/numwg patches.
// The count must equal Kernel::numImmediates; padding is made by the encoder.
struct Immediates {
    const uint32_t* words = nullptr;
    uint32_t count = 0;
};

// Encodes the fd5_compute.c fixed-local-size, text-kernel, buffer-only subset:
// Kernel::numBufs/bufs, immediates, constRegid and numwgConstRegid.
// All other resource layouts/features are refused with a reason, before changing
// stream. Workgroup ID uses r48-r55 shared registers on A5xx as well:
// ir3_parser.y:846-851, ir3.h:1526-1533. Local ID uses ordinary registers.
// A6xx merged footprints must not be translated by name.
//
// flushAddress names a caller-owned, GPU-writable, mapped 4-byte scratch word,
// kept resident until completion, outside program and every bound buffer.
// All program/data addresses must be mapped in the same GPU address space.
// The caller owns the A5xx context restore, BO residency, CPU/GPU coherency and
// eventual fence. This fragment does not implement fd5_emit_restore or a queue.
//
// true means ONLY that a source-backed command fragment was encoded on the host.
// It proves neither shader execution nor visibility to the CPU. No existing
// fake GPU is used as a shader interpreter. Do not route a device to this module
// on the strength of M0 packet tests.
bool encode(pm4::Stream& stream, const Kernel& kernel, const Grid& grid,
            const Bindings& bindings, const Program& program,
            uint64_t flushAddress, std::string* why = nullptr,
            const Immediates& immediates = {});

// Metadata omitted by the fixed Kernel API; copy from the ACTUAL A530 variant,
// not from the threadsize name: ir3_const_state(v)->num_driver_params and
// v->info.subgroup_size. Mesa 25.0.7 ir3.c:469-470 still sets subgroup_size to
// 64/128 on earlier gens (its TODO), while A530 threadsize_base is 32.
struct AirLayout {
    uint32_t numDriverParams = 0;
    uint32_t subgroupSize = 0;
    // Verify every exported KernelUboRange was a non-bindless application UBO
    // range, not a preamble/driver/constant-data range. compiler.cpp currently
    // drops r.ubo.bindless and the push type, so this is an explicit contract.
    bool boundUboPushes = false;
};

// Additional source-backed subset: AIR SSBOs (<=24), all UBO reads pushed into
// constants (numUbos==0), direct driver params, variable local size. Unpushed
// UBO pointers still require IR3_CONST_ALLOC_UBO_PTRS metadata and are refused.
// A5xx pushes use fd5_emit_const_bo: destination/count multiples of 4 vec4.
// Each bound UBO range must cover the FULL compiler-aligned range, even if the
// logical AIR argument was 4 bytes and the range is 64. Caller supplies a real
// 64-aligned, zero-padded staging Buffer; no access beyond Buffer::length.
// Bindings indices are the shader's SSBO/UBO indices, not a separate IBO table.
bool encodeAir(pm4::Stream& stream, const Kernel& kernel, const Grid& grid,
               const Bindings& bindings, const Program& program,
               uint64_t flushAddress, const AirLayout& layout,
               std::string* why = nullptr, const Immediates& immediates = {});

} // namespace hakios::metal::compute_a5xx
