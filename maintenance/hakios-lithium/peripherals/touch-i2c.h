#ifndef LITHIUM_TOUCH_I2C_H
#define LITHIUM_TOUCH_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lithium stock DTS: i2c12, BLSP2 QUP6, ft5x46_i2c@38. */
#define LITHIUM_TOUCH_I2C_QUP_BASE UINT32_C(0x075ba000)
#define LITHIUM_TOUCH_I2C_BUS 12
#define LITHIUM_TOUCH_I2C_ADDR UINT8_C(0x38)
#define LITHIUM_TOUCH_I2C_REPORT_SIZE 63
#define LITHIUM_TOUCH_I2C_MAX_ATTEMPTS 3

enum lithium_touch_i2c_status {
    LITHIUM_TOUCH_I2C_OK,
    LITHIUM_TOUCH_I2C_INVALID,
    LITHIUM_TOUCH_I2C_NOT_OWNED,
    LITHIUM_TOUCH_I2C_BUSY,
    LITHIUM_TOUCH_I2C_NACK,
    LITHIUM_TOUCH_I2C_TIMEOUT,
    LITHIUM_TOUCH_I2C_NO_PROGRESS,
    LITHIUM_TOUCH_I2C_SHORT,
    LITHIUM_TOUCH_I2C_IO_ERROR,
    /* The platform has latched this controller out of service. */
    LITHIUM_TOUCH_I2C_QUARANTINED,
    /* Transfer or cleanup state is not known to be safe for another owner. */
    LITHIUM_TOUCH_I2C_CLEANUP_UNCERTAIN
};

enum lithium_touch_i2c_handoff_state {
    /* Zero is deliberately fail-closed for callback implementations. */
    LITHIUM_TOUCH_I2C_HANDOFF_UNCERTAIN = 0,
    LITHIUM_TOUCH_I2C_HANDOFF_RELEASED = 1
};

enum lithium_touch_i2c_quarantine_reason {
    LITHIUM_TOUCH_I2C_QUARANTINE_TRANSFER_UNCERTAIN,
    LITHIUM_TOUCH_I2C_QUARANTINE_RELEASE_UNCERTAIN
};

/* Optional strict ownership callbacks. RELEASED means the controller and the
 * software claim were both safely handed back. Any other result must retain
 * the claim. quarantine must be idempotent, retain the claim, and make later
 * claim attempts return LITHIUM_TOUCH_I2C_QUARANTINED until explicit recovery. */
struct lithium_touch_i2c_safety_ops {
    enum lithium_touch_i2c_handoff_state (*release)(void *context);
    void (*quarantine)(void *context, enum lithium_touch_i2c_quarantine_reason reason);
};

struct lithium_touch_i2c_completion {
    enum lithium_touch_i2c_status status;
    size_t written;
    size_t read;
};

struct lithium_touch_i2c_ops {
    /* Synchronous callbacks. now uses the caller's nonwrapping monotonic timebase.
     * claim and write_read must enforce the absolute deadline internally. */
    uint64_t (*now)(void *context);
    /* Verify platform ownership of this QUP and exclusively claim it until
     * release. Only OK acquires a claim. Failure leaves no claim to release.
     * A controller latched by quarantine must reject subsequent claims with
     * LITHIUM_TOUCH_I2C_QUARANTINED. */
    enum lithium_touch_i2c_status (*claim)(void *context, uint32_t qup_base,
        uint64_t deadline);
    /* On the claimed controller: START, address+W, one reg byte, repeated
     * START, address+R, read, STOP. Counts are payload bytes for each phase.
     * OK and NACK are positive assertions that STOP/cleanup completed and the
     * controller is idle and safe to reuse. NACK must not be returned until
     * that condition is confirmed. TIMEOUT, IO_ERROR, NOT_OWNED, and
     * CLEANUP_UNCERTAIN do not assert safe cleanup; callers must quarantine. */
    struct lithium_touch_i2c_completion (*write_read)(void *context,
        uint8_t address, uint8_t reg, uint8_t *read, size_t length, uint64_t deadline);
    /* Legacy release callback. Called only after a confirmed-safe transaction.
     * It must not perform uncertain hardware cleanup. */
    void (*release)(void *context);
};

struct lithium_touch_i2c_identity {
    uint8_t chip_id;
    uint8_t firmware_version;
    uint8_t vendor_id;
};

struct lithium_touch_i2c_report {
    uint8_t bytes[LITHIUM_TOUCH_I2C_REPORT_SIZE];
};

