/* SPDX-License-Identifier: MIT
 * R39 inherited S11 DIGITAL configuration + measured dwell observation.
 * Standalone callbacks only. No PMIC/SAW/PLL writer, MMIO, SMC, CPU_ON, retry,
 * allocation, or persistent state. This is NOT analog voltage qualification
 * or authorization to change clocks. Main retains fuse, PLL, thermal and
 * launch guards. No claim of excluding unobserved secure/firmware writers.
 *
 * Pinned source/derivation: evidence/cpu-r39-20261121/supply/report.md.
 * FTS2.5 range0 350000..1355000 uV, STEP82 -> 2400 uV/us, +70 us.
 * Worst supported range0 starting point to retained905000:
 * ceil(max(905000-350000,1355000-905000)/2400)+70 = 302 us.
 * The enforced 302 us is a conservative source-derived observation dwell;
 * an unknown earlier request or invisible writer is NOT proven settled.
 *
 * Adapter contract (0 success, other values errors):
 * - read32: fresh, ordered, bounded/fault-aware physical device reads.
 * - read_s11: execute a NEW r38_spmi_read(..., SID1, addr, 1, result).
 *   Never return cached bytes/ACK-only data. Serialize the observer channel;
 *   only observer READ command doorbells are allowed by that transport.
 * - session: fresh live r38u_seed_valid-equivalent check, real MPIDR affinity,
 *   active seed generation/owner, session kind, PSCI AFFINITY_INFO level0
 *   for phys1/100/101. Return error if the live session is not valid.
 * - time_us/delay_us: same invariant counter throughout, detect counter
 *   frequency changes; bounded delay. A callback/MMIO bus hang cannot be
 *   contained by this synchronous helper's elapsed-time budget.
 * Never cache/reuse the receipt across a PLL write or CPU_ON. A caller may
 * run this helper immediately before EACH existing guarded PLL write.
 */
#ifndef LITHIUM_CPU_INHERITED_SUPPLY_R39_H
#define LITHIUM_CPU_INHERITED_SUPPLY_R39_H
#include <stddef.h>
#include <stdint.h>
#include "cpu-spmi-read-r38.h"

#define R39_SUPPLY_API 1
#define R39_SUPPLY_INHERITED_UV UINT32_C(905000)
#define R39_SUPPLY_CPU_MODEL_UV UINT32_C(585000)
#define R39_SUPPLY_EARLY_614_MODEL_UV UINT32_C(600000)
#define R39_SUPPLY_MIN_DWELL_US UINT32_C(302)
#define R39_SUPPLY_MAX_DWELL_US UINT32_C(1000)
/* Execution budget only, not a hardware settling/thermal specification. */
#define R39_SUPPLY_BUDGET_US UINT64_C(100000)
#define R39_SUPPLY_OPTION "cpu_inherited_supply_r39"

