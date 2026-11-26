/* Focused host test of the actual R37 include, with deterministic register and
 * scheduler mocks. Run from the repository root; output goes under measure/.
 * clang -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-format -fsanitize=address,undefined \
 *   maintenance/hakios-lithium/cpu-frequency-r37-test.c -o evidence/.../measure/r37-test
 *
 * Linux uint64_t uses unsigned long; XNU ARM64 uses unsigned long long.
 * The kernel printf format is independently checked by main's real BSD build.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LITHIUM_R37_TEST 1
#ifndef LITHIUM_R37_ARCH_PMU_UNOWNED
#define LITHIUM_R37_ARCH_PMU_UNOWNED 1
#endif
#ifndef MONOTONIC
#define MONOTONIC 0
#endif
#ifndef NMONOTONIC
#define NMONOTONIC 0
#endif
#define TEST_UNOWNED (LITHIUM_R37_ARCH_PMU_UNOWNED && !MONOTONIC && !NMONOTONIC)
#define KPC 1
#define THREAD_CONTINUE_NULL NULL
#define PROCESSOR_NULL NULL
#define PROCESSOR_BASIC_INFO 1
#define PROCESSOR_BASIC_INFO_COUNT 5
#define KERN_SUCCESS 0
typedef unsigned mach_msg_type_number_t;
typedef void *host_t;
typedef int *processor_info_t;
typedef struct { int running; } processor_basic_info_data_t;
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
typedef struct mock_cpu {
	int id, online;
	uint64_t pmcr, enable, filter, interrupt, overflow, pmuver, cycles;
} *processor_t;
static struct mock_cpu mock_cpus[8];
static processor_t binding, original_binding;
static int actual_cpu, max_cpu, irq_enabled, preemption_level;
static unsigned arg_freq, arg_timer, raw_reads, pmu_reads, writes;
static int arg_setup, arg_borrow;
static unsigned blocks, logs, pmcr_reset_writes, counter_writes, bad_bind;
static unsigned owner_classes, force_bad_readback, freeze_timer;
static unsigned owner_fixed, owner_configurable, owner_calls, owner_after;
static unsigned cycle_reads, inject_cycle_at, event_advances, irq_exposures;
static uint64_t inject_cycles;
static const char *drop_write_reg;
static unsigned drop_write_nth, drop_write_seen, inject_restore_event_overflow;
struct mock_write_record { const char *reg; uint64_t value; };
static struct mock_write_record write_trace[128];
static uint64_t tick;
static char log_buffer[128000];
static size_t log_used;

static int
mock_printf(const char *fmt, ...)
{
	assert(!preemption_level); /* Never print while borrowing the PMU. */
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(log_buffer + log_used, sizeof(log_buffer) - log_used, fmt, ap);
	va_end(ap);
	assert(n >= 0 && (size_t)n < sizeof(log_buffer) - log_used);
	log_used += (size_t)n;
	logs++;
	return n;
}
#define printf mock_printf

static void
PE_parse_boot_argn(const char *name, void *v, unsigned n)
{
	assert(n == sizeof(unsigned));
	if (!strcmp(name, "cpu_freq_diag")) *(unsigned *)v = arg_freq;
	if (!strcmp(name, "timer_migrate_diag")) *(unsigned *)v = arg_timer;
	if (!strcmp(name, "cpu_freq_pmu_setup") && arg_setup >= 0) *(unsigned *)v = (unsigned)arg_setup;
	if (!strcmp(name, "cpu_freq_pmu_borrow") && arg_borrow >= 0) *(unsigned *)v = (unsigned)arg_borrow;
}
static int ml_get_interrupts_enabled(void) { return irq_enabled; }
static int get_preemption_level(void) { return preemption_level; }
static int ml_get_max_cpu_number(void) { return max_cpu; }
static int cpu_number(void) { return actual_cpu; }
static void disable_preemption(void) { preemption_level++; }
static void enable_preemption(void) { assert(preemption_level > 0); preemption_level--; }
static __attribute__((unused)) unsigned kpc_get_classes(void)
{
	owner_calls++;
	return owner_classes || (owner_after && owner_calls >= owner_after);
}
static __attribute__((unused)) int kpc_is_running_fixed(void) { return owner_fixed; }
static __attribute__((unused)) int kpc_is_running_configurable(uint64_t m)
{
	assert(m == UINT64_MAX);
	return owner_configurable;
}
static processor_t cpu_to_processor(int i)
{
	assert(i >= 0 && i <= max_cpu && i < 8);
	return &mock_cpus[i];
}
static int
processor_info(processor_t p, int flavor, host_t *host, processor_info_t info,
    mach_msg_type_number_t *count)
{
	assert(flavor == PROCESSOR_BASIC_INFO && *count == PROCESSOR_BASIC_INFO_COUNT);
	((processor_basic_info_data_t *)info)->running = p->online;
	*host = NULL;
	return 0;
}
static processor_t thread_bind(processor_t p)
{
	assert(!preemption_level);
	processor_t old = binding;
	binding = p;
	return old;
}
static void thread_block(void *continuation)
{
	assert(!continuation && !preemption_level);
	blocks++;
	if (binding && !bad_bind) actual_cpu = binding->id;
	tick += 500;
}
static uint64_t mach_absolute_time(void) { return ++tick; }
static void clock_timebase_info(mach_timebase_info_data_t *tb)
{
	tb->numer = 125;
	tb->denom = 3;
}

