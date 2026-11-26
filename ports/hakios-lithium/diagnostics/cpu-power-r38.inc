/* SPDX-License-Identifier: MIT
 * R38 default-off RAM supply snapshot; no regulator/clock/SPMI/SCM writes.
 * Sources (MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9):
 * drivers/regulator/kryo-regulator.c:44-82; cpr3-util.c:203-236;
 * drivers/soc/qcom/spm.c:42-148,239-250; stock lithium DTS:
 * 10501-10555 (Kryo), 11581-11594 (SAW), 10445-10482 (memacc).
 * QFPROM rows: cpr3-hmss-regulator.c:139-328. No CPR live block.
 *
 * valid=1 means the volatile load returned, NOT analog voltage or bus status.
 * No abort guard: ml_probe_read is a panic stub here. An interconnect stall
 * cannot be bounded by software timeouts; timestamps do not contain a hang.
 * cookie=UNBOUND explicitly does not claim binding to a boot-shared cookie.
 * Include and call lithium_cpu_power_r38() from the existing prelaunch caller.
 */
#ifndef LITHIUM_CPU_POWER_R38_INC
#define LITHIUM_CPU_POWER_R38_INC
#include <stdint.h>
#include <kern/clock.h>
#include <kern/cpu_data.h>
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <mach/mach_time.h>
#include <mach/vm_param.h>
#include <arm/memory_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include "cpu-mmio-window-r38.h"

struct r38_power_reg {
    const char *name;
    unsigned offset;
};

static int
r38_power_load(unsigned pass, const char *block, const char *name,
    vm_offset_t mapped, uint64_t base, unsigned offset, unsigned bits,
    uint64_t *out)
{
    uint64_t t0 = mach_absolute_time(), value = 0;
    if (mapped) {
        __asm__ volatile("dmb oshld" ::: "memory");
        if (bits == 64)
            value = *(volatile uint64_t *)(mapped + offset);
        else
            value = *(volatile uint32_t *)(mapped + offset);
        __asm__ volatile("dmb oshld" ::: "memory");
    }
    uint64_t t1 = mach_absolute_time();
    if (!mapped) {
        printf("LITHIUM CPU POWER pass=%u block=%s reg=%s pa=0x%llx bits=%u valid=0 raw=UNREAD t0=%llu t1=%llu reason=map_failed\n",
            pass, block, name, base + offset, bits, t0, t1);
        return 0;
    }
    *out = value;
    printf("LITHIUM CPU POWER pass=%u block=%s reg=%s pa=0x%llx bits=%u valid=1 raw=0x%016llx t0=%llu t1=%llu\n",
        pass, block, name, base + offset, bits, value, t0, t1);
    return 1;
}

