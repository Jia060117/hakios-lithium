/*
 * R37: call lithium_cpu_frequency_r37() once from bsdinit_task, before exec,
 * with interrupts/preemption enabled. Independent of cpu_sanity.
 *
 * Required BSD-visible interfaces are included here, including the generated
 * <mach/processor.h>. No struct processor/thread layout is required.
 *
 * cpu_freq_diag=1 explicitly opts into raw PMU/feature reads WITHOUT a fault
 * guard. ID_AA64DFR0_EL1 and PMU accesses can trap to EL2/EL3 even at EL1.
 * Neither CurrentEL nor PMUVer proves the absence of such traps. We cannot
 * inspect MDCR_EL2/EL3 from EL1. An unhandled trap can terminate this boot.
 * timer_migrate_diag=1 separately opts into raw CNTPCT reads with the same
 * absence of an EL2 trap guard. CNTVOFF_EL2 is NEVER accessed from EL1 here.
 *
 * cpu_freq_diag=1 also authorizes minimal setup ONLY with a main-agent audited
 * LITHIUM_R37_ARCH_PMU_UNOWNED=1. Set the latter only for the actual generic
 * inert osfmk/arm64/kpc.c backend with MONOTONIC disabled and exclusive
 * architected PMU ownership for this prelaunch interval. The default is 0.
 * Main enforces the inert KPC source hash and generated NMONOTONIC=0 headers.
 * This software audit does not prove Secure-world/firmware PMU ownership.
 * Optional cpu_freq_pmu_setup=0 disables setup; absent defaults to 1. An
 * explicit value of 1 still cannot bypass the audited/live ownership checks.
 * Regular setup requires empty enable/interrupt/export masks, not proof of
 * ownership. No Apple PMCR0/PMC register is accessed by this include.
 *
 * cpu_freq_pmu_borrow=1 separately opts into borrowing inherited nonzero
 * enable/interrupt masks; these masks are NOT proven stale. Borrowing is
 * allowed ONLY with PMCR.E=0, the same audited/live ownership checks, X=0 and
 * no pending overflow. It also requires cpu_freq_pmu_setup=1 (the default).
 * Inherited PMINTEN.C is refused: even far from wrap, IRQ/firmware stalls have no proven
 * cycle bound, and a new sticky overflow cannot be undone without a forbidden
 * overflow-clear write. A near-wrap guard is additional caution, not that
 * missing bound. Only event interrupt bits that were saved can be restored.
 *
 * In the regular path only PMCR.E, PMCNTENSET.C and PMCCFILTR.{P,NSK} change.
 * Borrowing additionally masks/restores inherited enable/interrupt controls,
 * verifies them disabled BEFORE setting E, and freezes E before restoration.
 * No counter/reset/overflow-clear writes, clock/voltage/MMIO/timer writes,
 * PMCR.{P,C,D,LC,DP} changes, or enabling new PMU interrupts. Configuration is
 * restored/read back on normal completion; counter values and sticky overflow
 * status naturally advance and are NOT restored. There is no trap recovery.
 *
 * ARM DDI0601 2024-12 register entries, archived in measure/:
 * PMCCNTR_EL0 is 64 bit in AArch64. LC selects overflow detection width, not
 * the MRS data width. D divides by 64 ONLY if effective LC == 0.
 * PMCCFILTR.P/NSK are XOR filters in NS EL1; requiring both clear also avoids
 * assuming this EL1's security state. Newer PMU extensions are rejected here.
 * CNTVCT is a reference calibrated by CNTFRQ, NOT an external clock reference.
 *
 * All software loops have fixed caps. A scheduler block, trapped register
 * access, interrupt or hardware stall has no hard wall-clock bound here.
 */
#ifndef LITHIUM_CPU_FREQUENCY_R37_INC
#define LITHIUM_CPU_FREQUENCY_R37_INC

#ifndef LITHIUM_R37_TEST
/* This include is compiled by osfmk/user_exception_trace.c. Its existing
 * kernel printf/string declarations avoid BSD sys/systm.h type conflicts. */
#include <stdint.h>
#include <kern/clock.h>
#include <kern/cpu_data.h>
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <mach/mach_time.h>
#include <mach/processor.h>
#include <mach/processor_info.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#if KPC
#include <kern/kpc.h>
#endif
#define R37_READ(reg) __builtin_arm_rsr64(#reg)
#define R37_WRITE(reg, value) __builtin_arm_wsr64(#reg, (uint64_t)(value))
#define R37_ISB() __asm__ volatile("isb" ::: "memory")
#endif

#ifndef LITHIUM_R37_ARCH_PMU_UNOWNED
#define LITHIUM_R37_ARCH_PMU_UNOWNED 0
#endif

#define R37_CPU_CAP 8u
#define R37_FREQ_SAMPLES 5u
#define R37_MIGRATE_SAMPLES 8u
#define R37_SPIN_CAP 262144u
#define R37_U64_MAX (~(uint64_t)0)
#define R37_C ((uint64_t)1 << 31)
#define R37_RESET_BITS ((uint64_t)6)
#define R37_FILTER_EL1 (((uint64_t)1 << 31) | ((uint64_t)1 << 29))

