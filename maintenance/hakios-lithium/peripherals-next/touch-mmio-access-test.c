/* Host contract tests of the exact production include, not a second backend.
 * Fixed RAM windows and symbol stand-ins cannot establish Device attributes,
 * hardware ownership, exception entry, or a bounded bus access. Each reset
 * represents a separate boot, never a production reclaim.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error This test requires assertions
#endif

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define DEADLINE UINT64_C(1000)
#define SENTINEL UINT32_C(0xfedcba98)
#define PRECISE_READ_ABORT UINT32_C(0x96000010)

static uint64_t mach_absolute_time(void);
static void absolutetime_to_nanoseconds(uint64_t, uint64_t *);
static bool ml_at_interrupt_context(void);
static bool ml_get_interrupts_enabled(void);
static int ml_get_max_cpu_number(void);
static void *current_thread(void);
static uint64_t host_affinity(void);
static uintptr_t ml_io_map(uint32_t, size_t);
static int lithium_touch_probe_read32(uintptr_t, uint32_t *);

/* Addresses only: these symbols do not emulate execution of the ARM64 LDR. */
_Alignas(4) const char lithium_touch_probe_load[8] = {0};
_Alignas(4) const char lithium_touch_probe_failed[8] = {0};
/* Host stand-in only; production ownership belongs to the actual sleh hook. */
#ifndef LITHIUM_TOUCH_MMIO_HOST_OMIT_ABORT_HOOK_ABI
const uint32_t lithium_touch_mmio_abort_hook_abi = 1;
#endif
#define LITHIUM_TOUCH_MMIO_HOST_TEST 1
#define LITHIUM_PERIPHERAL_AFFINITY() host_affinity()
#include "../../../ports/hakios-lithium/peripherals/touch-mmio-access.inc"

static const uint32_t allowed[] = {
    0x00325004, 0x00352004, 0x00330000, 0x00330008, 0x00330020, 0x00330024,
    0x01067000, 0x01068000, 0x01079004, 0x0108d004,
    0x075ba004, 0x075ba030, 0x075ba404
};
static const uint32_t pages[] = {
    0x00325000, 0x00352000, 0x00330000, 0x01067000,
    0x01068000, 0x01079000, 0x0108d000, 0x075ba000
};

enum fault_edit {
    FAULT_EXACT, FAULT_FAR, FAULT_OPERAND, FAULT_OWNER, FAULT_NULL_OWNER,
    FAULT_PC, FAULT_RECOVERY_PC, FAULT_NULL_PC, FAULT_NOT_THREAD,
    FAULT_AFFINITY, FAULT_CPU, FAULT_UNARMED, FAULT_UNCLAIMED,
    FAULT_POISONED, FAULT_PHYSICAL, FAULT_NO_RECOVERY, FAULT_SAME_PC
};

static struct {
    struct { uint32_t words[1024]; bool mapped; } windows[8];
    struct { uint32_t page; size_t size; } maps[32];
    uintptr_t operands[32];
    uint64_t now_ns, affinity, map_time_ns, probe_time_ns;
    unsigned map_calls, probes, time_calls, conversions, fail_map_at;
    unsigned fail_probe_at, recover_calls;
    void *owner;
    int max_cpu;
    bool interrupt_context, interrupts_enabled, change_map_time;
    bool change_probe_time, inject_fault, expect_recovery, probe_lies;
    bool reenter;
    uint32_t esr;
    enum fault_edit edit;
} fake;
static unsigned owner_tokens[2], scenarios;

static uint32_t value_for(uint32_t physical)
{
    return physical ^ UINT32_C(0xa5c3907e);
}

static void new_boot(void)
{
    memset(&lithium_touch_mmio, 0, sizeof(lithium_touch_mmio));
    memset(&fake, 0, sizeof(fake));
    fake.now_ns = UINT64_C(100000);
    fake.affinity = 1;
    fake.owner = &owner_tokens[0];
    fake.interrupts_enabled = true;
    fake.esr = PRECISE_READ_ABORT;
    for (size_t p = 0; p < ARRAY_COUNT(pages); ++p)
        for (size_t w = 0; w < ARRAY_COUNT(fake.windows[p].words); ++w)
            fake.windows[p].words[w] = value_for(pages[p] + (uint32_t)w * 4);
    ++scenarios;
}

static uint64_t mach_absolute_time(void)
{
    ++fake.time_calls;
    return fake.now_ns;
}

