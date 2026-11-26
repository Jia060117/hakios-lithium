/* SPDX-License-Identifier: MIT -- inherited register-only G1 profile. */
#ifndef LITHIUM_G1_COMBINED_CORE_H
#define LITHIUM_G1_COMBINED_CORE_H
#include "../g1-clock-core.h"
#include "../rpm/mx-plan-core.h"
#include "../rpm/inherited-read.h"
#define LGCB_TOTAL_US UINT64_C(200000)
#define LGCB_CLOCK_READ_CAP 8192u
#define LGCB_CLOCK_STORE_CAP 128u
#define LGCB_VBIF_PA UINT64_C(0x00b0c000)
enum lgcb_error { LGCB_OK=0,LGCB_S1_UNBOUND=1,LGCB_MX=2,LGCB_BUS=3,
    LGCB_RAIL=4,LGCB_CX=5,LGCB_CLOCK=6,LGCB_CLEANUP=7,LGCB_TIME=8,
    LGCB_MAP=9,LGCB_CONTEXT=10,LGCB_BOUNDS=11,LGCB_BUDGET=12 };
/* branches_on is diagnostic only at cold capture. ahb_valid/hz describe the
 * inherited configuration, not a new frequency measurement or running proof.
 * Real parent/after-domain CBCR checks are performed by combined-mmio.inc. */
struct lgcb_bus_facts { unsigned stable,branches_on,ahb_valid; uint32_t ahb_hz,ahb_parent; };
int lgcb_mx_plan_eligible(const struct lgmp_result *);
int lgcb_bus_facts(const struct inherited_samples *,struct lgcb_bus_facts *);
/* Concrete missing source/observation: MMPLL2 vdd_dig -> PM8994 S1 corner.
 * This is not an AP vote or TZ-never-writes requirement. No default proof is
 * synthesized from S2/MX850 or from a cold PLL register snapshot. */
int lgcb_platform_source_blocker(void);
#endif