struct r37_stats {
	uint64_t count, sum, min, max;
	unsigned overflow;
};

static int
r37_add(struct r37_stats *s, uint64_t v)
{
	if (s->sum > R37_U64_MAX - v || s->count == R37_U64_MAX) {
		s->overflow = 1;
		return 0;
	}
	if (!s->count || v < s->min) {
		s->min = v;
	}
	if (!s->count || v > s->max) {
		s->max = v;
	}
	s->count++;
	s->sum += v;
	return 1;
}

static uint64_t
r37_avg(const struct r37_stats *s)
{
	return s->count && !s->overflow ? s->sum / s->count : 0;
}

/* Checked product/quotient: reject overflow rather than link __udivti3 into
 * the kernel, silently wrap, or invent a plausible frequency. */
static int
r37_muldiv(uint64_t a, uint64_t b, uint64_t d, uint64_t *out)
{
	if (!d || (b && a > R37_U64_MAX / b)) {
		return 0;
	}
	*out = a * b / d;
	return 1;
}

static uint64_t
r37_counter_delta(uint64_t before, uint64_t after, unsigned bits)
{
	return bits == 32 ? (uint64_t)(uint32_t)(after - before) : after - before;
}

static unsigned
r37_divisor(uint64_t pmcr)
{
	return (pmcr & (1u << 3)) && !(pmcr & (1u << 6)) ? 64u : 1u;
}

static int
r37_supported_pmu(unsigned v)
{
	/* v3 and v3p1 only: target-era semantics, no EBEP effective-LC override. */
	return v == 1 || v == 4;
}

static int
r37_el1_counted(uint64_t filter)
{
	/* Both Secure and Non-secure EL1 included; no inaccessible SCR_EL3 read. */
	return !(filter & R37_FILTER_EL1);
}

struct r37_pmu {
	uint64_t pmcr, enable, filter, interrupt, overflow;
};

static struct r37_pmu
r37_pmu_read(void)
{
	struct r37_pmu p;
	p.pmcr = R37_READ(PMCR_EL0);
	p.enable = R37_READ(PMCNTENSET_EL0);
	p.filter = R37_READ(PMCCFILTR_EL0);
	p.interrupt = R37_READ(PMINTENSET_EL1);
	p.overflow = R37_READ(PMOVSCLR_EL0);
	return p;
}

static int
r37_same_controls(const struct r37_pmu *a, const struct r37_pmu *b)
{
	return a->pmcr == b->pmcr && a->enable == b->enable &&
	       a->filter == b->filter && a->interrupt == b->interrupt;
}

static int
r37_pmu_enabled(const struct r37_pmu *p)
{
	return (p->pmcr & 1) && (p->enable & R37_C) && r37_el1_counted(p->filter);
}

static int
r37_setup_allowed(const struct r37_pmu *p, unsigned request, unsigned unowned)
{
	/* Do not take over a disabled-but-reserved cycle/event counter. */
	return request == 1 && unowned && !p->enable && !p->interrupt &&
	       !(p->pmcr & (1u << 4));
}

static unsigned
r37_backend_unowned(void)
{
#if !LITHIUM_R37_ARCH_PMU_UNOWNED || MONOTONIC || (defined(NMONOTONIC) && NMONOTONIC)
	return 0;
#else
#if KPC
	if (kpc_get_classes() != 0 || kpc_is_running_fixed() ||
	    kpc_is_running_configurable(R37_U64_MAX)) {
		return 0;
	}
#endif
	return 1;
#endif
}

struct r37_cycle_stamp {
	uint64_t v0, cycles, v1;
};

static struct r37_cycle_stamp
r37_cycle_stamp(void)
{
	struct r37_cycle_stamp s;
	R37_ISB();
	s.v0 = R37_READ(CNTVCT_EL0);
	R37_ISB();
	s.cycles = R37_READ(PMCCNTR_EL0);
	R37_ISB();
	s.v1 = R37_READ(CNTVCT_EL0);
	return s;
}

struct r37_frequency {
	struct r37_stats khz, cycles, ticks, bracket;
	uint64_t lower_khz, upper_khz;
	unsigned invalid, timeout, wrapped;
};