static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *ns)
{
    assert(ns != NULL);
    ++fake.conversions;
    *ns = ticks; /* The host clock uses one nanosecond per tick. */
}

static bool ml_at_interrupt_context(void) { return fake.interrupt_context; }
static bool ml_get_interrupts_enabled(void) { return fake.interrupts_enabled; }
static int ml_get_max_cpu_number(void) { return fake.max_cpu; }
static void *current_thread(void) { return fake.owner; }
static uint64_t host_affinity(void) { return fake.affinity; }

static uintptr_t ml_io_map(uint32_t page, size_t size)
{
    assert(fake.map_calls < ARRAY_COUNT(fake.maps));
    fake.maps[fake.map_calls].page = page;
    fake.maps[fake.map_calls++].size = size;
    assert((page & 0xfff) == 0 && size == 0x1000);
    assert(lithium_touch_mmio.claimed && !lithium_touch_mmio.poisoned);
    assert(!lithium_touch_mmio.guard.armed);
    if (fake.change_map_time) fake.now_ns = fake.map_time_ns;
    if (fake.map_calls == fake.fail_map_at) return 0;
    for (size_t i = 0; i < ARRAY_COUNT(pages); ++i) {
        if (page != pages[i]) continue;
        assert(!fake.windows[i].mapped); /* Cache must prevent a second map. */
        fake.windows[i].mapped = true;
        return (uintptr_t)fake.windows[i].words;
    }
    assert(!"map outside the fixed allowlisted windows");
    return 0;
}

static uintptr_t call_recovery(uintptr_t instruction, uint32_t esr,
    uintptr_t far, uintptr_t operand, uintptr_t owner, bool thread_mode)
{
    ++fake.recover_calls;
    return lithium_touch_mmio_recover_read(instruction, esr, far, operand,
        owner, thread_mode);
}

static int lithium_touch_probe_read32(uintptr_t address, uint32_t *value)
{
    struct lithium_touch_mmio_access *access = &lithium_touch_mmio;
    assert(value && access->claimed && access->guard.armed);
    assert(!access->poisoned && address == access->guard.address);
    assert(access->guard.owner == (uintptr_t)fake.owner);
    assert(access->guard.instruction == (uintptr_t)lithium_touch_probe_load);
    assert(access->guard.recovery == (uintptr_t)lithium_touch_probe_failed);
    assert(!fake.interrupt_context && fake.interrupts_enabled);
    assert(fake.affinity == 1 && fake.max_cpu == 0);
    assert(fake.probes < ARRAY_COUNT(fake.operands));
    fake.operands[fake.probes++] = address;
    assert(access->reads == fake.probes && access->reads <= 13);

    uint32_t loaded = 0;
    bool found = false;
    for (size_t i = 0; i < ARRAY_COUNT(pages); ++i) {
        uintptr_t base = (uintptr_t)fake.windows[i].words;
        if (!fake.windows[i].mapped || address < base || address - base >= 0x1000)
            continue;
        assert((address - base) % 4 == 0);
        assert(pages[i] + (uint32_t)(address - base) == access->guard.physical);
        loaded = fake.windows[i].words[(address - base) / 4];
        found = true;
        break;
    }
    assert(found);
    if (fake.reenter) {
        uint32_t nested = SENTINEL;
        assert(!lithium_touch_mmio_claim(DEADLINE));
        assert(!lithium_touch_mmio_read_observation(NULL, allowed[0],
            DEADLINE, &nested));
        assert(nested == SENTINEL && fake.probes == 1 && fake.map_calls == 1);
    }
    if (fake.change_probe_time) fake.now_ns = fake.probe_time_ns;
    if (fake.inject_fault) {
        uintptr_t pc = access->guard.instruction, far = address;
        uintptr_t operand = address, owner = access->guard.owner;
        bool thread_mode = true;
        switch (fake.edit) {
        case FAULT_EXACT: break;
        case FAULT_FAR: far += 4; operand = far; break;
        case FAULT_OPERAND: operand += 4; break;
        case FAULT_OWNER: owner = (uintptr_t)&owner_tokens[1]; break;
        case FAULT_NULL_OWNER: owner = 0; break;
        case FAULT_PC: pc += 4; break;
        case FAULT_RECOVERY_PC: pc = access->guard.recovery; break;
        case FAULT_NULL_PC: pc = 0; break;
        case FAULT_NOT_THREAD: thread_mode = false; break;
        case FAULT_AFFINITY: fake.affinity = 0x100; break;
        case FAULT_CPU: fake.max_cpu = 1; break;
        case FAULT_UNARMED: access->guard.armed = false; break;
        case FAULT_UNCLAIMED: access->claimed = false; break;
        case FAULT_POISONED: access->poisoned = true; break;
        case FAULT_PHYSICAL: access->guard.physical = 0x075ba00c; break;
        case FAULT_NO_RECOVERY: access->guard.recovery = 0; break;
        case FAULT_SAME_PC: access->guard.recovery = pc; break;
        }
        const struct lithium_touch_mmio_access before = *access;
        const unsigned maps_before = fake.map_calls, probes_before = fake.probes;
        uintptr_t recovery = call_recovery(pc, fake.esr, far, operand, owner,
            thread_mode);
        assert(fake.map_calls == maps_before && fake.probes == probes_before);
        if (fake.expect_recovery) {
            assert(recovery == (uintptr_t)lithium_touch_probe_failed);
            assert(access->poisoned && !access->guard.armed && access->claimed);
            assert(access->fault_esr == fake.esr);
            assert(access->fault_physical == before.guard.physical);
            assert(access->reads == before.reads && access->mappings == before.mappings);
            assert(call_recovery(pc, fake.esr, far, operand, owner, true) == 0);
        } else {
            assert(recovery == 0);
            /* Rejection must neither swallow the abort nor alter the guard. */
            assert(memcmp(access, &before, sizeof(before)) == 0);
        }
        *value = loaded; /* Even a faulty helper must not publish its stack word. */
        return fake.probe_lies ? 1 : 0;
    }
    *value = loaded;
    return fake.probes == fake.fail_probe_at ? 0 : 1;
}

