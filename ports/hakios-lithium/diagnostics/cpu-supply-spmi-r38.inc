/* SPDX-License-Identifier: MIT
 * Default-off, RAM-only S11 digital supply snapshot.
 * No regulator write, PLL write, IRQ/config change, reset or retry.
 * SPMI EXT_READL is issued by writing the observer CMD register.
 * Register meanings: pinned MiCode spm-regulator.c:81-94,175-217,622-678.
 * Board: lithium-stock.dts:7134-7141,7749-7753,7823-7829.
 * Call once from the prelaunch diagnostic thread, bound to phys1.
 * Faulting/stalled MMIO cannot be rescued by this software timeout.
 */
#ifndef LITHIUM_CPU_SUPPLY_SPMI_R38_INC
#define LITHIUM_CPU_SUPPLY_SPMI_R38_INC
#include "cpu-spmi-read-r38.h"
#include "cpu-mmio-window-r38.h"
#include <arm/memory_types.h>
#include <mach/vm_param.h>
#include <kern/clock.h>

struct r38s_mapping {
    struct r38_mmio_window window;
    vm_offset_t allocation;
};
struct r38s_context {
    struct r38s_mapping core, observer;
    uint64_t observer_pa;
    uint32_t allowed_cmd;
    unsigned reads, writes;
};
static volatile unsigned r38s_used;

static int
r38s_on_cpu(void)
{
    return cpu_number() == 0 &&
        (__builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL) == 1;
}

static int
r38s_map(struct r38s_mapping *m, uint64_t physical, uint64_t bytes)
{
    if (!r38_mmio_window_init(physical, bytes, PAGE_SIZE, &m->window))
        return 0;
    m->allocation = ml_io_map_unmappable(m->window.physical_base,
        m->window.mapped_bytes, VM_WIMG_IO);
    printf("LITHIUM CPU S11 map pa=0x%llx allocation=0x%lx bytes=%llu data_offset=%llu valid=%u\n",
        physical, (uintptr_t)m->allocation, m->window.mapped_bytes,
        m->window.data_offset, (unsigned)(m->allocation != 0));
    return m->allocation != 0;
}

static vm_offset_t
r38s_pointer(struct r38s_mapping *m, uint64_t pa)
{
    if (!m->allocation || (pa & 3) || pa < m->window.physical_base ||
        pa - m->window.physical_base > m->window.mapped_bytes - 4)
        return 0;
    return m->allocation + pa - m->window.physical_base;
}

static int
r38s_read(void *opaque, uint64_t pa, uint32_t *out)
{
    struct r38s_context *c = opaque;
    vm_offset_t pointer = 0;
    if (!out || !r38s_on_cpu()) return -1;
    if (pa == R38_SPMI_CORE_PA ||
        (pa >= R38_SPMI_CORE_PA + 0x800 &&
         pa < R38_SPMI_CORE_PA + 0x800 + 4 * R38_SPMI_CHANNELS))
        pointer = r38s_pointer(&c->core, pa);
    else if (c->observer_pa &&
        (pa == c->observer_pa + 4 || pa == c->observer_pa + 8 ||
         pa == c->observer_pa + 0x18 || pa == c->observer_pa + 0x1c))
        pointer = r38s_pointer(&c->observer, pa);
    if (!pointer) return -2;
    __asm__ volatile("dsb sy" ::: "memory");
    *out = *(volatile uint32_t *)pointer;
    __asm__ volatile("dsb sy" ::: "memory");
    ++c->reads;
    return 0;
}

static int
r38s_write_read_command(void *opaque, uint64_t pa, uint32_t value)
{
    struct r38s_context *c = opaque;
    if (!r38s_on_cpu() || !c->observer_pa || pa != c->observer_pa ||
        value != c->allowed_cmd || (value >> 27) != 1)
        return -1;
    vm_offset_t pointer = r38s_pointer(&c->observer, pa);
    if (!pointer) return -2;
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)pointer = value;
    __asm__ volatile("dsb sy" ::: "memory");
    ++c->writes;
    return 0;
}

static int
r38s_time_us(void *opaque, uint64_t *out)
{
    (void)opaque;
    uint64_t ns;
    if (!out || !r38s_on_cpu()) return -1;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    *out = ns / 1000;
    return 0;
}

static int
r38s_delay_us(void *opaque, uint32_t us)
{
    (void)opaque;
    if (!r38s_on_cpu() || us > 1000) return -1;
    delay(us);
    return 0;
}