static void
mock_advance(struct mock_cpu *p, uint64_t increment)
{
	if (!(p->pmcr & 1)) return;
	if (p->enable & ~(1ULL << 31)) event_advances++;
	if ((p->enable & (1ULL << 31)) &&
	    !(p->filter & ((1ULL << 31) | (1ULL << 29)))) {
		uint64_t limit = (p->pmcr & 64) ? UINT64_MAX : UINT32_MAX;
		if (increment > limit || (p->cycles & limit) > limit - increment) {
			p->overflow |= 1ULL << 31;
		}
		p->cycles += increment;
	}
}

static uint64_t
mock_read(const char *reg)
{
	struct mock_cpu *p = &mock_cpus[actual_cpu];
	raw_reads++;
	if (!freeze_timer) {
		tick += 24;
		mock_advance(p, 2400); /* 2400 MHz / 24 MHz */
	}
	if (!strcmp(reg, "PMCCNTR_EL0") && ++cycle_reads == inject_cycle_at) {
		mock_advance(p, inject_cycles); /* Model an unbounded IRQ/firmware stall. */
	}
	irq_exposures += !!(p->overflow & p->interrupt);
	if (!strcmp(reg, "CurrentEL")) return 4;
	if (!strcmp(reg, "ID_AA64DFR0_EL1")) return p->pmuver << 8;
	if (!strcmp(reg, "CNTFRQ_EL0")) return 24000000;
	if (!strcmp(reg, "CNTVCT_EL0")) return tick;
	if (!strcmp(reg, "CNTPCT_EL0")) return tick + 999;
	if (!strcmp(reg, "MPIDR_EL1")) return (uint64_t)actual_cpu;
	pmu_reads++;
	if (!strcmp(reg, "PMCR_EL0")) return p->pmcr;
	if (!strcmp(reg, "PMCNTENSET_EL0")) return p->enable;
	if (!strcmp(reg, "PMCCFILTR_EL0")) return p->filter;
	if (!strcmp(reg, "PMINTENSET_EL1")) return p->interrupt;
	if (!strcmp(reg, "PMOVSCLR_EL0")) return p->overflow;
	if (!strcmp(reg, "PMCCNTR_EL0")) return p->cycles;
	assert(!"unexpected system register");
	return 0;
}
static void
mock_write(const char *reg, uint64_t value)
{
	struct mock_cpu *p = &mock_cpus[actual_cpu];
	assert(writes < sizeof(write_trace) / sizeof(write_trace[0]));
	write_trace[writes] = (struct mock_write_record){reg, value};
	writes++;
	assert(preemption_level);
	if (drop_write_reg && !strcmp(reg, drop_write_reg) &&
	    ++drop_write_seen == drop_write_nth) return;
	if (!strcmp(reg, "PMCR_EL0")) {
		if (arg_borrow == 1 && (value & 1)) {
			/* A failed mask/enable write must be detected before E=1. */
			assert(p->enable == (1ULL << 31) && !p->interrupt);
		}
		pmcr_reset_writes += !!(value & 6);
		p->pmcr = value;
		if (inject_restore_event_overflow && !(value & 1)) p->overflow |= 1;
	} else if (!strcmp(reg, "PMCCFILTR_EL0")) {
		p->filter = force_bad_readback && writes == 1 ? value | (1ULL << 31) : value;
	} else if (!strcmp(reg, "PMCNTENSET_EL0")) {
		p->enable |= value;
	} else if (!strcmp(reg, "PMCNTENCLR_EL0")) {
		p->enable &= ~value;
	} else if (!strcmp(reg, "PMINTENSET_EL1")) {
		p->interrupt |= value;
	} else if (!strcmp(reg, "PMINTENCLR_EL1")) {
		p->interrupt &= ~value;
	} else {
		counter_writes++;
		assert(!"unexpected register write");
	}
	irq_exposures += !!(p->overflow & p->interrupt);
}
#define R37_READ(reg) mock_read(#reg)
#define R37_WRITE(reg, value) mock_write(#reg, (uint64_t)(value))
#define R37_ISB() ((void)0)
#include "../../ports/hakios-lithium/diagnostics/cpu-frequency-r37.inc"
#undef printf