static void claim(void)
{
    assert(lithium_touch_mmio_claim(DEADLINE));
    assert(lithium_touch_mmio.claimed && !lithium_touch_mmio.poisoned);
    assert(lithium_touch_mmio.deadline_us == DEADLINE);
    assert(lithium_touch_mmio.guard.owner == (uintptr_t)fake.owner);
    assert(lithium_touch_mmio.clock_seen && lithium_touch_mmio.last_us == 100);
    assert(fake.map_calls == 0 && fake.probes == 0);
}

static void read_ok(uint32_t physical)
{
    uint32_t out = SENTINEL;
    assert(lithium_touch_mmio_read_observation(NULL, physical, DEADLINE, &out));
    assert(out == value_for(physical));
    assert(!lithium_touch_mmio.guard.armed && !lithium_touch_mmio.poisoned);
}

static void read_fails(uint32_t physical, uint64_t deadline)
{
    uint32_t out = SENTINEL;
    assert(!lithium_touch_mmio_read_observation(NULL, physical, deadline, &out));
    assert(out == SENTINEL);
}

static void no_retry_after_poison(void)
{
    assert(lithium_touch_mmio.poisoned && !lithium_touch_mmio.guard.armed);
    const struct lithium_touch_mmio_access before = lithium_touch_mmio;
    const unsigned maps = fake.map_calls, probes = fake.probes;
    const unsigned clocks = fake.time_calls;
    fake.now_ns = 100000;
    fake.owner = &owner_tokens[0];
    fake.affinity = 1;
    fake.max_cpu = 0;
    fake.interrupt_context = false;
    fake.interrupts_enabled = true;
    fake.fail_map_at = fake.fail_probe_at = 0;
    fake.inject_fault = fake.change_map_time = fake.change_probe_time = false;
    for (unsigned i = 0; i < 3; ++i) {
        assert(!lithium_touch_mmio_claim(DEADLINE + i));
        read_fails(allowed[0], DEADLINE);
        read_fails(allowed[12], DEADLINE);
        assert(call_recovery(before.guard.instruction, PRECISE_READ_ABORT,
            before.guard.address, before.guard.address, before.guard.owner, true) == 0);
    }
    assert(fake.map_calls == maps && fake.probes == probes);
    assert(fake.time_calls == clocks);
    assert(memcmp(&lithium_touch_mmio, &before, sizeof(before)) == 0);
}

enum gate {
    GATE_OWNER, GATE_NO_OWNER, GATE_IRQ_CONTEXT, GATE_IRQ_DISABLED,
    GATE_CPU_NEGATIVE, GATE_CPU_SMP, GATE_AFFINITY_ZERO,
    GATE_AFFINITY_100, GATE_AFFINITY_101
};

