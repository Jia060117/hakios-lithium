/* SPDX-License-Identifier: MIT
 * Evidence-only MSM8996 lithium TSENS reader; NOT integrated or device tested.
 * The sole I/O interface is a caller-provided 32-bit READ callback.
 *
 * Sources, pinned hashes and semantic differences: thermal-read-path.md.
 * MiCode msm-tsens.c:113-122,1385-1501,2717-2726,5530-5589.
 * Stock DTS:8748-8756,8898-8959,9105-9155,13192-13202.
 * Source-backed native units do not prove this phone's calibration or freshness.
 */
#ifndef R38_TSENS8996_READONLY_H
#define R38_TSENS8996_READONLY_H
#include <stdint.h>

#define R38_TSENS_CTRL_OFF       UINT32_C(0x0004)
#define R38_TSENS_STATUS_OFF     UINT32_C(0x10a0)
#define R38_TSENS_TRDY_OFF       UINT32_C(0x10e4)
#define R38_TSENS_ENABLE         UINT32_C(1)
#define R38_TSENS_TRDY           UINT32_C(1)
#define R38_TSENS_VALID          (UINT32_C(1) << 21)
#define R38_TSENS_CODE_MASK      UINT32_C(0xfff)

enum r38_tsens_result {
    R38_TSENS_OK = 0,
    R38_TSENS_BAD_ARGUMENT = 1,
    R38_TSENS_IO_ERROR = 2,
    R38_TSENS_DISABLED = 3,
    R38_TSENS_NO_VALID_SAMPLE = 4,
    R38_TSENS_SUSPICIOUS_ALL_ONES = 5
};

struct r38_tsens_selection {
    uint32_t client_id, controller, hw_sensor;
    uint64_t base_pa, ctrl_pa, trdy_pa, status_pa;
};

struct r38_tsens_sample {
    struct r38_tsens_selection sensor;
    uint32_t ctrl_before, ctrl_after, trdy, raw_status[3];
    uint32_t ctrl_before_read, ctrl_after_read, trdy_read, status_reads;
    uint32_t selected_index, reads_attempted, temperature_present;
    uint32_t trdy_bit, calibration_verified, freshness_verified;
    uint64_t failed_pa;
    int32_t callback_error;
    int32_t native_deci_c, native_mc;
    enum r38_tsens_result result;
};

/* Return 0 only for a successful device-memory 32-bit read; never emulate zero
 * for a failure. Mapping, ordering, fault containment and timestamps are the
 * adapter's responsibility. No address outside the exact selection is read. */
typedef int (*r38_tsens_read32_fn)(void *ctx, uint64_t pa, uint32_t *value);

static inline int
r38_tsens8996_select(uint32_t client, struct r38_tsens_selection *out)
{
    static const uint32_t second_hw[8] = {1, 6, 7, 0, 2, 3, 4, 5};
    struct r38_tsens_selection s = {0};
    if (!out)
        return 0;
    *out = s;
    if (client > 20)
        return 0;
    s.client_id = client;
    s.controller = client < 13 ? 0 : 1;
    s.hw_sensor = client < 13 ? client : second_hw[client - 13];
    s.base_pa = s.controller ? UINT64_C(0x004ac000) : UINT64_C(0x004a8000);
    s.ctrl_pa = s.base_pa + R38_TSENS_CTRL_OFF;
    s.trdy_pa = s.base_pa + R38_TSENS_TRDY_OFF;
    s.status_pa = s.base_pa + R38_TSENS_STATUS_OFF + 4 * s.hw_sensor;
    *out = s;
    return 1;
}

/* Exact sign extension without an implementation-defined unsigned-to-signed
 * cast or signed shift. This is arithmetic only; it has no validity meaning. */
static inline int32_t
r38_tsens8996_native_deci_c(uint32_t raw)
{
    int32_t code = (int32_t)(raw & R38_TSENS_CODE_MASK);
    return code >= 2048 ? code - 4096 : code;
}

/* Physical affinity -> stock Linux CPU mitigation sensor, not a die-location
 * assertion. phys0 remains included even when resident outside XNU. */