static void
lithium_cpu_power_r38(void)
{
    unsigned enabled = 0, ufs = 99, boot = 99, cpus = 0, ram = 0;
    PE_parse_boot_argn("cpu_power_diag", &enabled, sizeof(enabled));
    if (enabled != 1) return;
    PE_parse_boot_argn("ufs", &ufs, sizeof(ufs));
    PE_parse_boot_argn("bootcpu", &boot, sizeof(boot));
    PE_parse_boot_argn("cpus", &cpus, sizeof(cpus));
    PE_parse_boot_argn("ram_smp_return", &ram, sizeof(ram));
    if (ufs != 0 || boot != 1 || cpus != 3 || ram != 1 ||
        !ml_get_interrupts_enabled() || get_preemption_level() != 0) {
        printf("LITHIUM CPU POWER skipped=1 reason=requires_ram_smp_context\n");
        return;
    }
    processor_t target = cpu_to_processor(0);
    if (!target) {
        printf("LITHIUM CPU POWER skipped=1 reason=no_logical0\n");
        return;
    }
    processor_t original = thread_bind(target);
    thread_block(THREAD_CONTINUE_NULL);
    uint64_t affinity = __builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL;
    if (cpu_number() != 0 || affinity != 1) {
        printf("LITHIUM CPU POWER skipped=1 reason=wrong_cpu affinity=0x%llx\n",
            affinity);
        goto restore;
    }
    /* All ranges are source-backed pages. Mapping does not enable hardware. */
    static const uint64_t physical[] = {
        0x099a2000ULL, 0x099d2000ULL, 0x099e0000ULL,
        0x09820000ULL, 0x09a10000ULL, 0x00074000ULL
    };
    vm_offset_t mapped[6] = {0}, allocation[6] = {0};
    struct r38_mmio_window windows[6] = {0};
    for (unsigned i = 0; i < 6; ++i) {
        /* ml_io_map may normally return a subpage pointer. ml_io_unmap
         * does not undo that adjustment: it directly calls pmap_remove.
         * Explicitly own a VM-page-aligned allocation and retain its extent.
         * In this 16K VM / 4K hardware-page port, passing the data pointer
         * (e.g. +0x2000) to unmap violates grouped-PTE alignment. */
        if (!r38_mmio_window_init(physical[i], 0x1000, PAGE_SIZE, &windows[i]))
            continue;
        /* A removable allocation must come from kernel_map, not the
         * permanent io_submap path used by ordinary ml_io_map. */
        allocation[i] = ml_io_map_unmappable(windows[i].physical_base,
            windows[i].mapped_bytes, VM_WIMG_IO);
        if (allocation[i])
            mapped[i] = allocation[i] + windows[i].data_offset;
        printf("LITHIUM CPU POWER map index=%u physical=0x%llx vm_page=%llu allocation=0x%lx bytes=%llu data_offset=%llu valid=%u\n",
            i, physical[i], (uint64_t)PAGE_SIZE, (uintptr_t)allocation[i],
            windows[i].mapped_bytes, windows[i].data_offset,
            (unsigned)(allocation[i] != 0));
    }
    static const struct r38_power_reg kryo[] = {
        {"LDO_VREF", 0x08}, {"RET_VREF", 0x10},
        {"PWR_GATE_MODE", 0x18}, {"PWR_GATE_DLY", 0x28},
        {"LDO_CFG", 0x40}, {"APM_CFG", 0x50}, {"PGS_STATUS", 0x60}
    };
    static const unsigned rows[] = {
        38, 39, 41, 64, 65, 66, 67, 68, 73, 74, 75, 76
    };
    static const char *saw_names[] = {
        "PMIC_STS", "VCTL", "AVS_CTL", "AVS_LIMIT"
    };
    unsigned valid = 0, unread = 0;
    printf("LITHIUM CPU POWER begin revision=38 affinity=0x%llx cookie=UNBOUND time=mach_absolute_ticks mmio_writes=0 regulator_writes=0 spmi_transactions=0 fault_guard=0 voltage_kind=programmed_only\n",
        affinity);
    for (unsigned pass = 0; pass < 2; ++pass) {
        uint64_t value = 0, version = 0;
        affinity = __builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL;
        if (cpu_number() != 0 || affinity != 1) {
            printf("LITHIUM CPU POWER pass=%u stopped=1 reason=wrong_cpu affinity=0x%llx\n",
                pass, affinity);
            break;
        }
        printf("LITHIUM CPU POWER pass=%u phase=begin t=%llu cookie=UNBOUND\n",
            pass, mach_absolute_time());
        for (unsigned c = 0; c < 2; ++c) {
            for (unsigned r = 0; r < sizeof(kryo) / sizeof(kryo[0]); ++r) {
                int ok = r38_power_load(pass, c ? "perfcl" : "pwrcl",
                    kryo[r].name, mapped[c], physical[c], kryo[r].offset,
                    32, &value);
                valid += ok; unread += !ok;
            }
        }
        static const struct r38_power_reg global[] = {
            {"PWR_OVERRIDE", 0x38}, {"MEMACC", 0xc0}
        };
        for (unsigned r = 0; r < sizeof(global) / sizeof(global[0]); ++r) {
            int ok = r38_power_load(pass, "apcc", global[r].name, mapped[2],
                physical[2], global[r].offset, 32, &value);
            valid += ok; unread += !ok;
        }
        int ok = r38_power_load(pass, "apcs", "VERSION", mapped[3],
            physical[3], 0xfd0, 32, &value);
        valid += ok; unread += !ok;
        ok = r38_power_load(pass, "saw", "VERSION", mapped[4],
            physical[4], 0xfd0, 32, &version);
        valid += ok; unread += !ok;
        unsigned major = (unsigned)(version >> 28) & 0xf;
        unsigned minor = (unsigned)(version >> 16) & 0xfff;
        unsigned offsets[4] = {0};
        int layout = 0;
        if (ok && ((major == 2 && minor == 1) ||
            (major == 3 && minor == 0) || (major == 1 && minor == 0))) {
            offsets[0] = 0x14; offsets[1] = 0x1c;
            offsets[2] = 0x20; offsets[3] = 0x24;
            layout = 1;
        } else if (ok && major == 4 && minor == 1) {
            offsets[0] = 0xc18; offsets[1] = 0x900;
            offsets[2] = 0x904; offsets[3] = 0x908;
            layout = 2;
        }
        printf("LITHIUM CPU POWER pass=%u block=saw major=%u minor=%u layout=%d version_valid=%d\n",
            pass, major, minor, layout, ok);
        for (unsigned r = 0; r < 4; ++r) {
            if (!layout) {
                uint64_t t = mach_absolute_time();
                printf("LITHIUM CPU POWER pass=%u block=saw reg=%s pa=UNRESOLVED bits=32 valid=0 raw=UNREAD t0=%llu t1=%llu reason=layout_unresolved\n",
                    pass, saw_names[r], t, t);
                ++unread;
                continue;
            }
            ok = r38_power_load(pass, "saw", saw_names[r], mapped[4],
                physical[4], offsets[r], 32, &value);
            valid += ok; unread += !ok;
        }
        for (unsigned r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r) {
            /* Native aligned 64-bit fuse row: no split/implicit zero fill. */
            ok = r38_power_load(pass, "qfprom", "ROW", mapped[5],
                physical[5], rows[r] * 8u, 64, &value);
            valid += ok; unread += !ok;
        }
        printf("LITHIUM CPU POWER pass=%u phase=end t=%llu cookie=UNBOUND\n",
            pass, mach_absolute_time());
    }
    for (unsigned i = 0; i < 6; ++i) {
        if (!allocation[i]) continue;
        printf("LITHIUM CPU POWER unmap index=%u allocation=0x%lx bytes=%llu\n",
            i, (uintptr_t)allocation[i], windows[i].mapped_bytes);
        ml_io_unmap(allocation[i], windows[i].mapped_bytes);
    }
    printf("LITHIUM CPU POWER end revision=38 valid=%u unread=%u cookie=UNBOUND mmio_writes=0 regulator_writes=0 fixed_frequency_applied=0\n",
        valid, unread);
restore:
    thread_bind(original);
    thread_block(THREAD_CONTINUE_NULL);
}
#endif
