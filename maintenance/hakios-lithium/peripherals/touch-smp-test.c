/* R36 focused host test. Actual production headers/include, existing Stage1
 * RAM fixture only; retained_stage1_main is deliberately never run.
 * Scheduler migration is a mock, not hardware/MMIO/ownership validation. */
#define main retained_stage1_main
#include "touch-stage1-test.c"
#undef main
#include "../../../ports/hakios-lithium/peripherals/boot-options.h"

typedef struct mock_processor {
    int logical;
    uint64_t affinity;
} *processor_t;
typedef int kern_return_t;
typedef int processor_flavor_t;
typedef void *host_t;
typedef int *processor_info_t;
typedef unsigned mach_msg_type_number_t;
typedef struct {
    int cpu_type, cpu_subtype, running, slot_num, is_master;
} processor_basic_info_data_t;
#define KERN_SUCCESS 0
#define PROCESSOR_BASIC_INFO 1
#define PROCESSOR_BASIC_INFO_COUNT \
    ((mach_msg_type_number_t)(sizeof(processor_basic_info_data_t) / sizeof(int)))
#define THREAD_CONTINUE_NULL ((void (*)(void))0)
static struct mock_processor target, prior;
static processor_t binding, binds[4];
static unsigned bind_count, block_count, lookup_count, info_count, cases;
static int logical, mismatch;
static bool missing;
static kern_return_t info_error;
static mach_msg_type_number_t returned_count;
static processor_basic_info_data_t returned_info;

static processor_t cpu_to_processor(int cpu)
{
    assert(cpu == 0);
    ++lookup_count;
    return missing ? NULL : &target;
}
static kern_return_t processor_info(processor_t processor,
    processor_flavor_t flavor, host_t *host, processor_info_t output,
    mach_msg_type_number_t *count)
{
    ++info_count;
    assert(processor == &target && flavor == PROCESSOR_BASIC_INFO);
    assert(host && *host == NULL);
    assert(count && *count == PROCESSOR_BASIC_INFO_COUNT);
    memcpy(output, &returned_info, sizeof(returned_info));
    *count = returned_count;
    return info_error;
}
static processor_t thread_bind(processor_t next)
{
    assert(bind_count < sizeof(binds) / sizeof(binds[0]));
    processor_t old = binding;
    binds[bind_count++] = next;
    binding = next;
    return old;
}
static void thread_block(void (*continuation)(void))
{
    assert(continuation == THREAD_CONTINUE_NULL);
    ++block_count;
    if (block_count == 1 && mismatch) {
        logical = mismatch == 1 ? 2 : 0;
        mock.affinity = mismatch == 1 ? 1 : 2;
    } else if (binding) {
        logical = binding->logical;
        mock.affinity = binding->affinity;
    }
}
static int cpu_number(void) { return logical; }
#define LITHIUM_TOUCH_CPU_SCOPE_HOST_TEST 1
#include "../../../ports/hakios-lithium/peripherals/touch-cpu-scope.h"