static int
r37_frequency_add(struct r37_frequency *f, struct r37_cycle_stamp a,
    struct r37_cycle_stamp b, uint64_t hz, unsigned divisor)
{
	uint64_t c, midpoint_a, midpoint_b, dt, lo_dt, hi_dt, rate, lo, hi;
	if (a.v1 < a.v0 || b.v1 < b.v0 || b.v0 <= a.v1 || !hz ||
	    a.v1 - a.v0 > R37_U64_MAX - (b.v1 - b.v0) ||
	    (divisor != 1 && divisor != 64)) {
		f->invalid++;
		return 0;
	}
	/* AArch64 PMCCNTR MRS width is 64 even when LC selects 32-bit overflow. */
	c = r37_counter_delta(a.cycles, b.cycles, 64);
	if (!c || c > R37_U64_MAX / divisor) {
		f->invalid++;
		return 0;
	}
	c *= divisor;
	midpoint_a = a.v0 + (a.v1 - a.v0) / 2;
	midpoint_b = b.v0 + (b.v1 - b.v0) / 2;
	dt = midpoint_b - midpoint_a;
	lo_dt = b.v0 - a.v1;
	hi_dt = b.v1 - a.v0;
	if (!r37_muldiv(c, hz, dt, &rate) ||
	    !r37_muldiv(c, hz, hi_dt, &lo) ||
	    !r37_muldiv(c, hz, lo_dt, &hi)) {
		f->invalid++;
		return 0;
	}
	/* Transactional statistics: no half-added accepted sample on overflow. */
	struct r37_frequency next = *f;
	if (!r37_add(&next.khz, rate / 1000) ||
	    !r37_add(&next.cycles, c) || !r37_add(&next.ticks, dt) ||
	    !r37_add(&next.bracket, (a.v1 - a.v0) + (b.v1 - b.v0))) {
		f->invalid++;
		return 0;
	}
	if (next.khz.count == 1 || lo / 1000 < next.lower_khz) {
		next.lower_khz = lo / 1000;
	}
	if (hi / 1000 > next.upper_khz) {
		next.upper_khz = hi / 1000;
	}
	next.wrapped += b.cycles < a.cycles;
	*f = next;
	return 1;
}

/* Headroom in counter increments (also when D=1), not a wall-time guarantee.
 * LC=0 detects overflow at bit31 even though the counter read is 64 bits. */
#define R37_BORROW_HEADROOM ((uint64_t)1 << 28)

static int
r37_borrow_room(uint64_t cycles, uint64_t pmcr)
{
	uint64_t limit = (pmcr & (1u << 6)) ? R37_U64_MAX : 0xffffffffULL;
	return limit - (cycles & limit) >= R37_BORROW_HEADROOM;
}

static const char *
r37_borrow_refusal(const struct r37_pmu *p, unsigned setup, unsigned unowned)
{
	if (setup != 1) return "setup_disabled";
	if (!unowned) return "pmu_owned_or_unaudited";
	if (p->pmcr & 1) return "pmcr_e_active";
	if (p->pmcr & (1u << 4)) return "export_active";
	if (p->overflow) return "pending_overflow";
	/* PMCR.E=0 does not suppress an interrupt from sticky PMOVS.C.
	 * No finite polling cap proves that re-enabling this bit will be safe. */
	if (p->interrupt & R37_C) return "cycle_interrupt_unbounded";
	if ((p->enable | p->interrupt) >> 32) return "unsupported_control_bits";
	return NULL;
}