static unsigned checks;
#define CHECK(e) do { checks++; assert(e); } while (0)

static void
reset_mock(void)
{
	memset(mock_cpus, 0, sizeof(mock_cpus));
	for (int i = 0; i < 8; i++) {
		mock_cpus[i].id = i;
		mock_cpus[i].online = i < 3;
		mock_cpus[i].pmuver = 1;
	}
	binding = original_binding = NULL;
	actual_cpu = 0; max_cpu = 2; irq_enabled = 1; preemption_level = 0;
	arg_freq = arg_timer = raw_reads = pmu_reads = writes = 0;
	arg_setup = arg_borrow = -1;
	blocks = logs = pmcr_reset_writes = counter_writes = bad_bind = 0;
	owner_classes = force_bad_readback = freeze_timer = 0;
	owner_fixed = owner_configurable = owner_calls = owner_after = 0;
	cycle_reads = inject_cycle_at = event_advances = irq_exposures = 0;
	inject_cycles = 0;
	drop_write_reg = NULL; drop_write_nth = drop_write_seen = 0;
	inject_restore_event_overflow = 0;
	tick = 1000; log_used = 0; log_buffer[0] = 0;
}

static void
test_math(void)
{
	uint64_t out = 123;
	CHECK(r37_divisor(0) == 1);
	CHECK(r37_divisor(8) == 64);
	CHECK(r37_divisor(64) == 1);
	CHECK(r37_divisor(72) == 1);
	CHECK(r37_supported_pmu(1) && r37_supported_pmu(4));
	CHECK(!r37_supported_pmu(0) && !r37_supported_pmu(15) && !r37_supported_pmu(9));
	CHECK(r37_el1_counted(0));
	CHECK(!r37_el1_counted(1ULL << 31));
	CHECK(!r37_el1_counted(1ULL << 29));
	CHECK(!r37_el1_counted((1ULL << 31) | (1ULL << 29)));
	CHECK(r37_muldiv(5000000, 24000000, 24000, &out) && out == 5000000000ULL);
	CHECK(!r37_muldiv(R37_U64_MAX, 2, 1, &out));
	CHECK(!r37_muldiv(1, 2, 0, &out));
	CHECK(r37_muldiv(0, R37_U64_MAX, 1, &out) && !out);
	CHECK(r37_counter_delta(0xfffffff0, 0x20, 32) == 48);
	CHECK(r37_counter_delta(R37_U64_MAX - 15, 32, 64) == 48);
	CHECK(r37_counter_delta(0xfffffff0, 0x100000020ULL, 64) == 48);
	for (uint64_t i = 0; i < 1024; i++) {
		CHECK(r37_counter_delta(R37_U64_MAX - i, i, 64) == 2 * i + 1);
		CHECK(r37_counter_delta(0xffffffffULL - i, i, 32) == 2 * i + 1);
	}
	struct r37_stats s = {0};
	CHECK(r37_avg(&s) == 0);
	CHECK(r37_add(&s, 0) && r37_add(&s, 10) && r37_add(&s, 5));
	CHECK(s.count == 3 && s.min == 0 && s.max == 10 && r37_avg(&s) == 5);
	s.sum = R37_U64_MAX;
	CHECK(!r37_add(&s, 1) && s.overflow && !r37_avg(&s));
}