static void
lithium_cpu_supply_spmi_r38(void)
{
    unsigned enabled = 0, ufs = 99, boot = 99, cpus = 0, ram = 0;
    PE_parse_boot_argn("cpu_spmi_diag", &enabled, sizeof(enabled));
    if (enabled != 1) return;
    PE_parse_boot_argn("ufs", &ufs, sizeof(ufs));
    PE_parse_boot_argn("bootcpu", &boot, sizeof(boot));
    PE_parse_boot_argn("cpus", &cpus, sizeof(cpus));
    PE_parse_boot_argn("ram_smp_return", &ram, sizeof(ram));
    if (ufs != 0 || boot != 1 || cpus != 3 || ram != 1 ||
        !ml_get_interrupts_enabled() || get_preemption_level() != 0 ||
        !__sync_bool_compare_and_swap(&r38s_used, 0, 1)) {
        printf("LITHIUM CPU S11 skipped=1 reason=requires_once_ram_smp\n");
        return;
    }
    processor_t target = cpu_to_processor(0);
    if (!target) return;
    processor_t previous = thread_bind(target);
    thread_block(THREAD_CONTINUE_NULL);
    struct r38s_context context = {0};
    const struct r38_spmi_io io = {
        &context, r38s_read, r38s_write_read_command,
        r38s_time_us, r38s_delay_us
    };
    struct r38_spmi_result r = {0};
    struct r38_spmi_route route = {0};
    static const unsigned offsets[] = {0x04, 0x05, 0x40, 0x41, 0x45, 0x61};
    static const char *names[] = {"TYPE", "SUBTYPE", "RANGE", "VSET", "MODE", "STEP"};
    uint8_t first[6] = {0};
    unsigned valid = 0, stable = 1;
    enum r38_spmi_error error = R38_SPMI_ARGUMENT;
    printf("LITHIUM CPU S11 begin affinity=0x%llx once=1 ee=0 sid=1 ppid=0x132 regulator_writes=0 clock_writes=0 fault_guard=0\n",
        __builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL);
    if (!r38s_on_cpu() || !r38s_map(&context.core, R38_SPMI_CORE_PA, 0x1000))
        goto cleanup;
    error = r38_spmi_probe(&io, &r);
    printf("LITHIUM CPU S11 route error=%u version=0x%08x apid=%u mapping=0x%08x matches=%u observer_pa=0x%llx reads=%u\n",
        error, r.route.version, r.route.apid, r.route.mapping_raw,
        r.matches, r.route.observer_pa, r.reads);
    if (error) goto cleanup;
    route = r.route;
    if (!r38s_map(&context.observer, route.observer_pa, route.observer_bytes)) {
        error = R38_SPMI_ARGUMENT;
        goto cleanup;
    }
    context.observer_pa = route.observer_pa;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < 6; ++i) {
            context.allowed_cmd = (1u << 27) | (offsets[i] << 4);
            error = r38_spmi_read(&io, &route, 1, 0x3200 + offsets[i], 1, &r);
            printf("LITHIUM CPU S11 pass=%u reg=%s addr=0x%x valid=%u raw=0x%02x error=%u phase=%u status_valid=%u status=0x%x command_attempted=%u completed=%u polls=%u start_us=%llu end_us=%llu\n",
                pass, names[i], 0x3200 + offsets[i], r.data_valid,
                r.data[0], error, r.phase, r.status_valid, r.status_raw,
                r.cmd_attempted, r.cmd_completed, r.polls, r.start_us, r.last_us);
            if (error) goto cleanup;
            ++valid;
            if (!pass) first[i] = r.data[0];
            else if (first[i] != r.data[0]) stable = 0;
        }
        if (!pass) delay(1000);
    }
cleanup:
    for (unsigned i = 0; i < 2; ++i) {
        struct r38s_mapping *m = i ? &context.observer : &context.core;
        if (!m->allocation) continue;
        printf("LITHIUM CPU S11 unmap index=%u allocation=0x%lx bytes=%llu\n",
            i, (uintptr_t)m->allocation, m->window.mapped_bytes);
        ml_io_unmap(m->allocation, m->window.mapped_bytes);
    }
    printf("LITHIUM CPU S11 end error=%u valid=%u stable=%u reads=%u observer_cmd_writes=%u regulator_writes=0 clock_writes=0 voltage_uv=UNVERIFIED\n",
        error, valid, valid == 12 ? stable : 0, context.reads, context.writes);
    thread_bind(previous);
    thread_block(THREAD_CONTINUE_NULL);
}
#endif
