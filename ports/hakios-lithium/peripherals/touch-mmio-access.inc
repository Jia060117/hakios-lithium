/* Lithium-only read observation backend. Include once from the ordinary input
 * thread translation unit. It does not grant any lqt_io.verified bit and does
 * not start a transfer. In particular it does NOT promise to recover SError,
 * XPU/platform fatal errors, or an interconnect access which never completes.
 * Do not enable on a device merely because the host tests/build pass. */
#include "touch-mmio-policy.h"

/* A production caller must link the actual exception-hook provider. Merely
 * including this file without installing that hook must fail at link time,
 * rather than silently issuing an unprotected LDR. The hook owns this symbol. */
extern const uint32_t lithium_touch_mmio_abort_hook_abi;

#if !defined(LITHIUM_TOUCH_MMIO_HOST_TEST)
#include <kern/clock.h>
#include <kern/thread.h>
#include <machine/machine_routines.h>

/* x0 is the sole MMIO address, x1 points at an ordinary kernel stack word.
 * Only the LDR has a recovery PC; failure cannot replay it or publish data.
 * Barriers order accesses but do not supply a hardware bus timeout. */
extern int lithium_touch_probe_read32(uintptr_t, uint32_t *);
extern const char lithium_touch_probe_load[], lithium_touch_probe_failed[];
__asm__(
    ".text\n.p2align 2\n"
    ".globl _lithium_touch_probe_read32\n"
    ".private_extern _lithium_touch_probe_read32\n"
    "_lithium_touch_probe_read32:\n"
    "dmb sy\n"
    ".globl _lithium_touch_probe_load\n"
    ".private_extern _lithium_touch_probe_load\n"
    "_lithium_touch_probe_load:\n"
    "ldr w2, [x0]\n"
    "dmb sy\n"
    "str w2, [x1]\n"
    "mov w0, #1\nret\n"
    ".globl _lithium_touch_probe_failed\n"
    ".private_extern _lithium_touch_probe_failed\n"
    "_lithium_touch_probe_failed:\nmov w0, #0\nret\n");
#endif

struct lithium_touch_mmio_window {
    uint32_t physical;
    uintptr_t address;
};

struct lithium_touch_mmio_access {
    struct lithium_touch_mmio_window windows[9];
    struct lithium_touch_mmio_guard guard;
    uint64_t deadline_us, last_us;
    uint32_t fault_esr, fault_physical, reads, mappings;
    bool claimed, poisoned, clock_seen;
};

/* One boot-lifetime provider and one owner; no reuse/re-init after a failure. */
static struct lithium_touch_mmio_access lithium_touch_mmio;

static uint64_t
lithium_touch_mmio_now(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000;
}

static bool
lithium_touch_mmio_open(struct lithium_touch_mmio_access *access)
{
    const uint64_t now = lithium_touch_mmio_now();
    if (access->poisoned || (access->clock_seen && now < access->last_us) ||
        now >= access->deadline_us) {
        access->poisoned = true;
        return false;
    }
    access->last_us = now;
    access->clock_seen = true;
    return true;
}

static bool
lithium_touch_mmio_claim(uint64_t deadline_us)
{
    struct lithium_touch_mmio_access *access = &lithium_touch_mmio;
    if (lithium_touch_mmio_abort_hook_abi != 1 || access->claimed ||
        access->poisoned || !deadline_us ||
        ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        LITHIUM_PERIPHERAL_AFFINITY() != 1 || ml_get_max_cpu_number() != 0)
        return false;
    const uintptr_t owner = (uintptr_t)current_thread();
    if (!owner) return false;
    access->deadline_us = deadline_us;
    access->guard.owner = owner;
    access->guard.instruction = (uintptr_t)lithium_touch_probe_load;
    access->guard.recovery = (uintptr_t)lithium_touch_probe_failed;
    access->claimed = true;
    return lithium_touch_mmio_open(access);
}

/* Called only by the narrowly scoped DABORT_EL1 recovery hook. No allocation,
 * printing, access to MMIO, retry, or change to generic copyio recovery. The
 * saved x0 operand must also match FAR: a nearby LDR is never accepted. */
uintptr_t lithium_touch_mmio_recover_read(uintptr_t instruction, uint32_t esr,
    uintptr_t address, uintptr_t operand, uintptr_t owner, bool thread_mode);

uintptr_t
lithium_touch_mmio_recover_read(uintptr_t instruction, uint32_t esr,
    uintptr_t address, uintptr_t operand, uintptr_t owner, bool thread_mode)
{
    struct lithium_touch_mmio_access *access = &lithium_touch_mmio;
    if (!__atomic_load_n(&access->guard.armed, __ATOMIC_ACQUIRE) ||
        operand != address || !access->claimed || access->poisoned) return 0;
    const struct lithium_touch_mmio_fault fault = {
        .instruction = instruction, .esr = esr, .address = address,
        .owner = owner, .affinity = LITHIUM_PERIPHERAL_AFFINITY(),
        .max_cpu = ml_get_max_cpu_number(), .thread_mode = thread_mode
    };
    if (!lithium_touch_mmio_fault_matches(&access->guard, &fault)) return 0;
    access->fault_esr = esr;
    access->fault_physical = access->guard.physical;
    access->poisoned = true;
    __atomic_store_n(&access->guard.armed, false, __ATOMIC_RELEASE);
    return access->guard.recovery;
}

static bool
lithium_touch_mmio_read_observation(void *unused, uint32_t physical,
    uint64_t deadline_us, uint32_t *out)
{
    (void)unused;
    struct lithium_touch_mmio_access *access = &lithium_touch_mmio;
    if (!out || !access->claimed || !access->guard.owner || access->poisoned ||
        __atomic_load_n(&access->guard.armed, __ATOMIC_ACQUIRE) ||
        deadline_us != access->deadline_us || !lithium_touch_mmio_open(access) ||
        (uintptr_t)current_thread() != access->guard.owner ||
        ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        LITHIUM_PERIPHERAL_AFFINITY() != 1 || ml_get_max_cpu_number() != 0 ||
        !lithium_touch_mmio_observation_allowed(physical) || access->reads >= 13)
        return false;
    const uint32_t page = physical & ~UINT32_C(0xfff);
    uintptr_t mapping = 0;
    for (uint32_t i = 0; i < access->mappings; ++i)
        if (access->windows[i].physical == page) mapping = access->windows[i].address;
    if (!mapping) {
        if (access->mappings >= sizeof(access->windows) / sizeof(access->windows[0]))
            return false;
        /* ml_io_map uses VM_WIMG_IO. Kernel page rounding/offset is handled by
         * that API; only the exact allowlisted words can be read here. */
        mapping = ml_io_map(page, 0x1000);
        if (!mapping || !lithium_touch_mmio_open(access)) {
            access->poisoned = true;
            return false;
        }
        access->windows[access->mappings++] =
            (struct lithium_touch_mmio_window){page, mapping};
    }
    access->guard.address = mapping + (physical - page);
    access->guard.physical = physical;
    ++access->reads;
    __atomic_store_n(&access->guard.armed, true, __ATOMIC_RELEASE);
    uint32_t value = 0;
    const int ok = lithium_touch_probe_read32(access->guard.address, &value);
    __atomic_store_n(&access->guard.armed, false, __ATOMIC_RELEASE);
    if (!ok || !lithium_touch_mmio_open(access)) {
        access->poisoned = true;
        return false;
    }
    *out = value;
    return true;
}
