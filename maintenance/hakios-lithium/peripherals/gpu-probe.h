#ifndef LITHIUM_GPU_PROBE_H
#define LITHIUM_GPU_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LITHIUM_GPU_PROBE_CPU_PHYS UINT32_C(0x1)
#define LITHIUM_GPU_PROBE_CHIP_ID UINT32_C(0x05030004)

/* Phase 0 model. The resource order does not encode a board power sequence. */
enum lithium_gpu_resource {
    LITHIUM_GPU_RAILS,
    LITHIUM_GPU_DOMAINS_AND_CLOCKS,
    LITHIUM_GPU_NONSECURE_ACCESS,
    LITHIUM_GPU_RESOURCE_COUNT
};

enum lithium_gpu_step_result {
    LITHIUM_GPU_STEP_OK,
    LITHIUM_GPU_STEP_FAILED,
    LITHIUM_GPU_STEP_TIMEOUT
};

enum lithium_gpu_probe_result {
    LITHIUM_GPU_PROBE_DISABLED,
    LITHIUM_GPU_PROBE_INVALID,
    LITHIUM_GPU_PROBE_WRONG_CPU,
    LITHIUM_GPU_PROBE_UNVERIFIED,
    LITHIUM_GPU_PROBE_PREP_FAILED,
    LITHIUM_GPU_PROBE_READ_FAILED,
    LITHIUM_GPU_PROBE_TIMEOUT,
    LITHIUM_GPU_PROBE_WRONG_CHIP,
    LITHIUM_GPU_PROBE_MATCH
};

struct lithium_gpu_probe_request {
    bool enabled;
    uint32_t cpu_phys; /* Observed physical affinity in Lithium's phys encoding. */
    bool rail_identity_verified;
    bool domain_identity_verified;
    bool nonsecure_ownership_verified;
    uint64_t deadline;
};

struct lithium_gpu_probe_ops {
    /* Monotonic ticks in the same nonwrapping timebase as the absolute deadline. */
    uint64_t (*now)(void *context);
    /* OK transfers ownership. On failure or timeout, prepare rolls back its own partial work. */
    enum lithium_gpu_step_result (*prepare)(void *context,
        enum lithium_gpu_resource resource, uint64_t deadline);
    /* Release one owned resource even after expiration. Cleanup must be bounded. */
    void (*cleanup)(void *context, enum lithium_gpu_resource resource);
    enum lithium_gpu_step_result (*read_chip_id)(void *context,
        uint64_t deadline, uint32_t *chip_id);
};

/* Callbacks are synchronous. Prepare and read must enforce the passed deadline
 * internally. The model checks the clock but cannot interrupt a callback. */
static inline enum lithium_gpu_probe_result
lithium_gpu_probe_run(const struct lithium_gpu_probe_request *request,
    const struct lithium_gpu_probe_ops *ops, void *context)
{
    if (request == NULL) {
        return LITHIUM_GPU_PROBE_INVALID;
    }
    if (!request->enabled) {
        return LITHIUM_GPU_PROBE_DISABLED;
    }
    if (request->cpu_phys != LITHIUM_GPU_PROBE_CPU_PHYS) {
        return LITHIUM_GPU_PROBE_WRONG_CPU;
    }
    if (!request->rail_identity_verified || !request->domain_identity_verified ||
        !request->nonsecure_ownership_verified) {
        return LITHIUM_GPU_PROBE_UNVERIFIED;
    }
    if (ops == NULL || ops->now == NULL || ops->prepare == NULL ||
        ops->cleanup == NULL || ops->read_chip_id == NULL) {
        return LITHIUM_GPU_PROBE_INVALID;
    }

    const uint64_t deadline = request->deadline;
    if (ops->now(context) >= deadline) {
        return LITHIUM_GPU_PROBE_TIMEOUT;
    }

    enum lithium_gpu_probe_result result;
    unsigned acquired = 0;
    while (acquired < LITHIUM_GPU_RESOURCE_COUNT) {
        const enum lithium_gpu_step_result step =
            ops->prepare(context, (enum lithium_gpu_resource)acquired, deadline);
        if (step == LITHIUM_GPU_STEP_OK) {
            ++acquired;
        }
        if (step == LITHIUM_GPU_STEP_TIMEOUT || ops->now(context) >= deadline) {
            result = LITHIUM_GPU_PROBE_TIMEOUT;
            goto cleanup;
        }
        if (step != LITHIUM_GPU_STEP_OK) {
            result = LITHIUM_GPU_PROBE_PREP_FAILED;
            goto cleanup;
        }
    }

    uint32_t chip_id = 0;
    const enum lithium_gpu_step_result step = ops->read_chip_id(context, deadline, &chip_id);
    if (step == LITHIUM_GPU_STEP_TIMEOUT || ops->now(context) >= deadline) {
        result = LITHIUM_GPU_PROBE_TIMEOUT;
    } else if (step != LITHIUM_GPU_STEP_OK) {
        result = LITHIUM_GPU_PROBE_READ_FAILED;
    } else {
        result = chip_id == LITHIUM_GPU_PROBE_CHIP_ID ?
            LITHIUM_GPU_PROBE_MATCH : LITHIUM_GPU_PROBE_WRONG_CHIP;
    }

cleanup:
    while (acquired != 0) {
        ops->cleanup(context, (enum lithium_gpu_resource)--acquired);
    }
    return result;
}

#endif
