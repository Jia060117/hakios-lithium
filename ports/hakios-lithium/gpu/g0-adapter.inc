/* SPDX-License-Identifier: MIT
 * Opt-in G0 only: exact GCC/GPUCC loads in an ordinary bound worker.
 * No GPU aperture, SMMU, IRQ, clock or regulator writes. A deadline bounds
 * returning callbacks, not an external abort or a never-completing MMIO load.
 */
#ifndef LITHIUM_GPU_G0_ADAPTER_INC
#define LITHIUM_GPU_G0_ADAPTER_INC
#ifndef LITHIUM_GPU_G0_HOST_TEST
#include <arm/memory_types.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <mach/vm_param.h>
#include <machine/machine_routines.h>
#include <vm/memory_types.h>
/* The BSD public machine header hides these behind MACH_KERNEL_PRIVATE.
 * Match the pinned osfmk/arm/machine_routines.h declarations without changing
 * the translation unit's visibility or the mapping API pairing. */
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, uint32_t);
extern void ml_io_unmap(vm_offset_t, vm_size_t);
#endif
#include "g0-core.h"
#include "../diagnostics/cpu-mmio-window-r38.h"
#include "../return-fastboot/ram-smp-profile.h"

/* GCC registers lie in two distinct VM pages. GPUCC lives in a third.
 * The mapping extent grants no extra register access: read32 checks the
 * exact nine-address allowlist before dereferencing any mapped pointer. */
static const uint64_t lg0_mapping_addresses[] = {
    UINT64_C(0x008c4000), UINT64_C(0x00309000), UINT64_C(0x00346000)
};
static struct {
    struct r38_mmio_window windows[3];
    vm_offset_t allocation[3];
    unsigned loads;
    struct lg0_result result;
} lg0_adapter;

static bool
lg0_adapter_read32(void *opaque, uint32_t pa, uint32_t *out)
{
    (void)opaque;
    if (!out || lg0_adapter.loads >= LG0_READ_CAP) return false;
    bool allowed = false;
    for (unsigned i = 0; i < LG0_REG_COUNT; ++i)
        if (lg0_regs[i].address == pa) allowed = true;
    if (!allowed) return false;
    for (unsigned i = 0; i < 3; ++i) {
        const struct r38_mmio_window *w = &lg0_adapter.windows[i];
        if (!lg0_adapter.allocation[i] || pa < w->physical_base ||
            (uint64_t)pa - w->physical_base > w->mapped_bytes - sizeof(*out))
            continue;
        ++lg0_adapter.loads;
        LG0_READ_BARRIER();
        *out = LG0_MMIO_LOAD(lg0_adapter.allocation[i] +
            ((uint64_t)pa - w->physical_base));
        LG0_READ_BARRIER();
        return true;
    }
    return false;
}

static uint64_t
lg0_adapter_now_us(void *opaque)
{
    (void)opaque;
    uint64_t nanoseconds = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &nanoseconds);
    return nanoseconds / 1000;
}

static bool
lg0_adapter_admit(void)
{
    const char *args = PE_boot_args();
    const size_t length = args ? strnlen(args, 4096) : 0;
    unsigned timer = 0;
    PE_parse_boot_argn("loader_return_after", &timer, sizeof(timer));
    return args && length < 4096 && timer == 180 &&
        rfbt_ram_smp_profile(args, length, 1);
}

static __attribute__((noinline)) void
lithium_gpu_g0_probe(void)
{
    /* One bounded worker dispatch per boot. The static sample avoids adding
     * another diagnostic record to the legacy touch worker's large frame. */
    if (!lg0_adapter_admit()) {
        printf("LITHIUM GPU stage=0 status=blocked reason=requires_isolated_ram_smp_180 mmio=0 gpu_writes=0\n");
        return;
    }
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU stage=0 status=blocked reason=cpu_bind mmio=0 gpu_writes=0\n");
        return;
    }
    printf("LITHIUM GPU stage=0 begin logical=%d affinity=0x%llx max_logical=%d register_count=%u passes=%u read_cap=%u deadline_us=%llu gpu_aperture=unread smmu=unread gpu_writes=0 irq_attached=0\n",
        cpu_number(), (unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),
        ml_get_max_cpu_number(), LG0_REG_COUNT, LG0_PASSES, LG0_READ_CAP,
        (unsigned long long)LG0_DEADLINE_US);
    for (unsigned i = 0; i < 3; ++i) {
        if (!r38_mmio_window_init(lg0_mapping_addresses[i], 0x1000,
                PAGE_SIZE, &lg0_adapter.windows[i])) continue;
        const struct r38_mmio_window *w = &lg0_adapter.windows[i];
        lg0_adapter.allocation[i] = ml_io_map_unmappable(w->physical_base,
            w->mapped_bytes, VM_WIMG_IO);
        printf("LITHIUM GPU stage=0 map index=%u physical=0x%llx bytes=%llu allocation=0x%lx valid=%u\n",
            i, (unsigned long long)w->physical_base,
            (unsigned long long)w->mapped_bytes,
            (unsigned long)lg0_adapter.allocation[i],
            (unsigned)(lg0_adapter.allocation[i] != 0));
    }
    const struct lg0_io io = {NULL, lg0_adapter_read32, lg0_adapter_now_us};
    const bool ok = lg0_run(&io, &lg0_adapter.result);
    const struct lg0_result *r = &lg0_adapter.result;
    for (unsigned pass = 0; pass < LG0_PASSES; ++pass)
        for (unsigned i = 0; i < LG0_REG_COUNT; ++i) {
            const unsigned valid = (r->valid[pass] >> i) & 1;
            if (valid)
                printf("LITHIUM GPU stage=0 pass=%u reg=%s pa=0x%08x valid=1 raw=0x%08x stable=%u\n",
                    pass, lg0_regs[i].name, lg0_regs[i].address, r->raw[pass][i],
                    (r->stable_mask >> i) & 1);
            else
                printf("LITHIUM GPU stage=0 pass=%u reg=%s pa=0x%08x valid=0 raw=UNREAD stable=0\n",
                    pass, lg0_regs[i].name, lg0_regs[i].address);
        }
    for (unsigned i = 0; i < 3; ++i) {
        if (!lg0_adapter.allocation[i]) continue;
        printf("LITHIUM GPU stage=0 unmap index=%u allocation=0x%lx bytes=%llu\n",
            i, (unsigned long)lg0_adapter.allocation[i],
            (unsigned long long)lg0_adapter.windows[i].mapped_bytes);
        ml_io_unmap(lg0_adapter.allocation[i],
            lg0_adapter.windows[i].mapped_bytes);
        lg0_adapter.allocation[i] = 0;
    }
    printf("LITHIUM GPU stage=0 end status=%s valid0=0x%x valid1=0x%x stable_mask=0x%x unstable_mask=0x%x reads=%u loads=%u read_failures=%u completed_passes=%u errors=0x%x elapsed_us=%llu gpu_writes=0 power_verified=0 chip_id=UNREAD\n",
        ok ? "snapshot_complete" : "snapshot_incomplete", r->valid[0],
        r->valid[1], r->stable_mask, r->unstable_mask, r->reads,
        lg0_adapter.loads, r->read_failures, r->passes_completed, r->errors,
        (unsigned long long)(r->ended_us >= r->started_us ?
            r->ended_us - r->started_us : 0));
    lithium_touch_cpu_leave(&scope);
}
#endif