static void
r37_frequency_borrow(unsigned logical, unsigned setup, unsigned unowned,
    uint64_t el, unsigned pmuver, uint64_t hz, const struct r37_pmu *saved)
{
	struct r37_frequency f = {0};
	struct r37_pmu expected = *saved, actual = *saved, restored = *saved;
	unsigned changed = 0, readback_ok = 1, controls_ok = 1, restored_ok;
	const char *reason = r37_borrow_refusal(saved, setup, unowned);
	if (reason) goto report;

	/* Keep the thread on this CPU throughout the transaction. IRQs remain
	 * enabled. This is not arbitration with EL2/EL3 or firmware owners. */
	disable_preemption();
	actual = r37_pmu_read();
	if (!r37_same_controls(saved, &actual) || actual.overflow ||
	    cpu_number() != (int)logical || !r37_backend_unowned()) {
		reason = "live_state_or_owner_changed";
		goto unlock;
	}
	if (!r37_borrow_room(R37_READ(PMCCNTR_EL0), saved->pmcr)) {
		reason = "near_cycle_overflow";
		goto unlock;
	}
	expected.enable = 0;
	expected.interrupt = 0;
	expected.filter &= ~R37_FILTER_EL1;
	changed = 1;
	R37_WRITE(PMINTENCLR_EL1, saved->interrupt);
	R37_WRITE(PMCNTENCLR_EL0, saved->enable);
	R37_WRITE(PMCCFILTR_EL0, expected.filter);
	R37_ISB();
	actual = r37_pmu_read();
	if (!r37_same_controls(&expected, &actual) || actual.overflow) {
		reason = "mask_readback_failed";
		readback_ok = controls_ok = 0;
		goto restore;
	}
	expected.enable = R37_C;
	R37_WRITE(PMCNTENSET_EL0, R37_C);
	R37_ISB();
	actual = r37_pmu_read();
	if (!r37_same_controls(&expected, &actual) || actual.overflow) {
		reason = "cycle_enable_readback_failed";
		readback_ok = controls_ok = 0;
		goto restore;
	}
	expected.pmcr = (saved->pmcr | 1) & ~R37_RESET_BITS;
	R37_WRITE(PMCR_EL0, expected.pmcr);
	R37_ISB();
	actual = r37_pmu_read();
	if (!r37_same_controls(&expected, &actual) || actual.overflow) {
		reason = "pmcr_readback_failed";
		readback_ok = controls_ok = 0;
		goto restore;
	}
	for (unsigned s = 0; s < R37_FREQ_SAMPLES; s++) {
		struct r37_pmu check = r37_pmu_read();
		struct r37_cycle_stamp a = r37_cycle_stamp();
		uint64_t now = a.v1;
		unsigned spin;
		if (!r37_same_controls(&expected, &check) || check.overflow) {
			reason = "controls_or_overflow_changed";
			controls_ok = 0;
			f.invalid++;
			break;
		}
		if (!r37_borrow_room(a.cycles, saved->pmcr)) {
			reason = "near_cycle_overflow";
			break;
		}
		for (spin = 0; spin < R37_SPIN_CAP; spin++) {
			R37_ISB();
			now = R37_READ(CNTVCT_EL0);
			if (!r37_borrow_room(R37_READ(PMCCNTR_EL0), saved->pmcr)) {
				reason = "near_cycle_overflow";
				break;
			}
			if (now < a.v1 || now - a.v1 >= hz / 1000) break;
		}
		struct r37_cycle_stamp b = r37_cycle_stamp();
		struct r37_pmu end = r37_pmu_read();
		if (!r37_same_controls(&expected, &end) || end.overflow) {
			reason = "controls_or_overflow_changed";
			controls_ok = 0;
			f.invalid++;
			break;
		}
		if (reason) break;
		if (spin == R37_SPIN_CAP) {
			f.timeout++;
			continue;
		}
		if (cpu_number() != (int)logical || now < a.v1) {
			f.invalid++;
			continue;
		}
		r37_frequency_add(&f, a, b, hz, r37_divisor(expected.pmcr));
	}
restore:
	/* Freeze first, then remove our C enable before restoring any inherited
	 * event controls. Never write a counter, PMOVSCLR or PMOVSSET. */
	R37_WRITE(PMCR_EL0, saved->pmcr & ~R37_RESET_BITS);
	R37_ISB();
	R37_WRITE(PMCNTENCLR_EL0, saved->enable | R37_C);
	R37_WRITE(PMINTENCLR_EL1, saved->interrupt);
	R37_WRITE(PMCCFILTR_EL0, saved->filter);
	R37_ISB();
	actual = r37_pmu_read();
	/* Failed writes or an unexpected event overflow must not turn a saved
	 * interrupt into a new pending interrupt. Leave masks off on failure. */
	if (actual.pmcr == saved->pmcr && !actual.enable && !actual.interrupt &&
	    actual.filter == saved->filter && !(actual.overflow & saved->interrupt)) {
		R37_WRITE(PMCNTENSET_EL0, saved->enable);
		R37_WRITE(PMINTENSET_EL1, saved->interrupt);
		R37_ISB();
	} else {
		reason = "restore_unsafe";
		controls_ok = 0;
	}
unlock:
	restored = r37_pmu_read();
	enable_preemption();
report:
	restored_ok = r37_same_controls(saved, &restored);
	uint64_t calibrated = 0;
	unsigned valid = !reason && controls_ok && restored_ok && f.khz.count &&
	    r37_muldiv(f.cycles.sum, hz, f.ticks.sum, &calibrated);
	printf("LITHIUM CPU FREQ logical=%u actual=%d mpidr=0x%llx currentel=0x%llx pmuver=%u cntfrq=%llu pmcr=0x%llx enable=0x%llx filter=0x%llx interrupt=0x%llx\n",
	    logical, cpu_number(), R37_READ(MPIDR_EL1), el, pmuver, hz,
	    saved->pmcr, saved->enable, saved->filter, saved->interrupt);
	printf("LITHIUM CPU FREQ logical=%u borrow_requested=1 borrow_changed=%u borrow_reason=%s setup_requested=%u unowned_audited=%u setup_readback=%u controls_stable=%u restored=%u used_pmcr=0x%llx used_filter=0x%llx counter_width=64 overflow_width=%u divisor=%u overflow_before=0x%llx overflow_after=0x%llx guard_counts=%llu cycle_irq_borrow_supported=0\n",
	    logical, changed, reason ? reason : "none", setup, unowned, readback_ok,
	    controls_ok, restored_ok, expected.pmcr, expected.filter,
	    (expected.pmcr & (1u << 6)) ? 64 : 32, r37_divisor(expected.pmcr),
	    saved->overflow, restored.overflow, R37_BORROW_HEADROOM);
	printf("LITHIUM CPU FREQ logical=%u valid=%u samples=%llu invalid=%u timeout=%u wraps64=%u effective_mhz_milli_min=%llu effective_mhz_milli_avg=%llu effective_mhz_milli_max=%llu calibrated_mhz_milli=%llu bound_min=%llu bound_max=%llu cycles_min=%llu cycles_avg=%llu cycles_max=%llu ticks_min=%llu ticks_avg=%llu ticks_max=%llu bracket_max_ticks=%llu\n",
	    logical, valid, f.khz.count, f.invalid, f.timeout, f.wrapped,
	    f.khz.min, r37_avg(&f.khz), f.khz.max, valid ? calibrated / 1000 : 0,
	    f.lower_khz, f.upper_khz, f.cycles.min, r37_avg(&f.cycles), f.cycles.max,
	    f.ticks.min, r37_avg(&f.ticks), f.ticks.max, f.bracket.max);
}