static void
test_frequency(void)
{
	struct r37_cycle_stamp a = {100, 1000, 102};
	struct r37_cycle_stamp b = {24100, 2401000, 24102};
	struct r37_frequency f = {0};
	CHECK(r37_frequency_add(&f, a, b, 24000000, 1));
	CHECK(f.khz.min == 2400000 && f.khz.max == 2400000 && f.khz.count == 1);
	CHECK(f.lower_khz < 2400000 && f.upper_khz > 2400000 && f.bracket.max == 4);
	f = (struct r37_frequency){0};
	b.cycles = 38500;
	CHECK(r37_frequency_add(&f, a, b, 24000000, 64) && f.khz.sum == 2400000);
	f = (struct r37_frequency){0};
	a.cycles = R37_U64_MAX - 999;
	b.cycles = 2399000;
	CHECK(r37_frequency_add(&f, a, b, 24000000, 1));
	CHECK(f.wrapped == 1 && f.khz.sum == 2400000);
	a.cycles = b.cycles;
	CHECK(!r37_frequency_add(&f, a, b, 24000000, 1)); /* stopped PMU */
	b.v0 = 90;
	CHECK(!r37_frequency_add(&f, a, b, 24000000, 1)); /* backwards reference */
	b = (struct r37_cycle_stamp){24100, R37_U64_MAX / 2, 24102};
	a = (struct r37_cycle_stamp){100, 0, 102};
	CHECK(!r37_frequency_add(&f, a, b, 24000000, 64)); /* scaled overflow */
	CHECK(!r37_frequency_add(&f, a, b, 24000000, 1)); /* product overflow */
	b.cycles = 2400000;
	CHECK(!r37_frequency_add(&f, a, b, 0, 1));
}

static struct r37_migration
forward_migration(uint64_t ns)
{
	struct r37_migration m = {{100, 100, 100, 101, 101, 0},
	    {101 + ns, 102 + ns, 102 + ns, 103 + ns, 104 + ns, 1}};
	return m;
}

static void
test_migration(void)
{
	struct r37_migration_stats s = {0};
	mach_timebase_info_data_t tb = {1, 1};
	uint64_t values[] = {0, 9999, 10000, 99999, 100000, 999999,
	    1000000, 9999999, 10000000};
	for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		struct r37_migration m = forward_migration(values[i]);
		r37_migration_add(&s, &m, 0, 1, tb);
	}
	CHECK(s.abs_ticks.count == 9 && s.latency_ns.min == 0 && s.latency_ns.max == 10000000);
	CHECK(s.histogram[0] == 2 && s.histogram[1] == 2 && s.histogram[2] == 2 &&
	    s.histogram[3] == 2 && s.histogram[4] == 1);
	struct r37_migration b = forward_migration(1);
	b.after.abs0 = 90; b.after.v0 = 90; b.after.physical = 90;
	r37_migration_add(&s, &b, 0, 1, tb);
	CHECK(s.abs_back == 1 && s.abs_back_max == 11 && s.virtual_back == 1 &&
	    s.physical_back == 1 && s.abs_ticks.count == 9);
	b.after.cpu = 7;
	r37_migration_add(&s, &b, 0, 1, tb);
	CHECK(s.bad_cpu == 1 && s.abs_back == 1);
	b = forward_migration(1);
	b.before.abs1 = 99; b.before.v1 = 99;
	r37_migration_add(&s, &b, 0, 1, tb);
	CHECK(s.internal_back == 1);
}