enum r39_supply_error {
    R39_SUPPLY_OK = 0, R39_SUPPLY_DISABLED, R39_SUPPLY_ARGUMENT,
    R39_SUPPLY_SESSION_ERROR, R39_SUPPLY_SESSION_CHANGED,
    R39_SUPPLY_READ_ERROR, R39_SUPPLY_S11_ERROR, R39_SUPPLY_ROUTE_ERROR,
    R39_SUPPLY_DIGITAL_MISMATCH, R39_SUPPLY_DIGITAL_CHANGED,
    R39_SUPPLY_TIME_ERROR, R39_SUPPLY_TIME_ROLLBACK,
    R39_SUPPLY_BUDGET_EXCEEDED, R39_SUPPLY_DELAY_ERROR,
    R39_SUPPLY_DWELL_SHORT
};
enum r39_supply_session_kind {
    R39_SUPPLY_RAM_SEED = 1, R39_SUPPLY_UFS_CPU_INIT = 2
};
struct r39_supply_session {
    uint64_t generation, seed_owner_affinity, current_affinity;
    /* Fixed order: phys1, phys0x100, phys0x101; PSCI OFF is exactly 1. */
    int64_t cpu_state[3];
    uint32_t kind;
};
struct r39_supply_io {
    void *context;
    int (*session)(void *, struct r39_supply_session *);
    int (*read32)(void *, uint64_t, uint32_t *);
    int (*read_s11)(void *, const struct r38_spmi_route *, uint32_t,
                    struct r38_spmi_result *);
    int (*time_us)(void *, uint64_t *);
    int (*delay_us)(void *, uint32_t);
};
struct r39_supply_request {
    uint32_t enabled, target_hz;
    uint64_t generation;
    /* 0 selects 302 us; explicit values must be 302..1000 inclusive. */
    uint32_t dwell_us;
    /* Freshly discovered by the existing serialized SPMI observer backend. */
    struct r38_spmi_route route;
};
enum r39_supply_reg {
    R39_SUPPLY_APCS, R39_SUPPLY_SAW, R39_SUPPLY_AVS,
    R39_SUPPLY_PWR_MODE, R39_SUPPLY_PWR_L, R39_SUPPLY_BHS, R39_SUPPLY_PGS,
    R39_SUPPLY_REGS
};
static const uint64_t r39_supply_pa[R39_SUPPLY_REGS] = {
    UINT64_C(0x09820fd0), UINT64_C(0x09a10fd0), UINT64_C(0x09a10020),
    UINT64_C(0x06400000), UINT64_C(0x06400004),
    UINT64_C(0x099d2018), UINT64_C(0x099d2060)
};
static const uint32_t r39_supply_s11_addr[6] = {
    0x3204u, 0x3205u, 0x3240u, 0x3241u, 0x3245u, 0x3261u
};
static const uint8_t r39_supply_s11_expected[6] = {
    0x1c, 0x09, 0x00, 0xa5, 0x40, 0x82
};
struct r39_supply_capture {
    uint64_t begin_us, end_us;
    uint32_t raw[R39_SUPPLY_REGS], raw_valid;
    uint8_t s11[6];
    uint32_t s11_valid, s11_status[6];
    struct r39_supply_session before, after;
};
struct r39_supply_result {
    enum r39_supply_error error;
    int callback_rc;
    uint32_t endpoint, field, session_calls, time_calls, read32_calls;
    uint32_t s11_calls, delay_calls, captures_complete, dwell_requested_us;
    uint64_t generation, start_us, last_us, completed_us, observed_dwell_us;
    uint32_t timer_valid, target_hz, inherited_digital_uv, cpu_model_uv;
    uint32_t digital_config_dwell_verified;
    /* These are deliberately never asserted, even on R39_SUPPLY_OK. */
    uint32_t analog_voltage_verified, analog_settled_verified;
    uint32_t secure_writers_excluded, shared_rail_envelope_verified;
    uint32_t fuse_profile_verified, thermal_verified, frequency_authorized;
    struct r39_supply_capture capture[2];
};

static inline int r39_supply_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\v' || c == '\f';
}
/* Length excludes terminating NUL. Missing/0 -> 0, exact 1 -> 1, bad -> -1.
 * No I/O and no global opt-in state; default-off at both parser and request. */