static void
r37_frequency_cpu(unsigned logical, unsigned setup, unsigned unowned, unsigned borrow)
{
	uint64_t el = R37_READ(CurrentEL);
	if (el != 4) {
		printf("LITHIUM CPU FREQ logical=%u valid=0 reason=requires_el1 currentel=0x%llx\n",
		    logical, el);
		return;
	}
	uint64_t dfr = R37_READ(ID_AA64DFR0_EL1);
	unsigned pmuver = (unsigned)((dfr >> 8) & 15);
	if (!r37_supported_pmu(pmuver)) {
		printf("LITHIUM CPU FREQ logical=%u valid=0 reason=unsupported_pmu pmuver=%u dfr=0x%llx\n",
		    logical, pmuver, dfr);
		return;
	}
	uint64_t hz = R37_READ(CNTFRQ_EL0);
	if (hz < 1000 || hz > 1000000000ULL) {
		printf("LITHIUM CPU FREQ logical=%u valid=0 reason=cntfrq_out_of_range cntfrq=%llu\n",
		    logical, hz);
		return;
	}
	struct r37_pmu saved = r37_pmu_read(), expected = saved;
	if (borrow == 1 && !r37_pmu_enabled(&saved)) {
		r37_frequency_borrow(logical, setup, unowned, el, pmuver, hz, &saved);
		return;
	}
	unsigned changed = 0, readback_ok = 1;
	if (!r37_pmu_enabled(&saved) && r37_setup_allowed(&saved, setup, unowned)) {
		/* No P/C reset strobes can enter either PMCR write. */
		disable_preemption();
		expected.filter &= ~R37_FILTER_EL1;
		expected.pmcr = (expected.pmcr | 1) & ~R37_RESET_BITS;
		expected.enable = R37_C;
		R37_WRITE(PMCCFILTR_EL0, expected.filter);
		R37_WRITE(PMCR_EL0, expected.pmcr);
		R37_WRITE(PMCNTENSET_EL0, R37_C);
		R37_ISB();
		struct r37_pmu actual = r37_pmu_read();
		readback_ok = r37_same_controls(&expected, &actual);
		enable_preemption();
		changed = 1;
	}
	struct r37_frequency f = {0};
	unsigned controls_ok = readback_ok;
	if (controls_ok && r37_pmu_enabled(&expected)) {
		for (unsigned s = 0; s < R37_FREQ_SAMPLES; s++) {
			disable_preemption(); /* IRQs stay enabled; no printing here. */
			struct r37_pmu check = r37_pmu_read();
			struct r37_cycle_stamp a = r37_cycle_stamp();
			uint64_t now = a.v1;
			unsigned spin;
			for (spin = 0; spin < R37_SPIN_CAP; spin++) {
				R37_ISB();
				now = R37_READ(CNTVCT_EL0);
				if (now < a.v1 || now - a.v1 >= hz / 1000) {
					break; /* nominal 1 ms, or broken reference */
				}
			}
			struct r37_cycle_stamp b = r37_cycle_stamp();
			struct r37_pmu end = r37_pmu_read();
			unsigned cpu_match = cpu_number() == (int)logical;
			enable_preemption();
			if (!r37_same_controls(&expected, &check) ||
			    !r37_same_controls(&expected, &end)) {
				controls_ok = 0;
				f.invalid++;
				break;
			}
			if (spin == R37_SPIN_CAP) {
				f.timeout++;
				continue;
			}
			if (!cpu_match || now < a.v1) {
				f.invalid++;
				continue;
			}
			r37_frequency_add(&f, a, b, hz, r37_divisor(expected.pmcr));
		}
	}
	/* Restoration occurs before any log, even after setup readback failure.
	 * Exclusive ownership is required: restoration is not a concurrent PMU
	 * arbitration protocol. Overflow status/cycle contents are not written. */
	struct r37_pmu used = r37_pmu_read();
	if (!r37_same_controls(&expected, &used)) {
		controls_ok = 0;
	}
	if (changed) {
		disable_preemption();
		R37_WRITE(PMCNTENCLR_EL0, R37_C);
		R37_WRITE(PMCCFILTR_EL0, saved.filter);
		R37_WRITE(PMCR_EL0, saved.pmcr & ~R37_RESET_BITS);
		R37_ISB();
		enable_preemption();
	}
	struct r37_pmu restored = r37_pmu_read();
	unsigned restored_ok = r37_same_controls(&saved, &restored);
	uint64_t calibrated = 0;
	unsigned valid = controls_ok && restored_ok && f.khz.count &&
	    r37_muldiv(f.cycles.sum, hz, f.ticks.sum, &calibrated);
	printf("LITHIUM CPU FREQ logical=%u actual=%d mpidr=0x%llx currentel=0x%llx pmuver=%u cntfrq=%llu pmcr=0x%llx enable=0x%llx filter=0x%llx interrupt=0x%llx\n",
	    logical, cpu_number(), R37_READ(MPIDR_EL1), el, pmuver, hz,
	    saved.pmcr, saved.enable, saved.filter, saved.interrupt);
	printf("LITHIUM CPU FREQ logical=%u setup_requested=%u unowned_audited=%u setup_changed=%u setup_readback=%u controls_stable=%u restored=%u used_pmcr=0x%llx used_filter=0x%llx counter_width=64 overflow_width=%u divisor=%u overflow_before=0x%llx overflow_after=0x%llx\n",
	    logical, setup, unowned, changed, readback_ok, controls_ok, restored_ok,
	    expected.pmcr, expected.filter, (expected.pmcr & (1u << 6)) ? 64 : 32,
	    r37_divisor(expected.pmcr), saved.overflow, restored.overflow);
	printf("LITHIUM CPU FREQ logical=%u valid=%u samples=%llu invalid=%u timeout=%u wraps64=%u effective_mhz_milli_min=%llu effective_mhz_milli_avg=%llu effective_mhz_milli_max=%llu calibrated_mhz_milli=%llu bound_min=%llu bound_max=%llu cycles_min=%llu cycles_avg=%llu cycles_max=%llu ticks_min=%llu ticks_avg=%llu ticks_max=%llu bracket_max_ticks=%llu\n",
	    logical, valid, f.khz.count, f.invalid, f.timeout, f.wrapped,
	    f.khz.min, r37_avg(&f.khz), f.khz.max, valid ? calibrated / 1000 : 0,
	    f.lower_khz, f.upper_khz, f.cycles.min, r37_avg(&f.cycles), f.cycles.max,
	    f.ticks.min, r37_avg(&f.ticks), f.ticks.max, f.bracket.max);
}