static void
test_mock_execution(void)
{
	reset_mock();
	lithium_cpu_frequency_r37();
	CHECK(!raw_reads && !writes && !blocks && !logs);
	arg_freq = 2; arg_timer = 99;
	lithium_cpu_frequency_r37();
	CHECK(!raw_reads && !writes && !blocks);
	reset_mock(); arg_timer = 1; binding = original_binding = &mock_cpus[2];
	lithium_cpu_frequency_r37();
	CHECK(!pmu_reads && !writes && blocks > 90 && binding == original_binding);
	CHECK(strstr(log_buffer, "abs_back=0") && !strstr(log_buffer, "abs_back=1"));
	CHECK(strstr(log_buffer, "original_binding_restored=1") && logs < 40);
	reset_mock(); arg_freq = 1;
	lithium_cpu_frequency_r37();
#if TEST_UNOWNED
	CHECK(writes == 18 && !counter_writes && !pmcr_reset_writes);
	CHECK(strstr(log_buffer, "valid=1 samples=5") && strstr(log_buffer, "restored=1"));
	for (unsigned i = 0; i < 3; i++) {
		CHECK(mock_cpus[i].pmcr == 0 && mock_cpus[i].enable == 0 &&
		    mock_cpus[i].filter == 0 && mock_cpus[i].cycles > 0);
	}
#else
	CHECK(!writes && strstr(log_buffer, "unowned_audited=0"));
#endif
	CHECK(!preemption_level && binding == NULL);
	reset_mock(); arg_freq = 1; arg_setup = 0;
	lithium_cpu_frequency_r37();
	CHECK(!writes && strstr(log_buffer, "setup_requested=0"));
	reset_mock(); arg_freq = 1; owner_classes = 1;
	lithium_cpu_frequency_r37();
	CHECK(!writes);
	reset_mock(); arg_freq = 1;
	for (unsigned i = 0; i < 3; i++) mock_cpus[i].interrupt = 1;
	lithium_cpu_frequency_r37();
	CHECK(!writes);
	reset_mock(); arg_freq = 1;
	for (unsigned i = 0; i < 3; i++) mock_cpus[i].enable = 1; /* reserved event */
	lithium_cpu_frequency_r37();
	CHECK(!writes);
	reset_mock(); arg_freq = 1;
	for (unsigned i = 0; i < 3; i++) {
		mock_cpus[i].pmcr = 1 | 64 | 8; /* D ignored with LC */
		mock_cpus[i].enable = R37_C;
	}
	lithium_cpu_frequency_r37();
	CHECK(!writes && strstr(log_buffer, "divisor=1") && strstr(log_buffer, "valid=1 samples=5"));
	reset_mock(); arg_freq = 1;
	for (unsigned i = 0; i < 3; i++) mock_cpus[i].pmuver = 0;
	lithium_cpu_frequency_r37();
	CHECK(!pmu_reads && !writes);
	reset_mock(); arg_freq = 1; irq_enabled = 0;
	lithium_cpu_frequency_r37();
	CHECK(!raw_reads && !blocks);
	reset_mock(); arg_freq = 1; bad_bind = 1;
	lithium_cpu_frequency_r37();
	CHECK(strstr(log_buffer, "reason=binding_mismatch") && binding == NULL);
	reset_mock(); arg_freq = 1; freeze_timer = 1; max_cpu = 0;
	lithium_cpu_frequency_r37();
#if TEST_UNOWNED
	CHECK(strstr(log_buffer, "timeout=5") && writes == 6);
#else
	CHECK(!writes);
#endif
	reset_mock(); arg_freq = 1; force_bad_readback = 1; max_cpu = 0;
	lithium_cpu_frequency_r37();
#if TEST_UNOWNED
	CHECK(writes == 6 && !mock_cpus[0].pmcr && !mock_cpus[0].filter &&
	    !mock_cpus[0].enable && strstr(log_buffer, "valid=0"));
#else
	CHECK(!writes);
#endif
	reset_mock(); arg_timer = 1; max_cpu = 0;
	lithium_cpu_frequency_r37();
	CHECK(strstr(log_buffer, "less_than_two_online_cpus") && binding == NULL);
}

static void
borrow_mock(void)
{
	reset_mock();
	arg_freq = arg_borrow = 1;
	max_cpu = 0;
	mock_cpus[0].pmcr = 0x51004020;
	mock_cpus[0].enable = 0x2a;
	mock_cpus[0].interrupt = 0x31;
	mock_cpus[0].filter = R37_FILTER_EL1 | 0x4000000;
	mock_cpus[0].cycles = 4096;
}

