/*
 * R38 host-tested, opt-in PERF PRIMARY PLL core. No MMIO or boot arguments.
 * Adapter owns power proof, same-generation identity/snapshot, serialization,
 * fault handling and ordered callbacks (barriers before/after hardware I/O).
 * Caller must remain on pwrcl with both perf CPUs quiesced throughout.
 * Source: MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9:
 * clock-cpu-8996.c:171-211,317,473-501,1535-1566,1664-1699;
 * clock-pll.c:267-314,493-524,700-746. See perf-fixed report.
 * Success means checked PROGRAMMING MODEL, never measured MHz or power safety.
 */
#ifndef LITHIUM_CPU_PERF_FIXED_R38_H
#define LITHIUM_CPU_PERF_FIXED_R38_H
#include <stdint.h>

#define R38_PERF_FIXED_API 2u
#define R38_PERF_FIXED_MAX_WRITES 11u
#define R38_PERF_FIXED_PRIMARY_HZ UINT32_C(614400000)
#define R38_PERF_FIXED_MAIN_HZ UINT32_C(307200000)
#define R38_PERF_FIXED_LOCK UINT32_C(0x80000000)
#define R38_PERF_FIXED_ACTIVE UINT32_C(0x40000000)
#define R38_PERF_FIXED_FSM UINT32_C(0x00118000)
/* Compare writable configuration, not hardware status: MODE[31] LOCK,
 * [30] ACTIVE and [2:0] OUT/BYPASS/RESET can change under the hardware FSM.
 * Linux v6.12 clk-cpu-8996.c binds the PRIMARY perfcl_pll to
 * clk_alpha_pll_hwfsm_ops; clk-alpha-pll.c defines ACTIVE_FLAG=BIT(30).
 * frequency11 retained 0xc0118000 after 51us: the only old-mask mismatch
 * was ACTIVE. LOCK remains separately mandatory before any MUX write. */
#define R38_PERF_FIXED_MODE_MASK UINT32_C(0x3ffffff8)

/* Literal stock corners of this device's bin0/rev2 plan.
 * main_hz is the legacy SELECTED CPU rate name, not always the /2 MAIN
 * output rate. The first four routes remain MAIN/2; 614.4 uses EARLY.
 * CPU voltage policy is selected separately in cpu-inherited-supply-r39.h.
 * Source: clock-cpu-8996.c:171-211,316-317,473-501,1853-1856. */
struct r39_perf_profile {
    uint32_t main_hz, primary_hz, l; /* preserve existing member offsets */
    uint32_t cpu_divider, final_mux;
};
static const struct r39_perf_profile r39_perf_profiles[] = {
    {307200000u, 614400000u, 32u, 2u, 4u},
    {384000000u, 768000000u, 40u, 2u, 4u},
    {460800000u, 921600000u, 48u, 2u, 4u},
    {537600000u, 1075200000u, 56u, 2u, 4u},
    {614400000u, 614400000u, 32u, 1u, 5u},
    {691200000u, 691200000u, 36u, 1u, 5u},
    {748800000u, 748800000u, 39u, 1u, 5u},
    {825600000u, 825600000u, 43u, 1u, 5u},
    {902400000u, 902400000u, 47u, 1u, 5u},
};
static inline const struct r39_perf_profile *
r39_perf_profile_for_hz(uint32_t hz)
{
    for (unsigned i=0; i<sizeof(r39_perf_profiles)/sizeof(r39_perf_profiles[0]); ++i)
        if (hz == r39_perf_profiles[i].main_hz) return &r39_perf_profiles[i];
    return (const struct r39_perf_profile *)0;
}