struct r37_timer_stamp {
	uint64_t abs0, v0, physical, v1, abs1;
	int cpu;
};

struct r37_migration {
	struct r37_timer_stamp before, after;
};

static struct r37_timer_stamp
r37_timer_stamp(void)
{
	struct r37_timer_stamp s;
	s.cpu = cpu_number();
	s.abs0 = mach_absolute_time();
	R37_ISB();
	s.v0 = R37_READ(CNTVCT_EL0);
	R37_ISB();
	s.physical = R37_READ(CNTPCT_EL0);
	R37_ISB();
	s.v1 = R37_READ(CNTVCT_EL0);
	s.abs1 = mach_absolute_time();
	return s;
}

struct r37_migration_stats {
	struct r37_stats abs_ticks, virtual_ticks, physical_ticks, latency_ns;
	uint64_t histogram[5], abs_back_max, virtual_back_max, physical_back_max;
	unsigned abs_back, virtual_back, physical_back, internal_back, bad_cpu;
};

static void
r37_migration_add(struct r37_migration_stats *s, const struct r37_migration *m,
    unsigned from, unsigned to, mach_timebase_info_data_t tb)
{
	const struct r37_timer_stamp *a = &m->before, *b = &m->after;
	if (a->cpu != (int)from || b->cpu != (int)to) {
		s->bad_cpu++;
		return;
	}
	if (a->abs1 < a->abs0 || b->abs1 < b->abs0 ||
	    a->v1 < a->v0 || b->v1 < b->v0) {
		s->internal_back++;
	}
	/* Compare ordered reads BEFORE subtraction: no unsigned underflow and
	 * no zero-clamping of a backward observation into the latency histogram. */
	if (b->abs0 < a->abs1) {
		uint64_t back = a->abs1 - b->abs0;
		s->abs_back++;
		if (back > s->abs_back_max) {
			s->abs_back_max = back;
		}
	} else {
		uint64_t dt = b->abs0 - a->abs1, ns;
		r37_add(&s->abs_ticks, dt);
		if (r37_muldiv(dt, tb.numer, tb.denom, &ns) &&
		    r37_add(&s->latency_ns, ns)) {
			unsigned bin = ns < 10000 ? 0 : ns < 100000 ? 1 :
			    ns < 1000000 ? 2 : ns < 10000000 ? 3 : 4;
			s->histogram[bin]++;
		}
	}
	if (b->v0 < a->v1) {
		uint64_t back = a->v1 - b->v0;
		s->virtual_back++;
		if (back > s->virtual_back_max) {
			s->virtual_back_max = back;
		}
	} else {
		r37_add(&s->virtual_ticks, b->v0 - a->v1);
	}
	if (b->physical < a->physical) {
		uint64_t back = a->physical - b->physical;
		s->physical_back++;
		if (back > s->physical_back_max) {
			s->physical_back_max = back;
		}
	} else {
		r37_add(&s->physical_ticks, b->physical - a->physical);
	}
}