static inline int r39_supply_option(const char *args, size_t length)
{
    static const char key[] = R39_SUPPLY_OPTION;
    size_t at = 0;
    unsigned seen = 0;
    int enabled = 0;
    if ((!args && length) || length > 4096u) return -1;
    while (at < length) {
        size_t start, end, equal, i;
        while (at < length && r39_supply_space((unsigned char)args[at])) ++at;
        start = at;
        while (at < length && !r39_supply_space((unsigned char)args[at])) {
            if (!args[at]) return -1;
            ++at;
        }
        end = at;
        equal = start;
        while (equal < end && args[equal] != '=') ++equal;
        if (equal - start != sizeof(key) - 1u) continue;
        for (i = 0; i < sizeof(key) - 1u && args[start + i] == key[i]; ++i) {}
        if (i != sizeof(key) - 1u) continue;
        if (++seen != 1u || end - equal != 2u ||
            args[equal] != '=' ||
            (args[equal + 1u] != '0' && args[equal + 1u] != '1')) return -1;
        enabled = args[equal + 1u] - '0';
    }
    return enabled;
}
/* Device rawbin0/rev2/combo2 CPU policy, including max 15000uV aging.
 * Stock perf corner5: interpolated597174 - trim15000 -> ceil5mV585000
 * + aging15000 = EXACT600000; aged floor520000, ceiling600000.
 * Source: lithium-stock.dts:10358-10372; cpr3-hmss-regulator.c:712-839;
 * cpr3-util.c:275-285. This is never the shared S11 write value. */