enum r38_perf_reg {
    R38_P_MODE, R38_P_L, R38_P_ALPHA, R38_P_USER, R38_P_CONFIG,
    R38_P_CONFIG_HI, R38_P_TEST, R38_P_TEST_HI,
    R38_A_MODE, R38_MUX, R38_CLKCTL, R38_PERF_REGS
};
static const uint32_t r38_perf_offsets[R38_PERF_REGS] = {
    0x00u, 0x04u, 0x08u, 0x10u, 0x18u, 0x1cu, 0x20u, 0x24u,
    0x100u, 0x40u, 0x44u
};
#define R38_PERF_VALID_ALL ((UINT32_C(1) << R38_PERF_REGS) - 1u)
struct r38_perf_snapshot { uint32_t valid, raw[R38_PERF_REGS]; };
enum r38_perf_status {
    R38_PERF_UNKNOWN = 0, R38_PERF_NOT_SUPPORTED,
    R38_PERF_READ_ERROR, R38_PERF_WRITE_ERROR, R38_PERF_READBACK_ERROR,
    R38_PERF_LOCK_TIMEOUT, R38_PERF_TIME_ERROR, R38_PERF_PROGRAMMED
};
enum r38_perf_phase {
    R38_PERF_GATE, R38_PERF_SNAPSHOT, R38_PERF_RESET,
    R38_PERF_INITIALIZE, R38_PERF_RATE, R38_PERF_FSM_ENABLE,
    R38_PERF_LOCK_WAIT, R38_PERF_MUX_SELECT, R38_PERF_FINAL,
    R38_PERF_PHASES
};
struct r38_perf_write_log {
    uint32_t phase, offset, requested, observed, mask, read_valid;
    int write_rc, read_rc;
};
struct r38_perf_fixed_io {
    void *context;
    /* Relative offsets ONLY; adapter must bind these to perf base 0x06480000.
     * Return 0 on success; delay/time must be bounded and use an invariant
     * microsecond clock. A blocking/faulting callback cannot be rescued here. */
    int (*read32)(void *, uint32_t, uint32_t *);
    int (*write32)(void *, uint32_t, uint32_t);
    int (*delay_us)(void *, uint32_t);
    int (*time_us)(void *, uint64_t *);
    void (*log_write)(void *, const struct r38_perf_write_log *); /* optional */
};
struct r38_perf_authorization {
    uint32_t power_verified, pro_verified, bin_revision_verified;
    uint32_t snapshot_verified, exclusive_owner, perf_cores_quiesced;
    /* Both MAIN/2 (PMUX0) and direct EARLY (PMUX1) bypass ACD.
     * Neither requires or claims the global ACD bank is disabled. Source:
     * clock-cpu-8996.c:317,473-501,593-617,645-651,1830-1839. */
    uint32_t executing_on_pwr, non_acd_path_verified;
    uint32_t approved_bin, observed_bin, approved_revision, observed_revision;
    uint64_t generation, power_generation, snapshot_generation;
};
struct r38_perf_fixed_result {
    enum r38_perf_status status;
    enum r38_perf_phase phase;
    uint32_t writes, readbacks, phase_writes[R38_PERF_PHASES];
    uint32_t phase_reads[R38_PERF_PHASES], mux_write_attempted;
    /* programmed_main_hz retains the legacy selected-CPU-rate meaning.
     * Physical MAIN output remains primary/2, including the EARLY profile. */
    uint32_t mux_xo_confirmed, programmed_primary_hz, programmed_main_hz;
    /* Captured from existing lock-loop reads, never extra MMIO. The first
     * hardware attempt stopped here without retaining the offending MODE. */
    uint32_t lock_polls, lock_mode_valid, lock_mode_first, lock_mode_last;
    uint32_t lock_mode_mismatch;
    uint64_t lock_elapsed_us;
    struct r38_perf_write_log log[R38_PERF_FIXED_MAX_WRITES];
    struct r38_perf_snapshot initial, final;
};