static void set_gate(enum gate gate)
{
    switch (gate) {
    case GATE_OWNER: fake.owner = &owner_tokens[1]; break;
    case GATE_NO_OWNER: fake.owner = NULL; break;
    case GATE_IRQ_CONTEXT: fake.interrupt_context = true; break;
    case GATE_IRQ_DISABLED: fake.interrupts_enabled = false; break;
    case GATE_CPU_NEGATIVE: fake.max_cpu = -1; break;
    case GATE_CPU_SMP: fake.max_cpu = 1; break;
    case GATE_AFFINITY_ZERO: fake.affinity = 0; break;
    case GATE_AFFINITY_100: fake.affinity = 0x100; break;
    case GATE_AFFINITY_101: fake.affinity = 0x101; break;
    }
}

static void test_claim_and_entry_gates(void)
{
    new_boot();
    read_fails(allowed[0], DEADLINE);
    assert(fake.map_calls == 0 && fake.probes == 0 && fake.time_calls == 0);
    assert(!lithium_touch_mmio_claim(0) && !lithium_touch_mmio.claimed);
    claim();
    const uintptr_t owner = lithium_touch_mmio.guard.owner;
    fake.owner = &owner_tokens[1];
    assert(!lithium_touch_mmio_claim(DEADLINE + 1));
    assert(lithium_touch_mmio.guard.owner == owner);
    assert(lithium_touch_mmio.deadline_us == DEADLINE);
    for (enum gate g = GATE_NO_OWNER; g <= GATE_AFFINITY_101; ++g) {
        new_boot();
        set_gate(g);
        assert(!lithium_touch_mmio_claim(DEADLINE));
        assert(fake.map_calls == 0 && fake.probes == 0);
        read_fails(allowed[0], DEADLINE);
        assert(fake.map_calls == 0 && fake.probes == 0);
        assert(!lithium_touch_mmio.claimed);
        if (g == GATE_NO_OWNER) {
            assert(lithium_touch_mmio.guard.owner == 0);
            assert(lithium_touch_mmio.deadline_us == 0 && !lithium_touch_mmio.clock_seen);
            assert(!lithium_touch_mmio_claim(DEADLINE));
            assert(fake.time_calls == 0 && fake.map_calls == 0 && fake.probes == 0);
        }
    }
    for (enum gate g = GATE_OWNER; g <= GATE_AFFINITY_101; ++g) {
        for (unsigned cached = 0; cached < 2; ++cached) {
            new_boot();
            claim();
            if (cached) read_ok(allowed[2]);
            const unsigned maps = fake.map_calls, probes = fake.probes;
            set_gate(g);
            read_fails(allowed[2], DEADLINE);
            assert(fake.map_calls == maps && fake.probes == probes);
            assert(!lithium_touch_mmio.poisoned && !lithium_touch_mmio.guard.armed);
        }
    }
    new_boot();
    claim();
    assert(!lithium_touch_mmio_read_observation(NULL, allowed[0], DEADLINE, NULL));
    read_fails(allowed[0], DEADLINE - 1);
    read_fails(allowed[0], DEADLINE + 1);
    lithium_touch_mmio.guard.armed = true;
    read_fails(allowed[0], DEADLINE);
    lithium_touch_mmio.guard.armed = false;
    assert(fake.map_calls == 0 && fake.probes == 0);
    fake.reenter = true;
    read_ok(allowed[0]);
    new_boot();
    claim();
    /* Defensive gate must also reject a corrupted zero owner after a claim. */
    lithium_touch_mmio.guard.owner = 0;
    fake.owner = NULL;
    const unsigned clocks = fake.time_calls;
    read_fails(allowed[0], DEADLINE);
    assert(fake.time_calls == clocks && fake.map_calls == 0 && fake.probes == 0);
    puts("PASS claim/read owner, CPU, affinity, context, IRQ, deadline-token and reentry gates");
}

