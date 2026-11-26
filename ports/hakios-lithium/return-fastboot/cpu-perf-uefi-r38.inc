/* SPDX-License-Identifier: MIT
 * Include inside loader.inc AFTER its seed-state globals, BEFORE seed().
 * The real loader psci_call definition must precede this include.
 * Public result: 0 disabled, 1 programmed, -1 retain/abort this launch.
 * RAM: call after successful isolated RAM SMP seed, before any XNU CPU_ON.
 * UFS: separate default-off CPU-init session; it proves no storage seal
 * and enables no automatic reset. The exact low-frequency core is shared.
 * Never reset the once state, retry, or jump to XNU after a negative result.
 *
 * Exact approved device/profile: power03-runtime/input.json + decode.json,
 * primary/bin0/rev2/partial0/limitation0, CPU policy585000uV for the
 * original four rates, exact600000uV for 614.4 EARLY, including aging;
 * inherited S11 BHS setpoint905000uV. No analog-voltage claim.
 * Only SPMI observer READ commands and the API2 core's 11 PLL/mux writes.
 * Native volatile accesses have no abort guard; time cannot contain a bus hang.
 */
#ifndef LITHIUM_CPU_PERF_UEFI_R38_INC
#define LITHIUM_CPU_PERF_UEFI_R38_INC
#include <stddef.h>
#include <stdint.h>
#include "ram-smp-profile.h"
#if defined(HAKIOS_LITHIUM_UFS_PERF)
#include "cpu-perf-ufs-profile.h"
static unsigned lithium_fastboot_return_ufs;
static uint64_t r38u_ufs_generation;
static unsigned r38u_ufs_session;
#endif
#include "../diagnostics/cpu-spmi-read-r38.h"
#include "../diagnostics/cpu-perf-fixed-r38.h"
#include "../diagnostics/cpu-inherited-supply-r39.h"
#if defined(HAKIOS_LITHIUM_TSENS)
#include "../diagnostics/cpu-tsens-r38.h"
#include "../diagnostics/cpu-tsens-receipt-r38.h"
static struct r38_temp_receipt r38u_temp_receipt;
#endif
#if R38_PERF_FIXED_API != 2
#error "R38 UEFI helper requires non_acd_path_verified API2"
#endif

#define R38U_PLL UINT64_C(0x06480000)
/* CPU-side policy comes from r39_supply_cpu_model_for_hz(target_hz). */
#define R38U_PROGRAMMED_UV UINT32_C(905000)
#define R38U_DELAY_SPINS 65536u
static const unsigned r38u_rows[12] = {
    38,39,41,64,65,66,67,68,73,74,75,76
};
/* Exact raw64 from input.json SHA256 35b7a6a42217ab7317dba56599d2d1db2
 * 770c41c1e31c20937d8a700e8ddf31a. Not synthetic fuse fixtures. */
static const uint64_t r38u_fuses[12] = {
    UINT64_C(0x8048d8e11091005f), UINT64_C(0x401103ea7ab389d6),
    UINT64_C(0xd4c1838e02e761f0), UINT64_C(0xc1dde74d5f3995ed),
    UINT64_C(0x986989b92da2a70a), UINT64_C(0xab0e61ddc60a1d04),
    UINT64_C(0x1004446389b8ec63), UINT64_C(0x00700000043ac54b),
    UINT64_C(0x19800001c9699201), UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000)
};
static const unsigned r38u_s11_offsets[6] = {4,5,0x40,0x41,0x45,0x61};
static const uint8_t r38u_s11_expected[6] = {0x1c,9,0,0xa5,0x40,0x82};

struct r38u_context {
    unsigned used, reads, reads64, observer_writes, pll_writes, gate_checks;
    unsigned fuse_passes, s11_passes, timer_valid;
    uint64_t generation, hz, last_tick, fault_pa, fuse_raw[12];
    uint32_t allowed_cmd, pwr_mode, pwr_l, bhs, pgs, saw, avs, apcs;
    int64_t off[3];
    const char *reason;
    struct r38_spmi_route route;
    struct r38_spmi_result spmi;
    struct r38_perf_snapshot expected;
    struct r38_perf_fixed_result core;
    uint32_t target_hz, temperature_valid[2];
    uint32_t temperature_policy_valid[2];
    int32_t temperature_max[2];
    unsigned inherited_supply_enabled;
    struct r39_supply_result inherited_supply;
};
static struct r38u_context r38u_state;