struct lithium_touch_i2c_read_step {
    uint8_t reg;
    size_t length;
};

/* steps and scratch are private to the two fixed read sequences below. */
static inline enum lithium_touch_i2c_status
lithium_touch_i2c_read_sequence_internal(const struct lithium_touch_i2c_ops *ops,
    const struct lithium_touch_i2c_safety_ops *safety,
    void *context, uint64_t deadline, const struct lithium_touch_i2c_read_step *steps,
    size_t step_count, uint8_t *scratch)
{
    if (ops == NULL || ops->now == NULL || ops->claim == NULL ||
        ops->write_read == NULL ||
        (safety == NULL && ops->release == NULL) ||
        (safety != NULL && (safety->release == NULL || safety->quarantine == NULL))) {
        return LITHIUM_TOUCH_I2C_INVALID;
    }
    if (ops->now(context) >= deadline) {
        return LITHIUM_TOUCH_I2C_TIMEOUT;
    }

    enum lithium_touch_i2c_status status =
        ops->claim(context, LITHIUM_TOUCH_I2C_QUP_BASE, deadline);
    if (status != LITHIUM_TOUCH_I2C_OK) {
        return ops->now(context) >= deadline ? LITHIUM_TOUCH_I2C_TIMEOUT : status;
    }
    /* A successful claim promises an idle controller before any transfer. */
    bool bus_safe = true;

    for (unsigned attempt = 0; attempt < LITHIUM_TOUCH_I2C_MAX_ATTEMPTS; ++attempt) {
        size_t offset = 0;
        for (size_t i = 0; i < step_count; ++i) {
            if (ops->now(context) >= deadline) {
                status = LITHIUM_TOUCH_I2C_TIMEOUT;
                goto release;
            }
            const struct lithium_touch_i2c_completion done =
                ops->write_read(context, LITHIUM_TOUCH_I2C_ADDR, steps[i].reg,
                    scratch + offset, steps[i].length, deadline);
            status = done.status;
            /* Only OK/NACK carry the callback's explicit safe-to-reuse
             * guarantee. Any other transfer status is fail-closed. */
            if (status != LITHIUM_TOUCH_I2C_OK && status != LITHIUM_TOUCH_I2C_NACK) {
                bus_safe = false;
                goto quarantine;
            }
            if (done.written > 1 || done.read > steps[i].length ||
                (done.read != 0 && done.written == 0)) {
                status = LITHIUM_TOUCH_I2C_IO_ERROR;
                bus_safe = false;
                goto quarantine;
            }
            if (ops->now(context) >= deadline) {
                status = LITHIUM_TOUCH_I2C_TIMEOUT;
                goto release;
            }
            if (status != LITHIUM_TOUCH_I2C_OK) {
                /* Restart at the first register so retries never reuse an old prefix. */
                if (status == LITHIUM_TOUCH_I2C_NACK && done.read == 0) {
                    break;
                }
                goto release;
            }
            if (done.read == 0) {
                status = LITHIUM_TOUCH_I2C_NO_PROGRESS;
                goto release;
            }
            if (done.read != steps[i].length) {
                status = LITHIUM_TOUCH_I2C_SHORT;
                goto release;
            }
            offset += done.read;
        }
        if (status == LITHIUM_TOUCH_I2C_OK) {
            break;
        }
    }

release:
    if (!bus_safe) {
        goto quarantine;
    }
    if (safety != NULL) {
        if (safety->release(context) != LITHIUM_TOUCH_I2C_HANDOFF_RELEASED) {
            safety->quarantine(context, LITHIUM_TOUCH_I2C_QUARANTINE_RELEASE_UNCERTAIN);
            return LITHIUM_TOUCH_I2C_QUARANTINED;
        }
    } else {
        ops->release(context);
    }
    if (status == LITHIUM_TOUCH_I2C_OK && ops->now(context) >= deadline) {
        return LITHIUM_TOUCH_I2C_TIMEOUT;
    }
    return status;

quarantine:
    if (safety != NULL) {
        safety->quarantine(context, LITHIUM_TOUCH_I2C_QUARANTINE_TRANSFER_UNCERTAIN);
    }
    /* No release: ownership remains reserved even for the legacy entrypoints. */
    return status;
}

static inline enum lithium_touch_i2c_status
lithium_touch_i2c_read_sequence_with_safety(const struct lithium_touch_i2c_ops *ops,
    const struct lithium_touch_i2c_safety_ops *safety, void *context,
    uint64_t deadline, const struct lithium_touch_i2c_read_step *steps,
    size_t step_count, uint8_t *scratch)
{
    return lithium_touch_i2c_read_sequence_internal(ops, safety, context, deadline,
        steps, step_count, scratch);
}