static void
r37_print_migration(unsigned from, unsigned to, unsigned sample,
    const struct r37_migration *m, const char *kind)
{
	printf("LITHIUM TIMER snapshot from=%u to=%u sample=%u kind=%s before_cpu=%d abs0=%llu v0=%llu p=%llu v1=%llu abs1=%llu after_cpu=%d abs0=%llu v0=%llu p=%llu v1=%llu abs1=%llu\n",
	    from, to, sample, kind, m->before.cpu, m->before.abs0, m->before.v0,
	    m->before.physical, m->before.v1, m->before.abs1, m->after.cpu,
	    m->after.abs0, m->after.v0, m->after.physical, m->after.v1, m->after.abs1);
}

static void
r37_timer_pair(processor_t from_p, unsigned from, processor_t to_p, unsigned to,
    mach_timebase_info_data_t tb)
{
	struct r37_migration_stats stats = {0};
	struct r37_migration first = {0}, worst = {0}, backward = {0};
	unsigned worst_sample = 0, backward_sample = 0, have_back = 0;
	uint64_t worst_dt = 0;
	for (unsigned sample = 0; sample < R37_MIGRATE_SAMPLES; sample++) {
		thread_bind(from_p);
		thread_block(THREAD_CONTINUE_NULL);
		struct r37_migration m;
		disable_preemption();
		m.before = r37_timer_stamp();
		enable_preemption();
		thread_bind(to_p);
		thread_block(THREAD_CONTINUE_NULL);
		disable_preemption();
		m.after = r37_timer_stamp();
		enable_preemption();
		r37_migration_add(&stats, &m, from, to, tb);
		if (!sample) {
			first = worst = m;
		}
		if (m.after.abs0 >= m.before.abs1) {
			uint64_t dt = m.after.abs0 - m.before.abs1;
			if (dt > worst_dt) {
				worst_dt = dt;
				worst = m;
				worst_sample = sample;
			}
		}
		if (!have_back && (m.after.abs0 < m.before.abs1 ||
		    m.after.v0 < m.before.v1 || m.after.physical < m.before.physical ||
		    m.before.abs1 < m.before.abs0 || m.after.abs1 < m.after.abs0 ||
		    m.before.v1 < m.before.v0 || m.after.v1 < m.after.v0)) {
			backward = m;
			backward_sample = sample;
			have_back = 1;
		}
	}
	printf("LITHIUM TIMER migration from=%u to=%u samples=%u bad_cpu=%u internal_back=%u abs_back=%u abs_back_max_ticks=%llu virtual_back=%u virtual_back_max_ticks=%llu physical_back=%u physical_back_max_ticks=%llu\n",
	    from, to, R37_MIGRATE_SAMPLES, stats.bad_cpu, stats.internal_back,
	    stats.abs_back, stats.abs_back_max, stats.virtual_back,
	    stats.virtual_back_max, stats.physical_back, stats.physical_back_max);
	printf("LITHIUM TIMER migration from=%u to=%u forward=%llu abs_ticks_min=%llu abs_ticks_avg=%llu abs_ticks_max=%llu v_ticks_min=%llu v_ticks_avg=%llu v_ticks_max=%llu p_ticks_min=%llu p_ticks_avg=%llu p_ticks_max=%llu elapsed_ns_min=%llu elapsed_ns_avg=%llu elapsed_ns_max=%llu hist_lt_10us=%llu lt_100us=%llu lt_1ms=%llu lt_10ms=%llu ge_10ms=%llu stats_overflow=%u\n",
	    from, to, stats.abs_ticks.count, stats.abs_ticks.min,
	    r37_avg(&stats.abs_ticks), stats.abs_ticks.max, stats.virtual_ticks.min,
	    r37_avg(&stats.virtual_ticks), stats.virtual_ticks.max, stats.physical_ticks.min,
	    r37_avg(&stats.physical_ticks), stats.physical_ticks.max, stats.latency_ns.min,
	    r37_avg(&stats.latency_ns), stats.latency_ns.max, stats.histogram[0],
	    stats.histogram[1], stats.histogram[2], stats.histogram[3], stats.histogram[4],
	    stats.abs_ticks.overflow | stats.virtual_ticks.overflow |
	    stats.physical_ticks.overflow | stats.latency_ns.overflow);
	r37_print_migration(from, to, 0, &first, "first");
	if (worst_sample) {
		r37_print_migration(from, to, worst_sample, &worst, "max_forward");
	}
	if (have_back) {
		r37_print_migration(from, to, backward_sample, &backward, "first_backward");
	}
}