/* Only these leaf primitives are replaced by focused native-host stubs. */
#ifdef R38_PERF_UEFI_HOST_TEST
static int r38u_native_read32(uint64_t, uint32_t *);
static int r38u_native_read64(uint64_t, uint64_t *);
static int r38u_native_write32(uint64_t, uint32_t);
#else
static int r38u_native_read32(uint64_t pa, uint32_t *out)
{
    __asm__ volatile("dsb sy" ::: "memory");
    *out = *(volatile uint32_t *)(uintptr_t)pa;
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}
static int r38u_native_read64(uint64_t pa, uint64_t *out)
{
    __asm__ volatile("dsb sy" ::: "memory");
    *out = *(volatile uint64_t *)(uintptr_t)pa;
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}
static int r38u_native_write32(uint64_t pa, uint32_t value)
{
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)pa = value;
    __asm__ volatile("dsb sy" ::: "memory");
    return 0;
}
#endif

static int r38u_fail(struct r38u_context *c, const char *reason)
{
    if (!c->reason) c->reason = reason;
    return 0;
}
static int r38u_on_owner(void)
{
    return (READ_SYSREG(MPIDR_EL1) & UINT64_C(0xff00ffffff)) == 0;
}
static int r38u_seed_valid(uint64_t generation)
{
#if defined(HAKIOS_LITHIUM_UFS_PERF)
    return generation && r38u_ufs_session == 1 &&
        generation == r38u_ufs_generation && r38u_on_owner();
#else
    return generation && generation == lithium_fastboot_cookie &&
        lithium_fastboot_handoff && lithium_fastboot_firmware &&
        lithium_fastboot_return_smp == 1 && lithium_fastboot_owner_affinity == 0;
#endif
}
static int r38u_cpus_off(struct r38u_context *c)
{
    if (!r38u_on_owner() || !r38u_seed_valid(c->generation))
        return r38u_fail(c, "owner_or_seed_changed");
    c->off[0] = psci_call(PSCI_AFFINITY_INFO, 0x100, 0, 0);
    c->off[1] = psci_call(PSCI_AFFINITY_INFO, 0x101, 0, 0);
    c->off[2] = psci_call(PSCI_AFFINITY_INFO, 1, 0, 0);
    if (c->off[0] != 1 || c->off[1] != 1 || c->off[2] != 1)
        return r38u_fail(c, "kernel_cpu_not_off");
    return 1;
}
static int r38u_perf_offset(uint32_t offset, int write)
{
    for (unsigned i = 0; i < R38_PERF_REGS; ++i)
        if (r38_perf_offsets[i] == offset)
            return !write || (i != R38_A_MODE && i != R38_CLKCTL);
    return 0;
}
static int r38u_read(void *opaque, uint64_t pa, uint32_t *out)
{
    struct r38u_context *c = opaque;
    int allowed = pa == R38_SPMI_CORE_PA ||
        (pa >= R38_SPMI_CORE_PA + 0x800 &&
         pa <= R38_SPMI_CORE_PA + 0x9fc && !(pa & 3)) ||
        (pa >= R38U_PLL && pa <= R38U_PLL + 0x100 &&
         r38u_perf_offset((uint32_t)(pa - R38U_PLL), 0)) ||
        pa == 0x06400000 || pa == 0x06400004 ||
        pa == 0x099d2018 || pa == 0x099d2060 ||
        pa == 0x09820fd0 || pa == 0x09a10fd0 || pa == 0x09a10020 ||
        (c->route.observer_pa &&
         (pa == c->route.observer_pa + 4 || pa == c->route.observer_pa + 8 ||
          pa == c->route.observer_pa + 0x18 || pa == c->route.observer_pa + 0x1c));
    c->fault_pa = pa;
    if (!out || !allowed || !r38u_on_owner())
        return r38u_fail(c, "read_whitelist_or_owner"), -1;
    ++c->reads;
    if (r38u_native_read32(pa, out))
        return r38u_fail(c, "mmio_read"), -1;
    return 0;
}
static int r38u_time(void *opaque, uint64_t *out)
{
    struct r38u_context *c = opaque;
    uint64_t hz = READ_SYSREG(CNTFRQ_EL0), tick = READ_SYSREG(CNTVCT_EL0);
    if (!out || !r38u_on_owner() || !hz || hz > UINT32_MAX ||
        (c->timer_valid && (hz != c->hz || tick < c->last_tick)) ||
        tick / hz > UINT64_MAX / UINT64_C(1000000))
        return r38u_fail(c, "counter_invalid"), -1;
    c->hz = hz; c->last_tick = tick; c->timer_valid = 1;
    *out = (tick / hz) * UINT64_C(1000000) +
           ((tick % hz) * UINT64_C(1000000)) / hz;
    return 0;
}
static int r38u_delay(void *opaque, uint32_t us)
{
    struct r38u_context *c = opaque;
    uint64_t begin, now;
    if (us > 1000 || r38u_time(c, &begin))
        return r38u_fail(c, "delay_argument_or_time"), -1;
    for (unsigned i = 0; i < R38U_DELAY_SPINS; ++i) {
        if (r38u_time(c, &now)) return -1;
        if (now - begin >= us) return 0;
    }
    return r38u_fail(c, "counter_stalled"), -1;
}
static int r38u_spmi_write(void *opaque, uint64_t pa, uint32_t value)
{
    struct r38u_context *c = opaque;
    if (!r38u_on_owner() || !c->route.observer_pa ||
        pa != c->route.observer_pa || value != c->allowed_cmd ||
        (value >> 27) != 1)
        return r38u_fail(c, "observer_cmd_whitelist"), -1;
    ++c->observer_writes;
    if (r38u_native_write32(pa, value))
        return r38u_fail(c, "observer_cmd_write"), -1;
    return 0;
}
static int r38u_s11(struct r38u_context *c, int log)
{
    const struct r38_spmi_io io = {
        c, r38u_read, r38u_spmi_write, r38u_time, r38u_delay
    };
    for (unsigned i = 0; i < 6; ++i) {
        c->allowed_cmd = (1u << 27) | (r38u_s11_offsets[i] << 4);
        enum r38_spmi_error error = r38_spmi_read(&io, &c->route, 1,
            0x3200 + r38u_s11_offsets[i], 1, &c->spmi);
        if (log)
            print("LITHIUM PERF UEFI s11 addr=%x raw=%x error=%d status=%x\n",
                (uint64_t)(0x3200 + r38u_s11_offsets[i]),
                (uint64_t)c->spmi.data[0], (uint64_t)error,
                (uint64_t)c->spmi.status_raw);
        if (error || !c->spmi.data_valid ||
            c->spmi.data[0] != r38u_s11_expected[i])
            return r38u_fail(c, "s11_changed_or_unread");
    }
    ++c->s11_passes;
    return 1;
}

