/* SPDX-License-Identifier: MIT */
#ifndef LITHIUM_CPU_RATE_SOAK_R39_H
#define LITHIUM_CPU_RATE_SOAK_R39_H

/*
 * Main-owned adapter: latest serialized software snapshot, only while running
 * with all four TSENS0 CPU clients valid/nonnegative. observed_abs is an XNU
 * absolute timestamp, not nanoseconds. The soak calls snapshot first and checks
 * software age <=10 seconds. Neither check attests hardware conversion freshness
 * or calibration. This module does not implement the adapter or touch MMIO.
 */
int lithium_cpu_thermal_peak(int32_t *mc, uint64_t *observed_abs);
void lithium_cpu_thermal_snapshot(const char *phase);

enum lithium_cpu_rate_soak_result {
    LITHIUM_RATE_SOAK_DISABLED = 0,
    LITHIUM_RATE_SOAK_COMPLETED = 1,
    LITHIUM_RATE_SOAK_DECLINED = 2,
    LITHIUM_RATE_SOAK_STOPPED = 3
};

/*
 * Include cpu-rate-soak-r39.inc once alongside the existing diagnostics.
 * Call once from the bsdinit_task diagnostic stage, after thermal_start, before
 * launchd and before any slowcores policy; ordinary thread context, IRQs and
 * preemption enabled. cpu_rate_soak=1 is independent of cpu_sanity.
 *
 * Mandatory RAM profile: ufs=0 ram_smp_return=1 loader_return_after=180.
 * Fixed 90-second budget, fixed bringup cutoff 75000 native mC (NOT a certified
 * safe junction temperature). No boot argument can increase either limit.
 *
 * Synchronous rotating 250 ms phases; NOT a simultaneous three-core load.
 * Work/checksum/copy counters persist per core across phases until return.
 * No helper workers survive return and no timer/PLL/voltage policy is changed.
 * Software deadline is cooperative: scheduler, logging, a thermal-reader lock,
 * an MMIO stall or a trapped register access cannot be given a hard wall bound.
 * The existing independent 180-second session timer remains main's owner.
 */
enum lithium_cpu_rate_soak_result lithium_cpu_rate_soak_r39(void);

#endif
