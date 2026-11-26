#pragma once
#include "hakios_gpu_abi.h"

namespace hakios::gpu_abi {
enum class Status { shape_valid, malformed, unknown_selector, unsupported };
struct Result { Status status; const char* reason; };

/* Local schema results, NOT IOReturn values or proof of hardware execution. */
inline Result validate_record(uint32_t selector, const void* input, size_t bytes) {
    const size_t expected = hakios_gpu_record_size(selector);
    if (!expected) return {Status::unknown_selector, "selector absent from pinned ABI"};
    if (!input || bytes != expected) return {Status::malformed, "canonical record size required"};
    return {Status::shape_valid, "host record shape only"};
}

/* Explicit host views: never follow the user VA fields bos/cmds/relocs. Sizes
 * are caller-supplied fixture metadata, not GEM/SMMU ownership verification. */
inline Result validate_submit(const HakiosGpuSubmit& r,
                              const HakiosGpuSubmitBo* bos, size_t bo_count,
                              const HakiosGpuSubmitCmd* cmds, size_t cmd_count,
                              const uint64_t* bo_bytes) {
    if (!r.nr_bos || r.nr_bos > HAKIOS_GPU_MAX_BUFFERS ||
        !r.nr_cmds || r.nr_cmds > HAKIOS_GPU_MAX_COMMANDS ||
        bo_count != r.nr_bos || cmd_count != r.nr_cmds || !bos || !cmds || !bo_bytes)
        return {Status::malformed, "invalid counts or missing resolved host view"};
    for (size_t i = 0; i < cmd_count; ++i) {
        const auto& c = cmds[i];
        /* Strict checker profile: kernel skips non-CMD_BUF types instead. */
        if (c.type != HAKIOS_GPU_SUBMIT_COMMAND_BUFFER || c.nr_relocs ||
            c.submit_idx >= bo_count || (c.size & 3u) || (c.submit_offset & 3u))
            return {Status::malformed, "command type, relocation, index or alignment"};
        if (!bos[c.submit_idx].handle ||
            uint64_t(c.submit_offset) + uint64_t(c.size) > bo_bytes[c.submit_idx])
            return {Status::malformed, "command buffer missing or range outside host view"};
    }
    return {Status::shape_valid, "host submit shape only; no command-stream validation"};
}

inline Result execute(uint32_t selector, const void* input, size_t bytes) {
    const auto shape = validate_record(selector, input, bytes);
    if (shape.status != Status::shape_valid) return shape;
    return {Status::unsupported, "host-only package has no A530 hardware backend"};
}
inline Result a530_submit(const HakiosGpuSubmit& r,
                         const HakiosGpuSubmitBo* bos, size_t bo_count,
                         const HakiosGpuSubmitCmd* cmds, size_t cmd_count,
                         const uint64_t* bo_bytes, uint32_t* /*fence_out*/) {
    const auto shape = validate_submit(r, bos, bo_count, cmds, cmd_count, bo_bytes);
    if (shape.status != Status::shape_valid) return shape;
    /* Never fabricate a fence, chip ID, completion, power or map ownership. */
    return {Status::unsupported, "A530 submit backend is absent from this host package"};
}
}