static int r39u_supply_session(void *opaque,struct r39_supply_session *out)
{
    struct r38u_context *c=opaque;
    if (!out || !r38u_cpus_off(c)) return -1;
    *out=(struct r39_supply_session){
        .generation=c->generation, .seed_owner_affinity=0,
        .current_affinity=READ_SYSREG(MPIDR_EL1)&UINT64_C(0xff00ffffff),
        .cpu_state={c->off[2],c->off[0],c->off[1]},
#if defined(HAKIOS_LITHIUM_UFS_PERF)
        .kind=R39_SUPPLY_UFS_CPU_INIT
#else
        .kind=R39_SUPPLY_RAM_SEED
#endif
    };
    return 0;
}
static int r39u_supply_byte(void *opaque,const struct r38_spmi_route *route,
                          uint32_t addr,struct r38_spmi_result *result)
{
    struct r38u_context *c=opaque;
    const struct r38_spmi_io io={
        c,r38u_read,r38u_spmi_write,r38u_time,r38u_delay
    };
    if (!r39_supply_route_equal(route,&c->route)) return -1;
    c->allowed_cmd=(1u<<27)|((addr&0xffu)<<4);
    return (int)r38_spmi_read(&io,route,1,addr,1,result);
}
static int r39u_supply_dwell(struct r38u_context *c)
{
    const struct r39_supply_io io={
        c,r39u_supply_session,r38u_read,r39u_supply_byte,r38u_time,r38u_delay
    };
    const struct r39_supply_request request={
        .enabled=1,.target_hz=c->target_hz,.generation=c->generation,
        .dwell_us=0,.route=c->route
    };
    enum r39_supply_error error=r39_supply_verify(&io,&request,&c->inherited_supply);
    print("LITHIUM PERF SUPPLY error=%d target_hz=%d digital_uv=%d cpu_model_uv=%d dwell_us=%d captures=%d s11_reads=%d digital_dwell=%d analog_settled=0 regulator_writes=0\n",
        (uint64_t)error,(uint64_t)c->target_hz,
        (uint64_t)c->inherited_supply.inherited_digital_uv,
        (uint64_t)c->inherited_supply.cpu_model_uv,c->inherited_supply.observed_dwell_us,
        (uint64_t)c->inherited_supply.captures_complete,
        (uint64_t)c->inherited_supply.s11_calls,
        (uint64_t)c->inherited_supply.digital_config_dwell_verified);
    return error==R39_SUPPLY_OK || r38u_fail(c,"inherited_supply_dwell_failed");
}
/* Source-defined BHS/PGS/AVS + observed inherited pwr PLL/DIG configuration.
 * These are digital conditions, not independent analog/DIG measurements. */