static inline int
r38_tsens8996_cpu_client(uint64_t affinity, uint32_t *client)
{
    if (!client)
        return 0;
    *client = UINT32_MAX;
    switch (affinity) {
    case UINT64_C(0):     *client = 4; return 1;
    case UINT64_C(1):     *client = 6; return 1;
    case UINT64_C(0x100): *client = 9; return 1;
    case UINT64_C(0x101): *client = 11; return 1;
    default: return 0;
    }
}

static inline int
r38_tsens8996_do_read(r38_tsens_read32_fn read32, void *ctx,
    uint64_t pa, uint32_t *value, struct r38_tsens_sample *out)
{
    int error;
    out->reads_attempted++;
    error = read32(ctx, pa, value);
    if (error) {
        out->failed_pa = pa;
        out->callback_error = error;
        out->result = R38_TSENS_IO_ERROR;
        return 0;
    }
    if (*value == UINT32_MAX) {
        /* Conservative software anomaly rule, NOT a documented TSENS bit. */
        out->failed_pa = pa;
        out->result = R38_TSENS_SUSPICIOUS_ALL_ONES;
        return 0;
    }
    return 1;
}

/* Bounded to six read32 calls per client: CTRL, TRDY, <=3 STATUS, CTRL.
 * Read TRDY diagnostically: the stock MSM8996 valid-check branch bypasses its
 * wait. A VALID sample with TRDY=0 remains source-decodable and is flagged.
 * Unlike stock's equality fallback, three INVALID codes never become a temp.
 * Both CTRL reads must have ENABLE set; disabling mid-read invalidates it.
 * No cached value, assumed ambient, synthetic Celsius or sentinel temperature.
 */
static inline enum r38_tsens_result
r38_tsens8996_read_client(r38_tsens_read32_fn read32, void *ctx,
    uint32_t client, struct r38_tsens_sample *out)
{
    struct r38_tsens_sample empty = {0};
    uint32_t i, chosen = 3;
    if (!out)
        return R38_TSENS_BAD_ARGUMENT;
    *out = empty;
    out->selected_index = UINT32_MAX;
    out->result = R38_TSENS_BAD_ARGUMENT;
    if (!read32 || !r38_tsens8996_select(client, &out->sensor))
        return out->result;
    if (!r38_tsens8996_do_read(read32, ctx, out->sensor.ctrl_pa,
        &out->ctrl_before, out))
        return out->result;
    out->ctrl_before_read = 1;
    if (!(out->ctrl_before & R38_TSENS_ENABLE)) {
        out->result = R38_TSENS_DISABLED;
        return out->result;
    }
    if (!r38_tsens8996_do_read(read32, ctx, out->sensor.trdy_pa,
        &out->trdy, out))
        return out->result;
    out->trdy_read = 1;
    out->trdy_bit = !!(out->trdy & R38_TSENS_TRDY);
    for (i = 0; i < 3; i++) {
        if (!r38_tsens8996_do_read(read32, ctx, out->sensor.status_pa,
            &out->raw_status[i], out))
            return out->result;
        out->status_reads++;
        if (out->raw_status[i] & R38_TSENS_VALID) {
            chosen = i;
            break;
        }
    }
    if (!r38_tsens8996_do_read(read32, ctx, out->sensor.ctrl_pa,
        &out->ctrl_after, out))
        return out->result;
    out->ctrl_after_read = 1;
    if (!(out->ctrl_after & R38_TSENS_ENABLE)) {
        out->result = R38_TSENS_DISABLED;
        return out->result;
    }
    if (chosen == 3) {
        out->result = R38_TSENS_NO_VALID_SAMPLE;
        return out->result;
    }
    out->selected_index = chosen;
    out->native_deci_c = r38_tsens8996_native_deci_c(out->raw_status[chosen]);
    out->native_mc = out->native_deci_c * 100;
    out->temperature_present = 1;
    /* These remain zero. Neither status VALID nor TRDY attests calibration,
     * conversion age, die placement or the current firmware execution path. */
    out->result = R38_TSENS_OK;
    return out->result;
}
#endif