void
lithium_cpu_frequency_r37(void)
{
	unsigned freq = 0, timer = 0, setup = 1, borrow = 0;
	PE_parse_boot_argn("cpu_freq_diag", &freq, sizeof(freq));
	PE_parse_boot_argn("timer_migrate_diag", &timer, sizeof(timer));
	if (freq != 1 && timer != 1) {
		return; /* no register accesses, binding or output by default */
	}
	PE_parse_boot_argn("cpu_freq_pmu_setup", &setup, sizeof(setup));
	PE_parse_boot_argn("cpu_freq_pmu_borrow", &borrow, sizeof(borrow));
	if (!ml_get_interrupts_enabled() || get_preemption_level() != 0) {
		printf("LITHIUM CPU FREQ skipped=1 reason=requires_irq_and_preemption_enabled\n");
		return;
	}
	processor_t cpus[R37_CPU_CAP];
	unsigned ids[R37_CPU_CAP], count = 0;
	int max_cpu = ml_get_max_cpu_number();
	unsigned unowned = freq == 1 ? r37_backend_unowned() : 0;
	for (unsigned i = 0; max_cpu >= 0 && i <= (unsigned)max_cpu && i < R37_CPU_CAP; i++) {
		processor_t p = cpu_to_processor((int)i);
		processor_basic_info_data_t info;
		mach_msg_type_number_t n = PROCESSOR_BASIC_INFO_COUNT;
		host_t host;
		if (p && processor_info(p, PROCESSOR_BASIC_INFO, &host,
		    (processor_info_t)&info, &n) == KERN_SUCCESS && info.running) {
			cpus[count] = p;
			ids[count++] = i;
		}
	}
	if (freq == 1) {
		printf("LITHIUM CPU FREQ begin revision=37 prelaunch=1 raw_pmu_optin=1 fault_guard=0 el2_el3_trap_risk=1 samples=%u nominal_us=1000 spin_cap=%u online=%u max_cpu=%d truncated=%u cntfrq_assumed_correct=1 external_reference=0 active_clock_or_pll_proof=0\n",
		    R37_FREQ_SAMPLES, R37_SPIN_CAP, count, max_cpu,
		    max_cpu >= (int)R37_CPU_CAP);
	}
	if (timer == 1) {
		printf("LITHIUM TIMER begin revision=37 raw_physical_read_optin=1 fault_guard=0 el2_trap_risk=1 samples_per_directed_pair=%u online=%u max_cpu=%d truncated=%u cntvoff_el2=unavailable_at_el1_not_read independent_walltime=0 elapsed_is_offset_skew=0\n",
		    R37_MIGRATE_SAMPLES, count, max_cpu, max_cpu >= (int)R37_CPU_CAP);
	}
	if (!count) {
		printf("LITHIUM CPU FREQ skipped=1 reason=no_online_cpu\n");
		return;
	}
	/* Snapshot the original binding once; intermediate thread_bind return
	 * values must not overwrite it while visiting different processors. */
	processor_t original = thread_bind(cpus[0]);
	thread_block(THREAD_CONTINUE_NULL);
	for (unsigned c = 0; c < count; c++) {
		thread_bind(cpus[c]);
		thread_block(THREAD_CONTINUE_NULL);
		int actual = cpu_number();
		if (actual != (int)ids[c]) {
			printf("LITHIUM CPU FREQ logical=%u actual=%d valid=0 reason=binding_mismatch\n",
			    ids[c], actual);
			continue;
		}
		if (freq == 1) {
			r37_frequency_cpu(ids[c], setup, unowned, borrow);
		}
		if (timer == 1) {
			printf("LITHIUM TIMER cpu logical=%u actual=%d mpidr=0x%llx currentel=0x%llx cntfrq=%llu\n",
			    ids[c], actual, R37_READ(MPIDR_EL1), R37_READ(CurrentEL),
			    R37_READ(CNTFRQ_EL0));
		}
	}
	if (timer == 1) {
		mach_timebase_info_data_t tb;
		clock_timebase_info(&tb);
		printf("LITHIUM TIMER units timebase_numer=%u timebase_denom=%u raw_ticks=1 snapshot_order=abs_v_p_v_abs snapshot_atomic=0 raw_backwards_compared_before_subtraction=1 positive_delta_is_scheduling_plus_timer=1\n",
		    tb.numer, tb.denom);
		for (unsigned from = 0; from < count; from++) {
			for (unsigned to = 0; to < count; to++) {
				if (from != to) {
					r37_timer_pair(cpus[from], ids[from], cpus[to], ids[to], tb);
				}
			}
		}
		if (count < 2) {
			printf("LITHIUM TIMER migration skipped=1 reason=less_than_two_online_cpus\n");
		}
	}
	processor_t previous = thread_bind(original);
	/* Check the restored binding without exposing struct thread to BSD.
	 * Rebinding to the same value is idempotent and returns that binding. */
	processor_t readback = thread_bind(original);
	thread_block(THREAD_CONTINUE_NULL);
	printf("LITHIUM CPU FREQ end revision=37 original_binding_restored=%u original_unbound=%u previous_was_null=%u clock_voltage_writes=0 counter_resets=0 irq_included=1 preemption_disabled_per_freq_sample=1 hypervisor_or_secure_exclusion_unknown=1\n",
	    readback == original, original == PROCESSOR_NULL, previous == PROCESSOR_NULL);
}
#endif /* LITHIUM_CPU_FREQUENCY_R37_INC */