static int r38u_supply(struct r38u_context *c)
{
    ++c->gate_checks;
    if (!r38u_cpus_off(c) ||
        r38u_read(c, 0x06400000, &c->pwr_mode) ||
        r38u_read(c, 0x06400004, &c->pwr_l) ||
        r38u_read(c, 0x099d2018, &c->bhs) ||
        r38u_read(c, 0x099d2060, &c->pgs) ||
        r38u_read(c, 0x09820fd0, &c->apcs) ||
        r38u_read(c, 0x09a10fd0, &c->saw)) return 0;
    /* Exact observed SAW 1.0 selects source v3_0 layout (AVS_CTL +0x20).
     * Do not read an assumed layout on an unknown version. */
    if (c->saw != 0x10000000 || c->apcs != 0x10020000)
        return r38u_fail(c, "unsupported_apcs_or_saw");
    if (r38u_read(c, 0x09a10020, &c->avs)) return 0;
    if (!(c->pwr_mode & R38_PERF_FIXED_LOCK) || c->pwr_l != 0x40 ||
        !(c->bhs & 1) || (c->pgs & 0x30) || c->avs != 0)
        return r38u_fail(c, "digital_supply_changed");
    return 1;
}
static int r38u_fuse_pass(struct r38u_context *c, unsigned pass)
{
    for (unsigned i = 0; i < 12; ++i) {
        uint64_t raw = 0, pa = UINT64_C(0x74000) + 8u * r38u_rows[i];
        c->fault_pa = pa;
        if (!r38u_on_owner())
            return r38u_fail(c, "fuse_owner");
        ++c->reads64;
        if (r38u_native_read64(pa, &raw))
            return r38u_fail(c, "fuse_read");
        c->fuse_raw[i] = raw;
        print("LITHIUM PERF UEFI fuse pass=%d row=%d raw=%x\n",
            (uint64_t)pass, (uint64_t)r38u_rows[i], raw);
        if (raw != r38u_fuses[i])
            return r38u_fail(c, "fuse_profile_mismatch");
    }
    ++c->fuse_passes;
    return 1;
}
static int r38u_pll_read(void *opaque, uint32_t offset, uint32_t *out)
{
    if (!r38u_perf_offset(offset, 0)) return -1;
    return r38u_read(opaque, R38U_PLL + offset, out);
}
static int r38u_pll_write(void *opaque, uint32_t offset, uint32_t value)
{
    struct r38u_context *c = opaque;
    uint32_t mux, ctl, alt;
    if (!r38u_perf_offset(offset, 1) || c->pll_writes >= 11)
        return r38u_fail(c, "pll_write_whitelist"), -1;
    /* All six S11 values and source/CPU conditions are checked before EACH
     * physical PLL write. No core snapshot is treated as continuing proof. */
    if (!r38u_supply(c) || !r38u_s11(c, 0) || !r38u_cpus_off(c) ||
        r38u_read(c, R38U_PLL + 0x40, &mux) ||
        r38u_read(c, R38U_PLL + 0x44, &ctl) ||
        r38u_read(c, R38U_PLL + 0x100, &alt)) return -1;
    if (mux != 0 || ctl != 0x71 || alt != c->expected.raw[R38_A_MODE])
        return r38u_fail(c, "non_acd_path_changed"), -1;
    c->fault_pa = R38U_PLL + offset;
    ++c->pll_writes; /* attempts, including callback failure */
    if (r38u_native_write32(c->fault_pa, value))
        return r38u_fail(c, "pll_write"), -1;
    return 0;
}
static void r38u_log_write(void *opaque, const struct r38_perf_write_log *e)
{
    (void)opaque;
    print("LITHIUM PERF UEFI write phase=%d pa=%x requested=%x observed=%x valid=%d wrc=%x rrc=%x\n",
        (uint64_t)e->phase, R38U_PLL + e->offset, (uint64_t)e->requested,
        (uint64_t)e->observed, (uint64_t)e->read_valid,
        (uint64_t)(int64_t)e->write_rc, (uint64_t)(int64_t)e->read_rc);
}
/* Returns 1 for the exact opt-in, 0 absent/explicit 0, -1 malformed.
 * Shared RAM profile parser still validates storage/CPU/return arguments. */
static int r38u_option(const char *args, size_t length)
{
    size_t at = 0;
    unsigned seen = 0, enabled = 0;
    if (!args || !length || length >= RFBT_ARGS_MAX) return -1;
    while (at < length) {
        while (at < length && rfbt_space((unsigned char)args[at])) ++at;
        size_t start = at, key;
        while (at < length && !rfbt_space((unsigned char)args[at])) {
            if (!args[at]) return -1;
            ++at;
        }
        key = start;
        while (key < at && args[key] != '=') ++key;
        if (!rfbt_bytes_equal(args + start, key - start,
                              "cpu_perf_fixed", sizeof("cpu_perf_fixed") - 1))
            continue;
        if (++seen != 1 || at - key != 2 || args[key] != '=' ||
            (args[key + 1] != '0' && args[key + 1] != '1')) return -1;
        enabled = (unsigned)(args[key + 1] - '0');
    }
    return (int)enabled;
}

/* Additive explicit target only. Omitted keeps the proven low-rate behavior;
 * malformed/duplicate/unsupported targets abort before any device write. */