static void test_mapping_cache_and_read_budget(void)
{
    new_boot();
    claim();
    for (size_t i = 0; i < ARRAY_COUNT(allowed); ++i) read_ok(allowed[i]);
    assert(lithium_touch_mmio.reads == 13 && fake.probes == 13);
    assert(lithium_touch_mmio.mappings == 8 && fake.map_calls == 8);
    for (size_t i = 0; i < ARRAY_COUNT(pages); ++i) {
        assert(fake.maps[i].page == pages[i] && fake.maps[i].size == 0x1000);
        assert(lithium_touch_mmio.windows[i].physical == pages[i]);
        assert(lithium_touch_mmio.windows[i].address == (uintptr_t)fake.windows[i].words);
    }
    read_fails(allowed[0], DEADLINE);
    assert(fake.map_calls == 8 && fake.probes == 13);
    assert(!lithium_touch_mmio_claim(DEADLINE + 1));
    assert(fake.conversions == fake.time_calls);
    new_boot();
    claim();
    for (unsigned i = 0; i < 13; ++i) read_ok(allowed[11]);
    read_fails(allowed[0], DEADLINE); /* Budget closes even an uncached page. */
    assert(fake.map_calls == 1 && fake.probes == 13);
    for (unsigned i = 1; i < 13; ++i) assert(fake.operands[i] == fake.operands[0]);
    new_boot();
    claim();
    /* Artificially full cache covers the otherwise unreachable defensive cap. */
    lithium_touch_mmio.mappings = (uint32_t)ARRAY_COUNT(lithium_touch_mmio.windows);
    read_fails(allowed[0], DEADLINE);
    assert(fake.map_calls == 0 && fake.probes == 0);
    puts("PASS ml_io_map aligned page/4096 args, 8-window cache and 13-read cap");
}

static void test_allowlist(void)
{
    const uint32_t rejected[] = {
        0, UINT32_MAX, 0x00325000, 0x00330004, 0x01069000,
        0x075ba000, 0x075ba00c, 0x075ba110, 0x075bb004
    };
    new_boot();
    claim();
    for (size_t i = 0; i < ARRAY_COUNT(rejected); ++i) read_fails(rejected[i], DEADLINE);
    for (size_t i = 0; i < ARRAY_COUNT(allowed); ++i)
        for (uint32_t byte = 1; byte <= 3; ++byte) read_fails(allowed[i] + byte, DEADLINE);
    assert(fake.map_calls == 0 && fake.probes == 0 && lithium_touch_mmio.reads == 0);
    read_ok(allowed[10]);
    read_fails(0x075ba00c, DEADLINE); /* Cached page does not authorize its reset word. */
    assert(fake.map_calls == 1 && fake.probes == 1);
    puts("PASS allowlist/misalignment rejection before mapping or load, including cached page");
}

static void test_clock(void)
{
    const uint64_t bad_ns[] = {UINT64_C(99000), UINT64_C(1000000), UINT64_C(1001000)};
    for (unsigned at = 0; at < 4; ++at) {
        for (size_t b = 0; b < ARRAY_COUNT(bad_ns); ++b) {
            new_boot();
            claim();
            if (at == 3) read_ok(allowed[10]);
            const unsigned old_maps = fake.map_calls, old_reads = fake.probes;
            if (at == 0) fake.now_ns = bad_ns[b];
            if (at == 1) { fake.change_map_time = true; fake.map_time_ns = bad_ns[b]; }
            if (at >= 2) { fake.change_probe_time = true; fake.probe_time_ns = bad_ns[b]; }
            read_fails(allowed[10], DEADLINE);
            assert(fake.map_calls == old_maps + (at == 1 || at == 2));
            assert(fake.probes == old_reads + (at >= 2));
            assert(lithium_touch_mmio.mappings == (at >= 2 ? 1u : 0u));
            no_retry_after_poison();
        }
    }
    for (unsigned late = 0; late < 2; ++late) {
        new_boot();
        fake.now_ns = (DEADLINE + late) * 1000;
        assert(!lithium_touch_mmio_claim(DEADLINE));
        no_retry_after_poison();
    }
    new_boot();
    claim();
    fake.now_ns = DEADLINE * 1000 - 1;
    read_ok(allowed[0]); /* Nanoseconds round down; equal timestamps are legal. */
    assert(lithium_touch_mmio.last_us == DEADLINE - 1);
    read_ok(allowed[0]);
    fake.now_ns = DEADLINE * 1000;
    read_fails(allowed[0], DEADLINE);
    no_retry_after_poison();
    new_boot();
    claim();
    read_ok(allowed[0]);
    fake.now_ns = 99000;
    read_fails(allowed[0], DEADLINE);
    assert(fake.map_calls == 1 && fake.probes == 1);
    no_retry_after_poison();
    puts("PASS rollback/deadline before access, after map, after load and on cache; no late publication");
}

