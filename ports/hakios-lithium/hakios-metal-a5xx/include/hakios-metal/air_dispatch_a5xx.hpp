// SPDX-License-Identifier: MIT
// CompiledKernel -> owned A5xx command fragment. HOST preparation only, no submit.
#pragma once

#include "hakios-metal/compiler.hpp"
#include "hakios-metal/compute_a5xx.hpp"
#include "hakios-metal/gpu.hpp"

namespace hakios::metal::air_dispatch_a5xx {

constexpr uint32_t kMaxArgumentSlots = 32;
constexpr uint32_t kMaxProgramBytes = 256 * 1024;
constexpr uint32_t kMaxStagingBytes = 512 * 16; // A5xx compute constant budget
constexpr uint32_t kMaxStreamDwords = 4096;

// Compiler-owned facts, never inferred from Kernel's register/layout names.
// Four exported fields at Kernel's end have these names and zero defaults.
struct Metadata {
    uint32_t compiledChipId = 0;
    uint32_t driverParamsDwords = 0;
    uint32_t subgroupSize = 0;
    bool a5xxBoundUboPushes = false;
};

// Builds on either schema: if the four compiler fields are not present, returns
// false rather than reading guessed offsets or selecting values for fill_id.
bool metadataFromKernel(const Kernel& kernel, Metadata* out, std::string* why = nullptr);

// Explicit caller-authorized byte range, kept by shared ownership while used.
// For an SSBO this must extend to Buffer::length: the fixed Binding API has no
// byte length, so a shorter view cannot be represented safely and is refused.
// For a constant argument this can be 4 bytes; the bridge stages its compiler
// push range in a fully zeroed 64-byte-aligned buffer, without reading padding.
struct Argument {
    // Keeps the shared GO/Gpu context owning Bo's raw Gpu& alive. Destroyed after
    // buffer. A Buffer shared_ptr alone does not retain that backing context.
    std::shared_ptr<const void> backingOwner;
    std::shared_ptr<const Buffer> buffer;
    uint64_t offset = 0;
    uint64_t sizeBytes = 0;
    Argument() = default;
    Argument(std::shared_ptr<const void> owner, std::shared_ptr<const Buffer> source,
             uint64_t offsetBytes, uint64_t lengthBytes);
    Argument(const Argument&) = default;
    Argument(Argument&& other) noexcept;
    Argument& operator=(Argument other) noexcept;
    void swap(Argument& other) noexcept;
};

// Injection point for the main agent's shared GO/BO ownership and CPU snapshot
// contract. This module never opens a GPU, queries a device, or submits work.
// Tests supply host memory exclusively. A future connected implementation owns
// CPU-read coherence and G4 admission; it must not be selected before that gate.
class Memory {
public:
    virtual ~Memory() = default;
    // Strong owner of the context needed by allocations' BO destructors.
    virtual std::shared_ptr<const void> backingOwner() const = 0;
    // Return a fresh, writable, CPU-mapped Buffer of exactly sizeBytes logical
    // bytes. GPU address must be 128-aligned for program, 64 for UBO, 4 for scratch.
    virtual std::shared_ptr<Buffer> allocate(uint64_t sizeBytes, std::string* why) = 0;
    // Called only for the validated logical bytes that intersect a pushed UBO
    // range. Must copy exactly sizeBytes after ensuring CPU-read readiness.
    virtual bool snapshot(const Buffer& source, uint64_t offset, uint64_t sizeBytes,
                          void* destination, std::string* why) = 0;
};

struct Resident {
    std::shared_ptr<const void> backingOwner;
    std::shared_ptr<const Buffer> buffer;
    bool write = false;
};

struct StagedUbo {
    uint32_t block = 0;
    uint64_t logicalBytes = 0;
    std::shared_ptr<const Buffer> buffer;
};

// Keep the whole Prepared alive until any FUTURE queue's completion. Residents
// retain every BO actually named by PM4 (program, SSBOs, UBO snapshots, scratch).
// The original constant BO need not survive because its authorized bytes were
// snapshotted. No GPU execution/result/fence state is fabricated here.
struct Prepared {
    // Declared first so it is destroyed after all allocated Buffer references.
    std::shared_ptr<const void> memoryOwner;
    pm4::Stream stream;
    std::vector<Resident> residents;
    std::vector<StagedUbo> stagedUbos;
    std::shared_ptr<const Buffer> program;
    std::shared_ptr<const Buffer> scratch;
    Metadata metadata;
    Prepared() = default;
    Prepared(const Prepared&) = default;
    Prepared(Prepared&& other) noexcept;
    Prepared& operator=(Prepared other) noexcept;
    void swap(Prepared& other) noexcept;
    // Raw references for the existing Gpu API, valid only while Prepared and its
    // residents stay alive. This method performs no transport operation.
    std::vector<BoUse> boUses() const;
};

// Production bridge: reads metadata exported by the actual compiler from
// Kernel. Existing schemas with missing fields are rejected atomically.
// Failure leaves *out unchanged; no allocation is leaked. No command submission.
bool prepare(const CompiledKernel& compiled, const compute_a5xx::Grid& grid,
             const std::vector<Argument>& arguments, Memory& memory,
             Prepared* out, std::string* why = nullptr);

// Explicit metadata interface for independent fixture tests, or an adapter that
// captured these four facts from a live compiler variant. It must not be used to
// invent metadata for a CompiledKernel whose compiler did not export it.
bool prepareWithMetadata(const CompiledKernel& compiled, const Metadata& metadata,
                         const compute_a5xx::Grid& grid,
                         const std::vector<Argument>& arguments, Memory& memory,
                         Prepared* out, std::string* why = nullptr);

} // namespace hakios::metal::air_dispatch_a5xx