static int r39u_rate_option(const char *args, size_t length, uint32_t *hz)
{
    size_t at=0;
    unsigned seen=0;
    *hz=R38_PERF_FIXED_MAIN_HZ;
    while (at<length) {
        while (at<length && rfbt_space((unsigned char)args[at])) ++at;
        size_t start=at, key;
        while (at<length && !rfbt_space((unsigned char)args[at])) {
            if (!args[at]) return 0;
            ++at;
        }
        key=start;
        while (key<at && args[key]!='=') ++key;
        if (!rfbt_bytes_equal(args+start,key-start,
                "cpu_perf_hz",sizeof("cpu_perf_hz")-1)) continue;
        if (++seen!=1 || key==at || at-key<2) return 0;
        uint64_t value=0;
        for (size_t i=key+1;i<at;++i) {
            if (args[i]<'0' || args[i]>'9') return 0;
            value=value*10+(unsigned)(args[i]-'0');
            if (value>UINT32_MAX) return 0;
        }
        if (args[key+1]=='0' || !r39_perf_profile_for_hz((uint32_t)value)) return 0;
        *hz=(uint32_t)value;
    }
    return 1;
}
static uint32_t r39u_target(const struct r38u_context *c)
{
    return c->target_hz ? c->target_hz : R38_PERF_FIXED_MAIN_HZ;
}

#if defined(HAKIOS_LITHIUM_TSENS)
/* A temperature observation cannot grant voltage or clock write permission.
 * Low307.2 remains the already-proven target even if firmware left TSENS off.
 * A later higher target must separately require successful fresh readings.
 */
static int r38u_temp_option(const char *args, size_t length)
{
    size_t at = 0;
    unsigned seen = 0, enabled = 0;
    while (at < length) {
        while (at < length && rfbt_space((unsigned char)args[at])) ++at;
        size_t start = at, key;
        while (at < length && !rfbt_space((unsigned char)args[at])) ++at;
        key = start;
        while (key < at && args[key] != '=') ++key;
        if (!rfbt_bytes_equal(args + start, key - start,
                "cpu_thermal_diag", sizeof("cpu_thermal_diag") - 1)) continue;
        if (++seen != 1 || at - key != 2 || args[key] != '=' ||
            (args[key + 1] != '0' && args[key + 1] != '1')) return -1;
        enabled = (unsigned)(args[key + 1] - '0');
    }
    return (int)enabled;
}

static int r38u_temp_read(void *opaque, uint64_t pa, uint32_t *out)
{
    struct r38u_context *c = opaque;
    int allowed = pa == UINT64_C(0x004a8004) ||
        pa == UINT64_C(0x004a90e4) ||
        pa == UINT64_C(0x004a90b0) || pa == UINT64_C(0x004a90b8) ||
        pa == UINT64_C(0x004a90c4) || pa == UINT64_C(0x004a90cc);
    if (!allowed || !out || !r38u_on_owner() ||
        !r38u_seed_valid(c->generation)) return -1;
    return r38u_native_read32(pa, out);
}

static void r38u_temperature(struct r38u_context *c, unsigned index)
{
    static const uint64_t affinities[] = {0, 1, 0x100, 0x101};
    const char *phase = index ? "after_pll_low" : "before_pll_low";
    uint64_t observed_us = 0;
    unsigned valid = 0;
    if (index > 1) return;
    if (r38u_time(c, &observed_us)) return;
    r38u_temp_receipt.magic = R38_TSENS_RECEIPT_MAGIC;
    r38u_temp_receipt.generation = c->generation;
    r38u_temp_receipt.observed_us[index] = observed_us;
    r38u_temp_receipt.target_hz = r39u_target(c);
    c->temperature_max[index] = INT32_MIN;
    for (unsigned i = 0; i < 4; ++i) {
        struct r38_tsens_sample sample;
        uint32_t client;
        if (!r38_tsens8996_cpu_client(affinities[i], &client)) continue;
        enum r38_tsens_result result =
            r38_tsens8996_read_client(r38u_temp_read, c, client, &sample);
        struct r38_temp_row *row = &r38u_temp_receipt.row[index][i];
        row->result = (uint32_t)result;
        row->ctrl = sample.temperature_present ? sample.ctrl_after : sample.ctrl_before;
        row->trdy = sample.trdy;
        row->status_reads = sample.status_reads;
        if (sample.status_reads)
            row->status = sample.raw_status[sample.temperature_present ?
                sample.selected_index : sample.status_reads - 1];
        if (result == R38_TSENS_OK && sample.temperature_present) {
            ++valid;
            int32_t mc = sample.native_mc;
            if (mc>=0 && mc<75000) ++c->temperature_policy_valid[index];
            if (mc>c->temperature_max[index]) c->temperature_max[index]=mc;
            print("LITHIUM CPU TEMP phase=%s generation=%x us=%d sensor_affinity=%x client=%d valid=1 native_mc=%s%d ctrl=%x trdy=%x raw=%x reads=%d calibration_verified=0 freshness_verified=0\n",
                phase, c->generation, observed_us, affinities[i], (uint64_t)client,
                mc < 0 ? "-" : "", (uint64_t)(mc < 0 ? -mc : mc),
                (uint64_t)sample.ctrl_after, (uint64_t)sample.trdy,
                (uint64_t)sample.raw_status[sample.selected_index],
                (uint64_t)sample.reads_attempted);
        } else {
            print("LITHIUM CPU TEMP phase=%s generation=%x us=%d sensor_affinity=%x client=%d valid=0 native_mc=UNAVAILABLE error=%d ctrl=%x trdy=%x raw0=%x raw1=%x raw2=%x reads=%d\n",
                phase, c->generation, observed_us, affinities[i], (uint64_t)client,
                (uint64_t)result, (uint64_t)sample.ctrl_before,
                (uint64_t)sample.trdy, (uint64_t)sample.raw_status[0],
                (uint64_t)sample.raw_status[1], (uint64_t)sample.raw_status[2],
                (uint64_t)sample.reads_attempted);
        }
    }
    print("LITHIUM CPU TEMP phase=%s sensors_valid=%d of=4 target_model_hz=%d voltage_writes=0\n",
        phase, (uint64_t)valid, (uint64_t)r39u_target(c));
    c->temperature_valid[index]=valid;
    r38u_temp_receipt.phase_mask |= 1u << index;
}
#endif

