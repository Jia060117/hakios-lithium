/* SPDX-License-Identifier: MIT
 * Default-off, read-only APCC observation before launchd. Register offsets:
 * MiCode scorpio-n-oss drivers/clk/msm/clock-cpu-8996.c and stock lithium DT.
 *
 * No MMIO writes, regulator changes, indirect L2 selector, debug-clock votes
 * or PMU setup. Software limits cannot contain a stalled interconnect read.
 * ml_probe_read is a panic stub on this port, not a usable abort guard.
 */
#ifndef LITHIUM_CPU_CLOCK_R38_INC
#define LITHIUM_CPU_CLOCK_R38_INC
#include "cpu-clock-decode-r38.h"

static void
lithium_cpu_clock_r38(void)
{
    unsigned enabled = 0, ufs = 99, boot = 99, cpus = 0, ram = 0;
    PE_parse_boot_argn("cpu_clock_diag", &enabled, sizeof(enabled));
    if (enabled != 1) return;
    PE_parse_boot_argn("ufs", &ufs, sizeof(ufs));
    PE_parse_boot_argn("bootcpu", &boot, sizeof(boot));
    PE_parse_boot_argn("cpus", &cpus, sizeof(cpus));
    PE_parse_boot_argn("ram_smp_return", &ram, sizeof(ram));
    if (ufs || boot != 1 || cpus != 3 || ram != 1 ||
        !ml_get_interrupts_enabled() || get_preemption_level() != 0) {
        printf("LITHIUM CPU CLOCK skipped=1 reason=requires_ram_smp_context\n");
        return;
    }
    processor_t target = cpu_to_processor(0);
    if (!target) return;
    processor_t original = thread_bind(target);
    thread_block(THREAD_CONTINUE_NULL);
    uint64_t affinity = __builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL;
    if (cpu_number() != 0 || affinity != 1) {
        printf("LITHIUM CPU CLOCK skipped=1 reason=wrong_cpu affinity=0x%llx\n",
            affinity);
        goto restore;
    }
    printf("LITHIUM CPU CLOCK begin revision=38 affinity=0x%llx mmio_writes=0 voltage_writes=0 fault_guard=0 effective_source_unverified=1\n",
        affinity);
    for (unsigned cluster = 0; cluster < 2; ++cluster) {
        const uint64_t base = cluster ? 0x06480000ULL : 0x06400000ULL;
        const char *name = cluster ? "perfcl" : "pwrcl";
        vm_offset_t mapped = ml_io_map(base, 0x1000);
        if (!mapped) {
            printf("LITHIUM CPU CLOCK cluster=%s base=0x%llx valid=0 reason=map_failed\n",
                name, base);
            continue;
        }
        struct r38_clk_snapshot first = {0}, second = {0};
        for (unsigned pass = 0; pass < 2; ++pass) {
            struct r38_clk_snapshot *sample = pass ? &second : &first;
            for (unsigned r = 0; r < R38_CLK_REG_COUNT; ++r) {
                __asm__ volatile("dmb oshld" ::: "memory");
                uint32_t value = *(volatile uint32_t *)(mapped + r38_clk_read32_regs[r].offset);
                __asm__ volatile("dmb oshld" ::: "memory");
                sample->raw[r] = value;
                sample->valid |= 1u << r;
                printf("LITHIUM CPU CLOCK cluster=%s pass=%u reg=%s pa=0x%llx valid=1 raw=0x%08x stable=%u\n",
                    name, pass, r38_clk_read32_regs[r].name,
                    base + r38_clk_read32_regs[r].offset, value,
                    !pass || first.raw[r] == value);
            }
        }
        /* Requested mux fields only. Automatic source switching, ACD and PLL
         * validity must be considered before deriving any effective rate. */
        uint32_t mux = first.raw[R38_CLK_MUX40], ctl = first.raw[R38_CLK_CLKCTL44];
        printf("LITHIUM CPU CLOCK cluster=%s requested_pmux=%u requested_smux=%u always_on_mux=%u auto_select=%u primary_lock_mode31=%u alternate_lock_mode31=%u programmed_l=%u effective_hz=UNVERIFIED actual_voltage_uv=UNVERIFIED\n",
            name, mux & 3, (mux >> 2) & 3, (mux >> 4) & 3,
            (ctl >> 8) & 1, (first.raw[R38_CLK_P_MODE] >> 31) & 1,
            (first.raw[R38_CLK_A_MODE] >> 31) & 1, first.raw[R38_CLK_P_L]);
        static const unsigned extra[] = {0x07c, 0x160, 0x164};
        for (unsigned e = 0; e < sizeof(extra) / sizeof(extra[0]); ++e) {
            uint32_t value = *(volatile uint32_t *)(mapped + extra[e]);
            printf("LITHIUM CPU CLOCK cluster=%s extra_pa=0x%llx valid=1 raw=0x%08x\n",
                name, base + extra[e], value);
        }
    }
    {
        vm_offset_t fuse = ml_io_map(0x74000, 0x1000);
        if (fuse) {
            uint32_t raw = *(volatile uint32_t *)(fuse + 0x130);
            printf("LITHIUM CPU CLOCK efuse_pa=0x74130 raw=0x%08x speedbin=%u voltage_uv=UNVERIFIED\n",
                raw, (raw >> 29) & 7);
        } else {
            printf("LITHIUM CPU CLOCK efuse_pa=0x74130 valid=0 reason=map_failed\n");
        }
    }
    printf("LITHIUM CPU CLOCK end revision=38 mmio_writes=0 regulator_writes=0 fixed_frequency_applied=0\n");
restore:
    thread_bind(original);
    thread_block(THREAD_CONTINUE_NULL);
}
#endif