static void setup(void)
{
    reset();
    target = (struct mock_processor){0, 1};
    prior = (struct mock_processor){2, 3};
    binding = &prior;
    logical = 2;
    mock.affinity = 3;
    mock.max_cpu = 2;
    memset(binds, 0, sizeof(binds));
    bind_count = block_count = lookup_count = info_count = 0;
    mismatch = 0;
    missing = false;
    info_error = KERN_SUCCESS;
    returned_count = PROCESSOR_BASIC_INFO_COUNT;
    returned_info = (processor_basic_info_data_t){.running = 1, .slot_num = 0};
}
static void pass(const char *name)
{
    ++cases;
    printf("PASS %s\n", name);
}
static void no_io(void)
{
    assert(mock.maps == 0 && mock.reads == 0 && mock.writes == 0);
}
static void scope_tests(void)
{
    const int bad_max[] = {-2147483647 - 1, -1, 3, 2147483647};
    for (unsigned i = 0; i < sizeof(bad_max) / sizeof(bad_max[0]); ++i) {
        setup();
        mock.max_cpu = bad_max[i];
        struct lithium_touch_cpu_scope s = {&prior, true};
        assert(!lithium_touch_cpu_enter(&s));
        assert(!s.bound && binding == &prior);
        assert(bind_count == 0 && block_count == 0 && lookup_count == 0);
        assert(info_count == 0);
        lithium_touch_cpu_leave(&s);
        no_io();
        pass("scope invalid max declines before lookup/bind");
    }
    for (unsigned i = 0; i < 3; ++i) {
        setup();
        if (i == 0) mock.interrupt_context = 1;
        if (i == 1) mock.interrupts_enabled = 0;
        if (i == 2) missing = true;
        struct lithium_touch_cpu_scope s = {0};
        assert(!lithium_touch_cpu_enter(&s));
        assert(!s.bound && binding == &prior);
        assert(bind_count == 0 && block_count == 0);
        assert(lookup_count == (i < 2 ? 0u : 1u));
        assert(info_count == 0);
        lithium_touch_cpu_leave(&s);
        no_io();
        pass("scope IRQ/disabled/missing declines before bind");
    }
    static const char *const info_cases[] = {
        "scope processor_info API failure declines before bind",
        "scope processor_info zero count declines before bind",
        "scope processor_info short count declines before bind",
        "scope processor_info oversized count declines before bind",
        "scope processor_info not running declines before bind",
        "scope processor_info negative slot declines before bind",
        "scope processor_info nonzero slot declines before bind",
    };
    for (unsigned i = 0; i < sizeof(info_cases) / sizeof(info_cases[0]); ++i) {
        setup();
        if (i == 0) info_error = 5;
        if (i == 1) returned_count = 0;
        if (i == 2) returned_count = PROCESSOR_BASIC_INFO_COUNT - 1;
        if (i == 3) returned_count = PROCESSOR_BASIC_INFO_COUNT + 1;
        if (i == 4) returned_info.running = 0;
        if (i == 5) returned_info.slot_num = -1;
        if (i == 6) returned_info.slot_num = 1;
        struct lithium_touch_cpu_scope s = {0};
        assert(!lithium_touch_cpu_enter(&s));
        assert(!s.bound && binding == &prior);
        assert(info_count == 1 && lookup_count == 1);
        assert(bind_count == 0 && block_count == 0);
        lithium_touch_cpu_leave(&s);
        no_io();
        pass(info_cases[i]);
    }
    for (int max = 0; max <= 2; ++max)
            for (unsigned unbound = 0; unbound < 2; ++unbound) {
                setup();
                mock.max_cpu = max;
                processor_t saved = unbound ? NULL : &prior;
                binding = saved;
                struct lithium_touch_cpu_scope s = {0};
                assert(lithium_touch_cpu_enter(&s));
                assert(logical == 0 && mock.affinity == 1);
                assert(s.bound && s.previous == saved && binding == &target);
                assert(bind_count == 1 && block_count == 1);
                assert(info_count == 1);
                lithium_touch_cpu_leave(&s);
                assert(!s.bound && binding == saved && binds[1] == saved);
                assert(bind_count == 2 && block_count == 2);
                lithium_touch_cpu_leave(&s);
                assert(bind_count == 2 && block_count == 2);
                no_io();
                pass("scope migrates logical0/aff1 and restores prior/null binding");
            }
    for (int fail = 1; fail <= 2; ++fail)
        for (unsigned unbound = 0; unbound < 2; ++unbound) {
            setup();
            mismatch = fail;
            processor_t saved = unbound ? NULL : &prior;
            binding = saved;
            struct lithium_touch_cpu_scope s = {0};
            assert(!lithium_touch_cpu_enter(&s));
            assert(!s.bound && binding == saved && binds[1] == saved);
            assert(bind_count == 2 && block_count == 2);
            lithium_touch_cpu_leave(&s);
            assert(bind_count == 2);
            no_io();
            pass("scope logical/affinity mismatch restores prior/null binding");
        }
}
static void parser_tests(void)
{
    static const struct { const char *args; bool valid, touch, smp; } rows[] = {
        {"", true, false, false},
        {"touch=1", true, true, false},
        {"touch=0", true, false, false},
        {"touch=1 touch_smp=1", true, true, true},
        {"touch_smp=1 touch=1", true, true, true},
        {"touch=1 touch_smp=0", true, true, false},
        {"touch=1 touch_smp=1 touch_log=1 touch_poll=1", true, true, true},
        {"touch=1 touch_smp=1 gpu_probe=0 gpu_stage=0", true, true, true},
        {"touch_smp=1", false, false, false},
        {"touch_smp=0", false, false, false},
        {"touch=0 touch_smp=1", false, false, false},
        {"touch=0 touch_smp=0", false, false, false},
        {"touch=1 touch_smp=1 touch_smp=1", false, false, false},
        {"touch=1 touch_smp=0 touch_smp=1", false, false, false},
        {"touch=1 touch_smp=0 touch_smp=0", false, false, false},
        {"touch=1 touch_smp=2", false, false, false},
        {"touch=1 touch_smp=9", false, false, false},
        {"touch=1 touch_smp=-1", false, false, false},
        {"touch=1 touch_smp=01", false, false, false},
        {"touch=1 touch_smp=", false, false, false},
        {"touch=1 touch_smp", false, false, false},
        {"touch=1 touch_smp=x", false, false, false},
        {"touch=1 touch_smp=1 touch_stage=0", false, false, false},
        {"touch=1 touch_smp=1 touch_stage=1", false, false, false},
        {"touch=1 touch_smp=0 touch_stage=1", false, false, false},
        {"touch=1 touch_smp=1 gpu_probe=1", false, false, false},
        {"touch=1 touch_smp=0 gpu_probe=1", false, false, false},
        {"touch=1 touch_smp=1 gpu_stage=1", false, false, false},
        {"touch=1 touch_smp=1 gpu_stage=4", false, false, false},
        {"touch=1 touch_smp=0 gpu_stage=1", false, false, false},
    };
    for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        struct lithium_peripheral_options o;
        memset(&o, 0xa5, sizeof(o));
        bool valid = lithium_peripheral_options_parse(rows[i].args,
            strlen(rows[i].args), &o);
        assert(valid == rows[i].valid);
        assert(o.touch == rows[i].touch && o.touch_smp == rows[i].smp);
        if (!valid) {
            struct lithium_peripheral_options zero = {0};
            assert(memcmp(&o, &zero, sizeof(o)) == 0);
        }
        pass(rows[i].args[0] ? rows[i].args : "parser default off");
    }
}
static void stage_tests(void)
{
    for (int max = 0; max <= 2; ++max) {
        reset();
        mock.max_cpu = max;
        lithium_touch_stage1_probe();
        if (max == 0) complete();
        else { stopped("execution_context", true); no_io(); }
        pass("Stage1 legacy probe preserves single-core gate");
        reset();
        mock.max_cpu = max;
        lithium_touch_stage1_initialize();
        if (max == 0) complete();
        else { stopped("execution_context", true); no_io(); }
        pass("Stage1 legacy initialize preserves single-core gate");
    }
    for (int max = 1; max <= 2; ++max) {
        setup();
        mock.max_cpu = max;
        struct lithium_touch_cpu_scope s = {0};
        assert(lithium_touch_cpu_enter(&s));
        lithium_touch_stage1_run_context(true, true);
        complete();
        lithium_touch_cpu_leave(&s);
        assert(binding == &prior && !s.bound && bind_count == 2);
        pass("Stage1 opt-in SMP succeeds in bound mock context, binding restored");
    }
    for (unsigned i = 0; i < 6; ++i) {
        reset();
        mock.max_cpu = 2;
        if (i == 0) mock.max_cpu = -1;
        if (i == 1) mock.max_cpu = 3;
        if (i == 2) mock.interrupt_context = 1;
        if (i == 3) mock.interrupts_enabled = 0;
        if (i == 4) mock.affinity = 0;
        if (i == 5) mock.affinity = 2;
        lithium_touch_stage1_run_context(true, true);
        stopped("execution_context", true);
        no_io();
        pass("Stage1 opt-in retains max/IRQ/interrupt/affinity rejection");
    }
}
int main(void)
{
    scope_tests();
    unsigned scope_cases = cases;
    parser_tests();
    unsigned parser_cases = cases - scope_cases;
    stage_tests();
    printf("RESULT PASS cases=%u scope=%u parser=%u stage=%u hardware_mmio_verified=0\n",
        cases, scope_cases, parser_cases, cases - scope_cases - parser_cases);
    return 0;
}