static int
lithium_cpu_perf_uefi_r38(const char *args, size_t length, uint64_t generation)
{
    struct r38u_context *c = &r38u_state;
    struct r38_perf_authorization a = {0};
    uint64_t time;
    uint32_t required_uv = 0;
    if (args && length && args[length - 1] == 0) --length;
    int option = r38u_option(args, length);
    if (option == 0) return 0;
    if (option < 0) {
        print("LITHIUM PERF UEFI retained reason=invalid_option\n");
        return -1;
    }
#if defined(HAKIOS_LITHIUM_TSENS)
    int thermal = r38u_temp_option(args, length);
    if (thermal < 0) {
        print("LITHIUM PERF UEFI retained reason=invalid_thermal_option\n");
        return -1;
    }
#endif
    if (c->used) {
        print("LITHIUM PERF UEFI retained reason=already_consumed\n");
        return -1;
    }
    c->used = 1;
    c->generation = generation;
    if (!r39u_rate_option(args,length,&c->target_hz)) {
        r38u_fail(c,"unsupported_or_duplicate_rate"); goto done;
    }
    required_uv = r39_supply_cpu_model_for_hz(c->target_hz);
    if (!required_uv) {
        r38u_fail(c,"unsupported_cpu_voltage_model"); goto done;
    }
    int supply_option=r39_supply_option(args,length);
    if (supply_option<0 ||
        (c->target_hz!=R38_PERF_FIXED_MAIN_HZ && supply_option!=1)) {
        r38u_fail(c,"higher_rate_requires_supply_observation"); goto done;
    }
    c->inherited_supply_enabled=(unsigned)supply_option;
#if !defined(HAKIOS_LITHIUM_TSENS)
    if (c->target_hz!=R38_PERF_FIXED_MAIN_HZ) {
        r38u_fail(c,"higher_rate_requires_temperature"); goto done;
    }
#else
    if (c->target_hz!=R38_PERF_FIXED_MAIN_HZ && thermal!=1) {
        r38u_fail(c,"higher_rate_requires_temperature"); goto done;
    }
#endif
    if (!r38u_seed_valid(generation) ||
#if defined(HAKIOS_LITHIUM_UFS_PERF)
        !r38_perf_ufs_profile(args, length) ||
#else
        !rfbt_ram_smp_profile(args, length, 0) ||
#endif
        !r38u_cpus_off(c)) {
        r38u_fail(c, "session_or_cpu_profile"); goto done;
    }
    if (r38u_time(c, &time) || r38u_delay(c, 1) || !r38u_supply(c)) goto done;
    {
        const struct r38_spmi_io io = {
            c, r38u_read, r38u_spmi_write, r38u_time, r38u_delay
        };
        enum r38_spmi_error error = r38_spmi_probe(&io, &c->spmi);
        print("LITHIUM PERF UEFI route error=%d version=%x apid=%d observer=%x\n",
            (uint64_t)error, (uint64_t)c->spmi.route.version,
            (uint64_t)c->spmi.route.apid, c->spmi.route.observer_pa);
        if (error) { r38u_fail(c, "spmi_probe"); goto done; }
        c->route = c->spmi.route;
    }
    for (unsigned pass = 0; pass < 2; ++pass)
        if (!r38u_fuse_pass(c, pass) || !r38u_s11(c, 1) ||
            !r38u_supply(c)) goto done;
    if (c->inherited_supply_enabled && !r39u_supply_dwell(c)) goto done;
    for (unsigned i = 0; i < R38_PERF_REGS; ++i) {
        if (r38u_pll_read(c, r38_perf_offsets[i], &c->expected.raw[i])) goto done;
        c->expected.valid |= 1u << i;
    }
    /* Derive authorization ONLY from successful current-generation captures.
     * The target's reviewed CPU model is pinned to ALL 12 raw64 values.
     * BHS retains exact S11=905mV (fresh six-byte reads); no rail writes
     * or LDO headroom addition. 614.4 requires exactly600mV CPU policy. */
    a.power_verified = c->fuse_passes == 2 && c->s11_passes == 2 &&
        c->gate_checks >= 3 && R38U_PROGRAMMED_UV >= required_uv;
    /* This approved profile belongs to the known lithium Pro port and
     * matches all 12 device fuse rows plus the captured APCS version.
     * APCS 0x10020000 alone is NOT a SoC Pro ID. */
    a.pro_verified = c->fuse_passes == 2 && c->apcs == 0x10020000;
    a.observed_bin = (uint32_t)((c->fuse_raw[0] >> 29) & 7);
    a.observed_revision = (uint32_t)((c->fuse_raw[1] >> 51) & 7);
    a.approved_bin = 0; a.approved_revision = 2;
    a.bin_revision_verified = c->fuse_passes == 2 &&
        a.observed_bin == 0 && a.observed_revision == 2;
    a.snapshot_verified = c->expected.valid == R38_PERF_VALID_ALL;
    a.executing_on_pwr = r38u_on_owner();
    a.perf_cores_quiesced = r38u_cpus_off(c);
    a.exclusive_owner = a.perf_cores_quiesced && r38u_seed_valid(generation);
    a.non_acd_path_verified = c->expected.raw[R38_MUX] == 0 &&
        c->expected.raw[R38_CLKCTL] == 0x71 &&
        !(c->expected.raw[R38_A_MODE] & ~UINT32_C(0x01000000)) &&
        r39_perf_profile_for_hz(c->target_hz) != NULL;
    a.generation = generation;
    if (a.power_verified) a.power_generation = generation;
    if (a.snapshot_verified) a.snapshot_generation = generation;
    print("LITHIUM PERF UEFI authorization generation=%x power=%d bin=%d revision=%d non_acd_path=%d programmed_uv=%d required_model_uv=%d analog_measured=0\n",
        generation, (uint64_t)a.power_verified, (uint64_t)a.observed_bin,
        (uint64_t)a.observed_revision, (uint64_t)a.non_acd_path_verified,
        (uint64_t)R38U_PROGRAMMED_UV, (uint64_t)required_uv);
#if defined(HAKIOS_LITHIUM_TSENS)
    if (thermal) r38u_temperature(c, 0);
    if (c->target_hz!=R38_PERF_FIXED_MAIN_HZ &&
        (c->temperature_valid[0]!=4 || c->temperature_policy_valid[0]!=4 ||
         c->temperature_max[0]<0 ||
         c->temperature_max[0]>=75000)) {
        r38u_fail(c,"higher_rate_temperature_before"); goto done;
    }
#endif
    {
        const struct r38_perf_fixed_io io = {
            c, r38u_pll_read, r38u_pll_write, r38u_delay, r38u_time, r38u_log_write
        };
        r39_perf_fixed_run(&io, &a, &c->expected,c->target_hz,&c->core);
    }
    if (c->core.status != R38_PERF_PROGRAMMED) {
        r38u_fail(c, "core_not_programmed"); goto done;
    }
    if (!r38u_supply(c) || !r38u_s11(c, 0)) goto done;
#if defined(HAKIOS_LITHIUM_TSENS)
    if (thermal) r38u_temperature(c, 1);
    if (c->target_hz!=R38_PERF_FIXED_MAIN_HZ &&
        (c->temperature_valid[1]!=4 || c->temperature_policy_valid[1]!=4 ||
         c->temperature_max[1]<0 ||
         c->temperature_max[1]>=75000)) {
        r38u_fail(c,"higher_rate_temperature_after"); goto done;
    }
#endif
done:
    print("LITHIUM PERF UEFI lock polls=%d valid=%d first=%x last=%x expected=%x mask=%x mismatch=%x elapsed_us=%d\n",
        (uint64_t)c->core.lock_polls, (uint64_t)c->core.lock_mode_valid,
        (uint64_t)c->core.lock_mode_first, (uint64_t)c->core.lock_mode_last,
        (uint64_t)R38_PERF_FIXED_FSM, (uint64_t)R38_PERF_FIXED_MODE_MASK,
        (uint64_t)c->core.lock_mode_mismatch, c->core.lock_elapsed_us);
    print("LITHIUM PERF UEFI end generation=%x reason=%s core_status=%d phase=%d pll_attempts=%d core_attempts=%d observer_read_cmds=%d gates=%d fault_pa=%x primary_model_hz=%d cpu_model_hz=%d measured_hz=UNVERIFIED\n",
        generation, c->reason ? c->reason : "programmed", (uint64_t)c->core.status,
        (uint64_t)c->core.phase, (uint64_t)c->pll_writes,
        (uint64_t)c->core.writes, (uint64_t)c->observer_writes,
        (uint64_t)c->gate_checks, c->fault_pa,
        (uint64_t)c->core.programmed_primary_hz,
        (uint64_t)c->core.programmed_main_hz);
    /* Retain the existing captured snapshot, not new MMIO. frequency12
     * passed LOCK and the MUX write, then failed the final predicate; the
     * former receipt retained only the earlier LOCK sample. A raw field is
     * usable only if its enum-index bit in valid is set. */
    const struct r39_perf_profile *profile=r39_perf_profile_for_hz(c->target_hz);
    print("LITHIUM PERF UEFI snapshot phase=%d valid=%x expect_mux=%x expect_alt=%x\n",
        (uint64_t)c->core.phase, (uint64_t)c->core.final.valid,
        (uint64_t)(c->core.phase == R38_PERF_FINAL && profile ? profile->final_mux : 0u),
        (uint64_t)c->core.initial.raw[R38_A_MODE]);
    if (c->core.final.valid) {
        print("LITHIUM PERF UEFI raw mode=%x l=%x alpha=%x user=%x\n",
            (uint64_t)c->core.final.raw[R38_P_MODE],
            (uint64_t)c->core.final.raw[R38_P_L],
            (uint64_t)c->core.final.raw[R38_P_ALPHA],
            (uint64_t)c->core.final.raw[R38_P_USER]);
        print("LITHIUM PERF UEFI raw cfg=%x cfg_hi=%x test=%x test_hi=%x\n",
            (uint64_t)c->core.final.raw[R38_P_CONFIG],
            (uint64_t)c->core.final.raw[R38_P_CONFIG_HI],
            (uint64_t)c->core.final.raw[R38_P_TEST],
            (uint64_t)c->core.final.raw[R38_P_TEST_HI]);
        print("LITHIUM PERF UEFI raw alt=%x mux=%x ctl=%x\n",
            (uint64_t)c->core.final.raw[R38_A_MODE],
            (uint64_t)c->core.final.raw[R38_MUX],
            (uint64_t)c->core.final.raw[R38_CLKCTL]);
    }
    return !c->reason && c->core.status == R38_PERF_PROGRAMMED ? 1 : -1;
}