static inline int
r38_perf_read(const struct r38_perf_fixed_io *io,
              struct r38_perf_fixed_result *r, unsigned reg, uint32_t *v)
{
    ++r->phase_reads[r->phase];
    if (io->read32(io->context, r38_perf_offsets[reg], v)) {
        r->status = R38_PERF_READ_ERROR;
        return 0;
    }
    return 1;
}
static inline int
r38_perf_snapshot_read(const struct r38_perf_fixed_io *io,
                       struct r38_perf_fixed_result *r,
                       struct r38_perf_snapshot *s)
{
    s->valid = 0;
    for (unsigned i = 0; i < R38_PERF_REGS; ++i) {
        if (!r38_perf_read(io, r, i, &s->raw[i])) return 0;
        s->valid |= UINT32_C(1) << i;
    }
    return 1;
}
static inline int
r38_perf_xo_guard(const struct r38_perf_fixed_io *io,
                  struct r38_perf_fixed_result *r)
{
    uint32_t mux, ctl, alt;
    if (!r38_perf_read(io,r,R38_MUX,&mux) ||
        !r38_perf_read(io,r,R38_CLKCTL,&ctl) ||
        !r38_perf_read(io,r,R38_A_MODE,&alt)) return 0;
    if (mux != 0 || ctl != 0x71u || alt != r->initial.raw[R38_A_MODE]) {
        r->status = R38_PERF_UNKNOWN;
        return 0;
    }
    return 1;
}
static inline int
r38_perf_write_checked(const struct r38_perf_fixed_io *io,
                       struct r38_perf_fixed_result *r, unsigned reg,
                       uint32_t value, uint32_t mask)
{
    struct r38_perf_write_log *e;
    if (r->writes >= R38_PERF_FIXED_MAX_WRITES ||
        !r38_perf_xo_guard(io,r)) return 0;
    e = &r->log[r->writes++];
    e->phase = (uint32_t)r->phase;
    e->offset = r38_perf_offsets[reg];
    e->requested = value; e->mask = mask;
    ++r->phase_writes[r->phase];
    if (reg == R38_MUX) r->mux_write_attempted = 1;
    e->write_rc = io->write32(io->context,e->offset,value);
    /* Read after EVERY attempted write, including a callback-reported error. */
    ++r->phase_reads[r->phase];
    e->read_rc = io->read32(io->context,e->offset,&e->observed);
    e->read_valid = (e->read_rc == 0);
    r->readbacks += e->read_valid;
    if (io->log_write) io->log_write(io->context,e);
    if (e->write_rc) { r->status=R38_PERF_WRITE_ERROR; return 0; }
    if (e->read_rc) { r->status=R38_PERF_READ_ERROR; return 0; }
    if ((e->observed & mask) != (value & mask)) {
        r->status=R38_PERF_READBACK_ERROR; return 0;
    }
    return 1;
}
static inline int
r38_perf_delay(const struct r38_perf_fixed_io *io,
               struct r38_perf_fixed_result *r, uint32_t us)
{
    if (io->delay_us(io->context,us)) {
        r->status=R38_PERF_TIME_ERROR; return 0;
    }
    return 1;
}
static inline int
r38_perf_clkctl_snapshot_ok(uint32_t value, uint32_t expected_mux)
{
    /* Board/profile-specific observed state, not a register-field mask:
     * frequency13's complete final snapshot retained LOCK, MUX=4 and every
     * programmed PLL field, with CLKCTL=0x40040071 after the sole MUX write.
     * Bits30/18 have no established field names here. Do not write them,
     * ignore arbitrary high bits, or accept this state before MUX selection.
     * Keep the old exact value as well; AUTO bit8 and all other values remain
     * rejected. Initial snapshots and every prewrite XO guard stay exact. */
    if (expected_mux == 0u) return value == UINT32_C(0x71);
    if (expected_mux == 4u || expected_mux == 5u)
        if (value == UINT32_C(0x71) || value == UINT32_C(0x40040071))
            return 1;
    /* frequency26's retained full snapshot: MUX5, LOCK, L0x20, all eleven
     * fields valid and exact; only CLKCTL=0x50050071 failed the old final
     * predicate. This is a lithium empirical profile, NOT a field decoding
     * or frequency/stability proof. High-bit meanings remain unverified.
     * Admit only this complete raw word after MUX5 selection, never before
     * selection or on MUX4. Keep every other unknown value rejected; there
     * is no CLKCTL write, AUTO-only mask or new hardware configuration. */
    if (expected_mux == 5u) return value == UINT32_C(0x50050071);
    return 0;
}
static inline int
r39_perf_programmed_snapshot(const struct r38_perf_snapshot *s,
                             uint32_t mux, uint32_t alt,
                             const struct r39_perf_profile *profile)
{
    if (!profile || (mux != 0u && mux != profile->final_mux)) return 0;
    return s->valid == R38_PERF_VALID_ALL &&
        (s->raw[R38_P_MODE] & R38_PERF_FIXED_MODE_MASK) == R38_PERF_FIXED_FSM &&
        (s->raw[R38_P_MODE] & R38_PERF_FIXED_LOCK) != 0 &&
        s->raw[R38_P_L] == profile->l && s->raw[R38_P_ALPHA] == 0 &&
        s->raw[R38_P_USER] == 0x109u && s->raw[R38_P_CONFIG] == 0x200d4828u &&
        s->raw[R38_P_CONFIG_HI] == 6 && s->raw[R38_P_TEST] == 0x1c000000u &&
        s->raw[R38_P_TEST_HI] == 0x4000u && s->raw[R38_A_MODE] == alt &&
        s->raw[R38_MUX] == mux && r38_perf_clkctl_snapshot_ok(s->raw[R38_CLKCTL], mux);
}
static inline int
r38_perf_programmed_snapshot(const struct r38_perf_snapshot *s,
                             uint32_t mux, uint32_t alt)
{
    return r39_perf_programmed_snapshot(s,mux,alt,&r39_perf_profiles[0]);
}