static inline uint32_t r39_supply_cpu_model_for_hz(uint32_t hz)
{
    if (hz == 307200000u || hz == 384000000u ||
        hz == 460800000u || hz == 537600000u)
        return R39_SUPPLY_CPU_MODEL_UV;
    if (hz == 614400000u) return R39_SUPPLY_EARLY_614_MODEL_UV;
    /* Consecutive stock corners6..9; exact aged CPU policy, not S11.
     * Same bin0/rev2/combo2 source/model as corner5; no new rail write. */
    if (hz == 691200000u) return UINT32_C(610000);
    if (hz == 748800000u) return UINT32_C(620000);
    if (hz == 825600000u) return UINT32_C(635000);
    if (hz == 902400000u) return UINT32_C(645000);
    return 0;
}
static inline int r39_supply_target(uint32_t hz)
{
    return r39_supply_cpu_model_for_hz(hz) != 0;
}
static inline int r39_supply_route_valid(const struct r38_spmi_route *r)
{
    return r->version == R38_SPMI_V2_VERSION &&
           r->apid < R38_SPMI_CHANNELS && r->mapping_raw &&
           ((r->mapping_raw >> 8) & 0xfffu) == R38_SPMI_PPID &&
           r->observer_bytes == R38_SPMI_WINDOW_BYTES &&
           r->observer_pa == R38_SPMI_OBSERVER_PA +
               UINT64_C(0x8000) * r->apid; /* EE0 */
}
static inline int r39_supply_route_equal(const struct r38_spmi_route *a,
                                         const struct r38_spmi_route *b)
{
    return a->version == b->version && a->apid == b->apid &&
           a->mapping_raw == b->mapping_raw &&
           a->observer_pa == b->observer_pa &&
           a->observer_bytes == b->observer_bytes;
}
static inline enum r39_supply_error
r39_supply_fail(struct r39_supply_result *r, enum r39_supply_error error)
{
    r->error = error;
    r->digital_config_dwell_verified = 0;
    return error;
}
static inline int r39_supply_clock(const struct r39_supply_io *io,
                                   struct r39_supply_result *r)
{
    uint64_t now = 0;
    int rc;
    ++r->time_calls;
    rc = io->time_us(io->context, &now);
    if (rc) {
        r->callback_rc = rc;
        r39_supply_fail(r, R39_SUPPLY_TIME_ERROR); return 0;
    }
    if (r->timer_valid && now < r->last_us) {
        r39_supply_fail(r, R39_SUPPLY_TIME_ROLLBACK); return 0;
    }
    if (!r->timer_valid) { r->start_us = now; r->timer_valid = 1; }
    r->last_us = now;
    if (now - r->start_us > R39_SUPPLY_BUDGET_US) {
        r39_supply_fail(r, R39_SUPPLY_BUDGET_EXCEEDED); return 0;
    }
    return 1;
}
static inline int r39_supply_session_read(const struct r39_supply_io *io,
    const struct r39_supply_request *q, struct r39_supply_result *r,
    struct r39_supply_session *out)
{
    struct r39_supply_session s = {0};
    int rc;
    ++r->session_calls;
    rc = io->session(io->context, &s);
    if (rc) {
        r->callback_rc = rc;
        r39_supply_fail(r, R39_SUPPLY_SESSION_ERROR); return 0;
    }
    if (s.generation != q->generation || s.seed_owner_affinity != 0 ||
        s.current_affinity != 0 ||
        (s.kind != R39_SUPPLY_RAM_SEED && s.kind != R39_SUPPLY_UFS_CPU_INIT) ||
        s.cpu_state[0] != 1 || s.cpu_state[1] != 1 || s.cpu_state[2] != 1 ||
        (r->capture[0].before.kind && s.kind != r->capture[0].before.kind)) {
        r39_supply_fail(r, R39_SUPPLY_SESSION_CHANGED); return 0;
    }
    if (out) *out = s;
    return r39_supply_clock(io, r);
}
static inline int r39_supply_reg_valid(unsigned i, uint32_t raw)
{
    switch (i) {
    case R39_SUPPLY_APCS: return raw == 0x10020000u;
    case R39_SUPPLY_SAW: return raw == 0x10000000u;
    case R39_SUPPLY_AVS: return raw == 0;
    case R39_SUPPLY_PWR_MODE: return (raw & UINT32_C(0x80000000)) != 0;
    case R39_SUPPLY_PWR_L: return raw == 0x40u;
    case R39_SUPPLY_BHS: return (raw & 1u) != 0;
    case R39_SUPPLY_PGS: return (raw & 0x30u) == 0;
    default: return 0;
    }
}
static inline int r39_supply_capture_read(const struct r39_supply_io *io,
    const struct r39_supply_request *q, struct r39_supply_result *r, unsigned n)
{
    struct r39_supply_capture *c = &r->capture[n];
    int rc;
    r->endpoint = n;
    if (!r39_supply_session_read(io, q, r, &c->before)) return 0;
    c->begin_us = r->last_us;
    for (unsigned i = 0; i < R39_SUPPLY_REGS; ++i) {
        r->field = i;
        if (!r39_supply_session_read(io, q, r, NULL)) return 0;
        ++r->read32_calls;
        rc = io->read32(io->context, r39_supply_pa[i], &c->raw[i]);
        if (rc) {
            r->callback_rc = rc;
            r39_supply_fail(r, R39_SUPPLY_READ_ERROR); return 0;
        }
        c->raw_valid |= 1u << i;
        /* Versions precede AVS; never interpret an unknown SAW layout. */
        if (!r39_supply_reg_valid(i, c->raw[i])) {
            r39_supply_fail(r, R39_SUPPLY_DIGITAL_MISMATCH); return 0;
        }
        if (n && c->raw[i] != r->capture[0].raw[i]) {
            r39_supply_fail(r, R39_SUPPLY_DIGITAL_CHANGED); return 0;
        }
    }
    for (unsigned i = 0; i < 6u; ++i) {
        struct r38_spmi_result bus = {0};
        r->field = i;
        if (!r39_supply_session_read(io, q, r, NULL)) return 0;
        ++r->s11_calls;
        rc = io->read_s11(io->context, &q->route, r39_supply_s11_addr[i], &bus);
        if (rc || bus.error != R38_SPMI_OK) {
            r->callback_rc = rc ? rc : (int)bus.error;
            r39_supply_fail(r, R39_SUPPLY_S11_ERROR); return 0;
        }
        if (!r39_supply_route_equal(&bus.route, &q->route) ||
            bus.version_recheck != q->route.version ||
            bus.mapping_recheck != q->route.mapping_raw) {
            r39_supply_fail(r, R39_SUPPLY_ROUTE_ERROR); return 0;
        }
        if (bus.start_us < r->last_us || bus.last_us < bus.start_us) {
            r39_supply_fail(r, R39_SUPPLY_TIME_ROLLBACK); return 0;
        }
        if (bus.phase != R38_SPMI_DATA || bus.sid != 1u ||
            bus.addr != r39_supply_s11_addr[i] || bus.len != 1u ||
            bus.cmd != ((1u << 27) | ((bus.addr & 0xffu) << 4)) ||
            bus.cmd_attempted != 1u || bus.cmd_completed != 1u ||
            bus.writes != 1u || bus.matches != 1u ||
            bus.mapping_reads != R38_SPMI_CHANNELS ||
            bus.config_valid != 1u || bus.config_raw != 0u ||
            bus.status_valid != 1u || bus.status_raw != 1u ||
            bus.status_or != 1u || bus.data_valid != 1u ||
            bus.data_words_valid != 1u ||
            bus.data[0] != (uint8_t)bus.data_raw[0]) {
            r39_supply_fail(r, R39_SUPPLY_S11_ERROR); return 0;
        }
        c->s11_status[i] = bus.status_raw;
        c->s11[i] = bus.data[0];
        c->s11_valid |= 1u << i;
        if (c->s11[i] != r39_supply_s11_expected[i]) {
            r39_supply_fail(r, R39_SUPPLY_DIGITAL_MISMATCH); return 0;
        }
    }
    if (!r39_supply_session_read(io, q, r, &c->after)) return 0;
    c->end_us = r->last_us;
    ++r->captures_complete;
    return 1;
}
static inline enum r39_supply_error
r39_supply_verify(const struct r39_supply_io *io,
                   const struct r39_supply_request *request,
                   struct r39_supply_result *r)
{
    struct r39_supply_request q;
    int rc;
    if (!r) return R39_SUPPLY_ARGUMENT;
    /* Permit request to reside inside a larger caller-owned receipt object. */
    if (request) q = *request;
    *r = (struct r39_supply_result){0};
    if (!request) return r39_supply_fail(r, R39_SUPPLY_ARGUMENT);
    if (!q.enabled) return r39_supply_fail(r, R39_SUPPLY_DISABLED);
    if (q.enabled != 1u || !q.generation || !r39_supply_target(q.target_hz) ||
        !r39_supply_route_valid(&q.route) ||
        (q.dwell_us && (q.dwell_us < R39_SUPPLY_MIN_DWELL_US ||
                       q.dwell_us > R39_SUPPLY_MAX_DWELL_US)) ||
        !io || !io->session || !io->read32 || !io->read_s11 ||
        !io->time_us || !io->delay_us)
        return r39_supply_fail(r, R39_SUPPLY_ARGUMENT);
    r->generation = q.generation;
    r->target_hz = q.target_hz;
    r->cpu_model_uv = r39_supply_cpu_model_for_hz(q.target_hz);
    r->dwell_requested_us = q.dwell_us ? q.dwell_us : R39_SUPPLY_MIN_DWELL_US;
    if (!r39_supply_capture_read(io, &q, r, 0)) return r->error;
    ++r->delay_calls;
    rc = io->delay_us(io->context, r->dwell_requested_us);
    if (rc) {
        r->callback_rc = rc;
        return r39_supply_fail(r, R39_SUPPLY_DELAY_ERROR);
    }
    if (!r39_supply_session_read(io, &q, r, NULL)) return r->error;
    r->observed_dwell_us = r->last_us - r->capture[0].end_us;
    if (r->observed_dwell_us < r->dwell_requested_us)
        return r39_supply_fail(r, R39_SUPPLY_DWELL_SHORT);
    if (!r39_supply_capture_read(io, &q, r, 1)) return r->error;
    r->completed_us = r->last_us;
    r->inherited_digital_uv = 80000u + 5000u * r->capture[1].s11[3];
    r->digital_config_dwell_verified = 1;
    return r->error = R39_SUPPLY_OK;
}
#endif