#if defined(HAKIOS_LITHIUM_UFS_PERF)
/* Called at the existing pre-CPU_ON loader seed site, BEFORE the reset
 * feature's opt-in check. This session never seeds a Fastboot return record.
 * UFS runtime can be configured here, but cannot be using a worker yet:
 * PSCI AFFINITY_INFO must report all three future XNU CPUs OFF.
 */
static int
lithium_cpu_perf_ufs_prepare(const char *args, uint32_t bytes,
    unsigned copies, uint64_t checkpoint)
{
    if (!args || !bytes || bytes > RFBT_ARGS_MAX ||
        args[bytes - 1] != 0) return -1;
    int opt = r38u_option(args, bytes - 1);
    if (!opt) return 0;
    if (opt < 0 || copies != 1 || checkpoint || hc_requested ||
        !block_enabled || !block_write_requested ||
        !r38_perf_ufs_profile(args, bytes - 1) || !r38u_on_owner() ||
        r38u_ufs_session) {
        print("LITHIUM PERF UEFI retained reason=ufs_init_profile\n");
        return -1;
    }
    uint64_t generation = READ_SYSREG(CNTVCT_EL0) ^
        (uint64_t)(uintptr_t)&r38u_state ^ UINT64_C(0x5546534350553338);
    /* The automatic route uses the very same seeded rfbt/receipt cookie.
     * The old desktop-only initialization keeps its independent session. */
    if (lithium_fastboot_return_ufs) {
        if (!lithium_fastboot_handoff || !lithium_fastboot_firmware ||
            !lithium_fastboot_cookie || !lithium_fastboot_return_smp)
            return -1;
        generation = lithium_fastboot_cookie;
    }
    r38u_ufs_generation = generation ? generation : 1;
    r38u_ufs_session = 1;
    print("LITHIUM PERF UEFI session=ufs_cpu_init automatic_return=%d\n",
        (uint64_t)lithium_fastboot_return_ufs);
    return lithium_cpu_perf_uefi_r38(args, bytes - 1, r38u_ufs_generation);
}
#endif
#endif