static void test_map_and_read_errors(void)
{
    for (unsigned after_success = 0; after_success < 2; ++after_success) {
        new_boot();
        claim();
        if (after_success) read_ok(allowed[0]);
        fake.fail_map_at = fake.map_calls + 1;
        read_fails(allowed[10], DEADLINE);
        assert(fake.map_calls == after_success + 1 && fake.probes == after_success);
        assert(lithium_touch_mmio.mappings == after_success);
        no_retry_after_poison();
    }
    for (unsigned cached = 0; cached < 2; ++cached) {
        new_boot();
        claim();
        if (cached) read_ok(allowed[10]);
        fake.fail_probe_at = fake.probes + 1;
        read_fails(allowed[11], DEADLINE);
        assert(fake.map_calls == 1 && fake.probes == cached + 1);
        assert(lithium_touch_mmio.fault_esr == 0 && lithium_touch_mmio.fault_physical == 0);
        no_retry_after_poison();
    }
    puts("PASS map/read errors preserve output and prohibit retry/reclaim after poison");
}

static void run_fault(uint32_t esr, enum fault_edit edit, bool accepted, bool lies)
{
    new_boot();
    claim();
    fake.inject_fault = true;
    fake.esr = esr;
    fake.edit = edit;
    fake.expect_recovery = accepted;
    fake.probe_lies = lies;
    read_fails(allowed[12], DEADLINE);
    assert(fake.map_calls == 1 && fake.probes == 1);
    assert(fake.recover_calls == (accepted ? 2u : 1u));
    assert(lithium_touch_mmio.fault_esr == (accepted ? esr : 0));
    assert(lithium_touch_mmio.fault_physical == (accepted ? allowed[12] : 0));
    no_retry_after_poison();
}

static void test_actual_recovery_hook(void)
{
    new_boot();
    assert(call_recovery((uintptr_t)lithium_touch_probe_load, PRECISE_READ_ABORT,
        0x1000, 0x1000, (uintptr_t)fake.owner, true) == 0);
    claim();
    read_ok(allowed[12]);
    assert(call_recovery(lithium_touch_mmio.guard.instruction, PRECISE_READ_ABORT,
        lithium_touch_mmio.guard.address, lithium_touch_mmio.guard.address,
        lithium_touch_mmio.guard.owner, true) == 0); /* Outside armed LDR. */
    assert(!lithium_touch_mmio.poisoned);
    run_fault(PRECISE_READ_ABORT, FAULT_EXACT, true, false);
    run_fault(PRECISE_READ_ABORT, FAULT_EXACT, true, true); /* Poison wins over bogus success. */
    for (unsigned ec = 0; ec < 64; ++ec)
        run_fault((ec << 26) | 0x02000010u, FAULT_EXACT, ec == 0x25, false);
    for (unsigned dfsc = 0; dfsc < 64; ++dfsc)
        run_fault(0x96000000u | dfsc, FAULT_EXACT, dfsc == 0x10, false);
    for (unsigned bit = 0; bit < 32; ++bit) {
        const bool constrained = (UINT32_C(0xfe0005ff) & (UINT32_C(1) << bit)) != 0;
        run_fault(PRECISE_READ_ABORT ^ (UINT32_C(1) << bit), FAULT_EXACT,
            !constrained, false);
    }
    for (enum fault_edit edit = FAULT_FAR; edit <= FAULT_SAME_PC; ++edit)
        run_fault(PRECISE_READ_ABORT, edit, false, false);
    puts("PASS actual recover_read: precise external read abort, recovery PC and sticky diagnostics");
    puts("PASS hook rejects EC/DFSC/IL/FnV/CM/S1PTW/WnR, FAR/operand/owner/load-PC and invalid guards");
}

int main(void)
{
    test_claim_and_entry_gates();
    test_mapping_cache_and_read_budget();
    test_allowlist();
    test_clock();
    test_map_and_read_errors();
    test_actual_recovery_hook();
    printf("PASS touch-mmio-access: %u isolated host boot scenarios, exact production .inc\n", scenarios);
    puts("HOST ONLY: ml_io_map selection/args checked; real Device attributes, ARM64 exception entry,");
    puts("hardware safety, secure-world ownership and non-completing bus accesses remain unverified.");
    return 0;
}