/* Null/missing authorization or an unexpected initial value => zero writes.
 * No retries, rollback, alternative PLL, auto clock, ACD or voltage writes.
 * After any error leave the programmed PLL state alone. In particular, a mux
 * write error leaves selection UNKNOWN and must not trigger a soft rollback. */
static inline enum r38_perf_status
r39_perf_fixed_run(const struct r38_perf_fixed_io *io,
                   const struct r38_perf_authorization *a,
                   const struct r38_perf_snapshot *expected,
                   uint32_t target_hz,
                   struct r38_perf_fixed_result *r)
{
    const uint32_t initial[R38_PERF_REGS] = {
        0x01000000u,0,0,8,0x200d4828u,6,0x1c000000u,0x4000u,0,0,0x71u
    };
    uint64_t begin=0, now=0, previous=0;
    uint32_t mode=0;
    if (!r) return R38_PERF_UNKNOWN;
    *r = (struct r38_perf_fixed_result){0};
    r->phase=R38_PERF_GATE;
    const struct r39_perf_profile *profile = r39_perf_profile_for_hz(target_hz);
    if (!io || !io->read32 || !io->write32 || !io->delay_us || !io->time_us ||
        !a || !expected || !profile || a->power_verified != 1 || a->pro_verified != 1 ||
        a->bin_revision_verified != 1 || a->snapshot_verified != 1 ||
        a->exclusive_owner != 1 || a->perf_cores_quiesced != 1 ||
        a->executing_on_pwr != 1 || a->non_acd_path_verified != 1 ||
        a->approved_bin != a->observed_bin || a->approved_bin > 7 ||
        a->approved_revision != a->observed_revision ||
        !a->generation || a->power_generation != a->generation ||
        a->snapshot_generation != a->generation ||
        expected->valid != R38_PERF_VALID_ALL) return r->status;
    /* Exact supported profile only: primary initially off/powered down,
     * integer L=0, XO mux, AUTO disabled. Alternate may be reset or APC_PDN
     * but must have no other bits (including FSM/lock/output) set. */
    for (unsigned i=0; i<R38_PERF_REGS; ++i) {
        if (i == R38_A_MODE) {
            if (expected->raw[i] & ~UINT32_C(0x01000000))
                return r->status;
        } else if (expected->raw[i] != initial[i]) return r->status;
    }
    r->phase=R38_PERF_SNAPSHOT;
    if (!r38_perf_snapshot_read(io,r,&r->initial)) return r->status;
    for (unsigned i=0; i<R38_PERF_REGS; ++i)
        if (r->initial.raw[i] != expected->raw[i]) return r->status;
    /* Check a usable invariant timer BEFORE permitting any write. */
    if (io->time_us(io->context,&begin) || !r38_perf_delay(io,r,1) ||
        io->time_us(io->context,&now) || now < begin || now-begin < 1) {
        r->status=R38_PERF_TIME_ERROR; return r->status;
    }
    r->phase=R38_PERF_RESET;
    if (!r38_perf_write_checked(io,r,R38_P_MODE,0,UINT32_MAX)) goto done;
    r->phase=R38_PERF_INITIALIZE;
    /* Literal expansion of __variable_rate_pll_init for this exact profile.
     * USER: /2, no predivide/alpha, main+early enabled. Pro init_test_ctl=true. */
    if (!r38_perf_write_checked(io,r,R38_P_USER,0x109u,UINT32_MAX) ||
        !r38_perf_write_checked(io,r,R38_P_MODE,0,UINT32_MAX) ||
        !r38_perf_write_checked(io,r,R38_P_ALPHA,0,UINT32_MAX) ||
        !r38_perf_write_checked(io,r,R38_P_CONFIG,0x200d4828u,UINT32_MAX) ||
        !r38_perf_write_checked(io,r,R38_P_CONFIG_HI,6,UINT32_MAX) ||
        !r38_perf_write_checked(io,r,R38_P_TEST,0x1c000000u,UINT32_MAX) ||
        !r38_perf_write_checked(io,r,R38_P_TEST_HI,0x4000u,UINT32_MAX)) goto done;
    r->phase=R38_PERF_RATE;
    if (!r38_perf_write_checked(io,r,R38_P_L,profile->l,UINT32_MAX)) goto done;
    r->phase=R38_PERF_FSM_ENABLE;
    if (!r38_perf_write_checked(io,r,R38_P_MODE,R38_PERF_FIXED_FSM,
                               R38_PERF_FIXED_MODE_MASK)) goto done;
    r->phase=R38_PERF_LOCK_WAIT;
    if (io->time_us(io->context,&begin) || !r38_perf_delay(io,r,50)) {
        r->status=R38_PERF_TIME_ERROR; goto done;
    }
    previous=begin;
    for (unsigned poll=0; poll<200; ++poll) {
        if (io->time_us(io->context,&now) || now<previous ||
            now<begin || now-begin<50) {
            r->status=R38_PERF_TIME_ERROR; goto done;
        }
        if (now-begin>=250) break;
        previous=now;
        r->lock_elapsed_us = now - begin;
        if (!r38_perf_read(io,r,R38_P_MODE,&mode)) goto done;
        if (!r->lock_mode_valid) r->lock_mode_first = mode;
        r->lock_mode_valid = 1;
        r->lock_mode_last = mode;
        ++r->lock_polls;
        r->lock_mode_mismatch =
            (mode ^ R38_PERF_FIXED_FSM) & R38_PERF_FIXED_MODE_MASK;
        if ((mode & R38_PERF_FIXED_MODE_MASK) != R38_PERF_FIXED_FSM) {
            r->status=R38_PERF_READBACK_ERROR; goto done;
        }
        if (mode & R38_PERF_FIXED_LOCK) break;
        if (!r38_perf_delay(io,r,1)) goto done;
    }
    if (!(mode & R38_PERF_FIXED_LOCK)) {
        r->status=R38_PERF_LOCK_TIMEOUT; goto done;
    }
    if (!r38_perf_snapshot_read(io,r,&r->final)) goto done;
    if (!r39_perf_programmed_snapshot(&r->final,0,r->initial.raw[R38_A_MODE],profile)) {
        r->status=R38_PERF_READBACK_ERROR; goto done;
    }
    r->phase=R38_PERF_MUX_SELECT;
    /* SMUX[3:2]=1; PMUX0=MAIN/2 or PMUX1=EARLY, per exact profile.
     * No ACD/auto/always-on-source change; the first ten writes are shared.
     * This is an off-PLL bootstrap from XO, not a live 537->614 transition. */
    if (!r38_perf_write_checked(io,r,R38_MUX,profile->final_mux,UINT32_MAX) ||
        !r38_perf_delay(io,r,5)) goto done;
    r->phase=R38_PERF_FINAL;
    if (!r38_perf_snapshot_read(io,r,&r->final)) goto done;
    if (!r39_perf_programmed_snapshot(&r->final,profile->final_mux,r->initial.raw[R38_A_MODE],profile)) {
        r->status=R38_PERF_READBACK_ERROR; goto done;
    }
    r->programmed_primary_hz=profile->primary_hz;
    r->programmed_main_hz=profile->main_hz;
    r->status=R38_PERF_PROGRAMMED;
done:
    /* Observational only: never claim XO after any attempted mux write. */
    if (!r->mux_write_attempted) {
        uint32_t mux=~0u, ctl=~0u;
        ++r->phase_reads[r->phase];
        int mr=io->read32(io->context,r38_perf_offsets[R38_MUX],&mux);
        ++r->phase_reads[r->phase];
        int cr=io->read32(io->context,r38_perf_offsets[R38_CLKCTL],&ctl);
        r->mux_xo_confirmed=(mr==0 && cr==0 && mux==0 && ctl==0x71u);
    }
    return r->status;
}
/* Original opt-in is byte-for-byte the same 307.2MHz sequence by default. */
static inline enum r38_perf_status
r38_perf_fixed_run(const struct r38_perf_fixed_io *io,
                   const struct r38_perf_authorization *a,
                   const struct r38_perf_snapshot *expected,
                   struct r38_perf_fixed_result *r)
{
    return r39_perf_fixed_run(io,a,expected,R38_PERF_FIXED_MAIN_HZ,r);
}
#endif