static inline enum lithium_touch_i2c_status
lithium_touch_i2c_read_sequence(const struct lithium_touch_i2c_ops *ops,
    void *context, uint64_t deadline, const struct lithium_touch_i2c_read_step *steps,
    size_t step_count, uint8_t *scratch)
{
    return lithium_touch_i2c_read_sequence_internal(ops, NULL, context, deadline,
        steps, step_count, scratch);
}

/* Return raw runtime identity bytes. Chip validation needs board evidence. */
static inline enum lithium_touch_i2c_status
lithium_touch_i2c_probe(const struct lithium_touch_i2c_ops *ops, void *context,
    uint64_t deadline, struct lithium_touch_i2c_identity *identity)
{
    if (identity == NULL) {
        return LITHIUM_TOUCH_I2C_INVALID;
    }
    static const struct lithium_touch_i2c_read_step steps[] = {
        {0xa3, 1}, {0xa6, 1}, {0xa8, 1}
    };
    uint8_t raw[3] = {0};
    const enum lithium_touch_i2c_status status =
        lithium_touch_i2c_read_sequence(ops, context, deadline, steps,
            sizeof(steps) / sizeof(steps[0]), raw);
    if (status == LITHIUM_TOUCH_I2C_OK) {
        *identity = (struct lithium_touch_i2c_identity){raw[0], raw[1], raw[2]};
    }
    return status;
}

static inline enum lithium_touch_i2c_status
lithium_touch_i2c_probe_with_safety(const struct lithium_touch_i2c_ops *ops,
    const struct lithium_touch_i2c_safety_ops *safety, void *context,
    uint64_t deadline, struct lithium_touch_i2c_identity *identity)
{
    if (identity == NULL) {
        return LITHIUM_TOUCH_I2C_INVALID;
    }
    static const struct lithium_touch_i2c_read_step steps[] = {
        {0xa3, 1}, {0xa6, 1}, {0xa8, 1}
    };
    uint8_t raw[3] = {0};
    const enum lithium_touch_i2c_status status =
        lithium_touch_i2c_read_sequence_internal(ops, safety, context, deadline, steps,
            sizeof(steps) / sizeof(steps[0]), raw);
    if (status == LITHIUM_TOUCH_I2C_OK) {
        *identity = (struct lithium_touch_i2c_identity){raw[0], raw[1], raw[2]};
    }
    return status;
}

/* Publish only the complete stock split read. The claim serializes bus access.
 * Whether the chip latches a frame across both reads requires hardware evidence. */
static inline enum lithium_touch_i2c_status
lithium_touch_i2c_read_report(const struct lithium_touch_i2c_ops *ops, void *context,
    uint64_t deadline, struct lithium_touch_i2c_report *report)
{
    if (report == NULL) {
        return LITHIUM_TOUCH_I2C_INVALID;
    }
    static const struct lithium_touch_i2c_read_step steps[] = {
        {0x00, 32}, {0x20, LITHIUM_TOUCH_I2C_REPORT_SIZE - 32}
    };
    struct lithium_touch_i2c_report staged = {0};
    const enum lithium_touch_i2c_status status =
        lithium_touch_i2c_read_sequence(ops, context, deadline, steps,
            sizeof(steps) / sizeof(steps[0]), staged.bytes);
    if (status == LITHIUM_TOUCH_I2C_OK) {
        *report = staged;
    }
    return status;
}

static inline enum lithium_touch_i2c_status
lithium_touch_i2c_read_report_with_safety(const struct lithium_touch_i2c_ops *ops,
    const struct lithium_touch_i2c_safety_ops *safety, void *context,
    uint64_t deadline, struct lithium_touch_i2c_report *report)
{
    if (report == NULL) {
        return LITHIUM_TOUCH_I2C_INVALID;
    }
    static const struct lithium_touch_i2c_read_step steps[] = {
        {0x00, 32}, {0x20, LITHIUM_TOUCH_I2C_REPORT_SIZE - 32}
    };
    struct lithium_touch_i2c_report staged = {0};
    const enum lithium_touch_i2c_status status =
        lithium_touch_i2c_read_sequence_internal(ops, safety, context, deadline, steps,
            sizeof(steps) / sizeof(steps[0]), staged.bytes);
    if (status == LITHIUM_TOUCH_I2C_OK) {
        *report = staged;
    }
    return status;
}

#endif