static void
check_borrow_restored(void)
{
	CHECK(mock_cpus[0].pmcr == 0x51004020);
	CHECK(mock_cpus[0].enable == 0x2a && mock_cpus[0].interrupt == 0x31);
	CHECK(mock_cpus[0].filter == (R37_FILTER_EL1 | 0x4000000));
	CHECK(!preemption_level && !binding && !event_advances && !irq_exposures);
	CHECK(!pmcr_reset_writes && !counter_writes);
	CHECK(strstr(log_buffer, "restored=1"));
}

static void
test_borrow(void)
{
	struct r37_pmu p = {0};
	CHECK(!r37_borrow_refusal(&p, 1, 1));
	CHECK(r37_borrow_refusal(&p, 0, 1) && r37_borrow_refusal(&p, 2, 1));
	CHECK(r37_borrow_refusal(&p, 1, 0));
	p.pmcr = 1; CHECK(r37_borrow_refusal(&p, 1, 1));
	p.pmcr = 16; CHECK(r37_borrow_refusal(&p, 1, 1));
	p.pmcr = 0;
	for (unsigned bit = 0; bit < 32; bit++) {
		p.overflow = 1ULL << bit;
		CHECK(r37_borrow_refusal(&p, 1, 1));
	}
	p.overflow = 0; p.interrupt = R37_C;
	CHECK(r37_borrow_refusal(&p, 1, 1)); /* Even far from wrap / LC=1. */
	p.pmcr = 64; CHECK(r37_borrow_refusal(&p, 1, 1));
	p.interrupt = 0; p.enable = 1ULL << 32;
	CHECK(r37_borrow_refusal(&p, 1, 1));
	p.enable = 0; p.interrupt = 1ULL << 32;
	CHECK(r37_borrow_refusal(&p, 1, 1));
	for (unsigned lc = 0; lc < 2; lc++) {
		uint64_t limit = lc ? UINT64_MAX : UINT32_MAX;
		CHECK(r37_borrow_room(limit - R37_BORROW_HEADROOM, lc * 64));
		CHECK(!r37_borrow_room(limit - R37_BORROW_HEADROOM + 1, lc * 64));
		CHECK(!r37_borrow_room(limit, lc * 64));
	}
	CHECK(!r37_borrow_room(0x12345678ffffffffULL, 0));
	reset_mock(); arg_borrow = 1;
	lithium_cpu_frequency_r37();
	CHECK(!raw_reads && !writes && !blocks && !logs);
	/* Absent, zero and non-1 opt-ins preserve the inherited-mask refusal. */
	for (int opt = -1; opt <= 2; opt++) {
		if (opt == 1) continue;
		borrow_mock(); arg_borrow = opt;
		lithium_cpu_frequency_r37();
		CHECK(!writes && !strstr(log_buffer, "borrow_requested=1"));
	}
	borrow_mock();
	lithium_cpu_frequency_r37();
#if TEST_UNOWNED
	CHECK(writes == 11 && strstr(log_buffer, "valid=1 samples=5"));
	CHECK(strstr(log_buffer, "borrow_changed=1 borrow_reason=none"));
	CHECK(!strcmp(write_trace[0].reg, "PMINTENCLR_EL1") && write_trace[0].value == 0x31);
	CHECK(!strcmp(write_trace[1].reg, "PMCNTENCLR_EL0") && write_trace[1].value == 0x2a);
	CHECK(!strcmp(write_trace[4].reg, "PMCR_EL0") && (write_trace[4].value & 1));
	CHECK(!strcmp(write_trace[5].reg, "PMCR_EL0") && !(write_trace[5].value & 1));
	CHECK(!strcmp(write_trace[10].reg, "PMINTENSET_EL1") && write_trace[10].value == 0x31);
	check_borrow_restored();
#else
	CHECK(!writes && strstr(log_buffer, "pmu_owned_or_unaudited"));
#endif
	/* Every admission failure is write-free, including the observed phys100 mask. */
	for (unsigned scenario = 0; scenario < 10; scenario++) {
		borrow_mock();
		switch (scenario) {
		case 0: arg_setup = 0; break;
		case 1: owner_classes = 1; break;
		case 2: owner_fixed = 1; break;
		case 3: owner_configurable = 1; break;
		case 4: mock_cpus[0].pmcr |= 1; break;
		case 5: mock_cpus[0].pmcr |= 16; break;
		case 6: mock_cpus[0].overflow = 1; break;
		case 7: mock_cpus[0].enable = 4; mock_cpus[0].interrupt = 0x8000007f; break;
		case 8: mock_cpus[0].overflow = R37_C; break;
		case 9: mock_cpus[0].cycles = UINT32_MAX - R37_BORROW_HEADROOM + 1; break;
		}
		lithium_cpu_frequency_r37();
		CHECK(!writes && strstr(log_buffer, "valid=0 samples=0"));
#if TEST_UNOWNED
		if (scenario == 7) CHECK(strstr(log_buffer, "cycle_interrupt_unbounded"));
#endif
		CHECK(!preemption_level && !binding);
	}
#if TEST_UNOWNED
	borrow_mock(); owner_after = 2;
	lithium_cpu_frequency_r37();
	CHECK(!writes && strstr(log_buffer, "live_state_or_owner_changed"));
	borrow_mock(); freeze_timer = 1;
	lithium_cpu_frequency_r37();
	CHECK(strstr(log_buffer, "valid=0 samples=0") && strstr(log_buffer, "timeout=5"));
	check_borrow_restored();
	/* Fail each preparation write: inherited events never start counting. */
	const char *fail_regs[] = {"PMINTENCLR_EL1", "PMCNTENCLR_EL0",
	    "PMCCFILTR_EL0", "PMCNTENSET_EL0", "PMCR_EL0"};
	for (unsigned i = 0; i < sizeof(fail_regs) / sizeof(fail_regs[0]); i++) {
		borrow_mock(); drop_write_reg = fail_regs[i]; drop_write_nth = 1;
		lithium_cpu_frequency_r37();
		CHECK(strstr(log_buffer, "readback_failed") && strstr(log_buffer, "valid=0 samples=0"));
		check_borrow_restored();
	}
	for (unsigned lc = 0; lc < 2; lc++) {
		borrow_mock();
		uint64_t limit = lc ? UINT64_MAX : UINT32_MAX;
		mock_cpus[0].pmcr |= lc * 64;
		mock_cpus[0].cycles = limit - R37_BORROW_HEADROOM - 200000;
		lithium_cpu_frequency_r37();
		CHECK(strstr(log_buffer, "near_cycle_overflow") && writes == 11);
		CHECK(!mock_cpus[0].overflow && !irq_exposures && !event_advances);
		CHECK(mock_cpus[0].pmcr == (0x51004020 | lc * 64));
		CHECK(mock_cpus[0].enable == 0x2a && mock_cpus[0].interrupt == 0x31);
	}
	/* A stall can overrun any headroom: C IRQ must stay off even after restore. */
	borrow_mock(); inject_cycle_at = 4; inject_cycles = 1ULL << 32;
	lithium_cpu_frequency_r37();
	CHECK(strstr(log_buffer, "controls_or_overflow_changed") &&
	    strstr(log_buffer, "valid=0 samples=0") && mock_cpus[0].overflow == R37_C);
	check_borrow_restored();
	/* If freezing fails, do not restart events or restore interrupt masks. */
	borrow_mock(); drop_write_reg = "PMCR_EL0"; drop_write_nth = 2;
	lithium_cpu_frequency_r37();
	CHECK(strstr(log_buffer, "restore_unsafe") && strstr(log_buffer, "restored=0"));
	CHECK(!mock_cpus[0].enable && !mock_cpus[0].interrupt && !event_advances && !irq_exposures);
	borrow_mock(); inject_restore_event_overflow = 1;
	lithium_cpu_frequency_r37();
	CHECK(strstr(log_buffer, "restore_unsafe") && strstr(log_buffer, "restored=0"));
	CHECK(!mock_cpus[0].enable && !mock_cpus[0].interrupt && !irq_exposures);
	CHECK(mock_cpus[0].overflow == 1 && !counter_writes && !pmcr_reset_writes);
#endif
}

int
main(void)
{
	test_math();
	test_frequency();
	test_migration();
	test_mock_execution();
	test_borrow();
	printf("R37 PASS checks=%u audited_setup=%u math rollover statistics frequency migration mock_default_off restore bounded_timeout pmu_borrow ownership masks readback_faults overflow_guard cycle_irq_refusal\n",
	    checks, (unsigned)LITHIUM_R37_ARCH_PMU_UNOWNED);
	return 0;
}
