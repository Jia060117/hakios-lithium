/* SPDX-License-Identifier: GPL-2.0-only
 * MSM8996 SMMUv2 live discovery and disabled-by-default static S1 draft.
 * Numeric definitions follow the pinned Linux v6.12 arm-smmu.h.
 */
#ifndef LITHIUM_G3_LIVE_H
#define LITHIUM_G3_LIVE_H
#include <stdint.h>
#include <stddef.h>
#include "../smmu/smmu.h"
#define LG3_BASE UINT64_C(0x00b40000)
#define LG3_BYTES 0x10000u /* Linux resource; do not use stock's extra 64KiB. */
#define LG3_MAX_SMR 128u
#define LG3_MAX_CB 8u
#define LG3_MAX_SAMPLES 448u
#define LG3_READ_CAP 1024u
#define LG3_BUDGET_US UINT64_C(20000)
#ifndef LG3_ENABLE_STAGE1_WRITES
#define LG3_ENABLE_STAGE1_WRITES 0
#endif
enum lg3_status { LG3_OK,LG3_DISABLED,LG3_ARGUMENT,LG3_USED,LG3_CONTEXT,
    LG3_TIME,LG3_IO,LG3_LIMIT,LG3_UNSTABLE,LG3_GEOMETRY,LG3_CAPABILITY,
    LG3_ROUTING,LG3_OWNERSHIP,LG3_MEMORY,LG3_BUSY,LG3_DIRTY };
struct lg3_sample { uint32_t offset,value[2]; unsigned valid; };
struct lg3_snapshot {
    unsigned used,complete,count,reads;
    enum lg3_status status;
    uint64_t start_us,last_us,elapsed_us;
    unsigned register_shift,num_smr,num_cb,num_s2cb;
    unsigned ias_bits,oas_bits,va_bits,cpu_pa_bits,effective_pa_bits;
    uint32_t cb_read_mask;
    struct lg3_sample sample[LG3_MAX_SAMPLES];
};
struct lg3_io {
    void *ctx;
    int (*guard)(void *); /* real same-boot powered G1 window, not a fake receipt */
    uint64_t (*now_us)(void *);
    int (*read32)(void *,uint32_t offset,uint32_t *);
};
/* cb_read_mask is explicit: recommend bit0 for the source-described user CB
 * observation first. It does NOT assert CB0 is idle/free/owned. No protected
 * last-CB sweep is inferred from ID1. Zero mask means global/stream-only.
 */
enum lg3_status lg3_discover_once(struct lg3_snapshot *,const struct lg3_io *,
                                unsigned enabled,uint32_t cb_read_mask,
                                unsigned cpu_pa_bits);
int lg3_read_allowed(uint32_t);
int lg3_sample_value(const struct lg3_snapshot *,uint32_t offset,uint32_t *);
unsigned lg3_address_bits(unsigned encoding);

struct lg3_assignment {
    unsigned smr_index,cb_index,asid;
    uint16_t sid; /* explicit; no default SID0 selection */
};
struct lg3_store { uint32_t offset; uint64_t value; unsigned width; };
#define LG3_PLAN_STORES 9u
struct lg3_plan {
    struct lg3_assignment assignment;
    struct lg3_store store[LG3_PLAN_STORES];
    unsigned count,pa_bits;
    uint32_t original_smr,original_s2cr,original_cbar,original_cba2r;
    uint64_t pool_pa,table_pa;
};
/* Builds register VALUES only. The selected route must already be exact and
 * translating to this disabled CB. Never allocates a stream/context or writes
 * SMR/S2CR/global control. Stable snapshots do not establish ownership.
 */
enum lg3_status lg3_stage1_plan(const struct lg3_snapshot *,
    const struct lg3_assignment *,const struct gs_domain *,struct lg3_plan *);
struct lg3_program_io {
    struct lg3_io io;
    /* Platform must acquire actual exclusive routing ownership AND keep DMA
     * quiesced. There is deliberately no built-in successful claim provider.
     */
    int (*claim_quiesced)(void *,const struct lg3_assignment *);
    void (*finish_claim)(void *,unsigned retain);
    /* Sync the exact retained arena; data pool was cleaned outside G1 and
     * must have had no CPU writes since. Complete DSB before returning.
     */
    int (*publish_tables)(void *,uint64_t table_pa);
    int (*write)(void *,uint32_t offset,uint64_t,unsigned width);
    int (*barrier)(void *);
};
struct lg3_program_result {
    unsigned used,attempts,hold_dependencies,installed;
    unsigned tlb_polls,tlb_status_valid;
    uint32_t last_tlb_status;
    enum lg3_status status;
    uint64_t start_us,last_us;
};
/* Compile-time LG3_ENABLE_STAGE1_WRITES=0 is the shipped default. Even when
 * compiled on, enabled must be explicit and the real claim callback supplied.
 * Any attempted MMIO store retains the pool, tables and G1 dependencies, on
 * success as well as failure. No automatic rollback/unmap/reset/retry.
 */
enum lg3_status lg3_stage1_program(struct lg3_program_result *,
    const struct lg3_program_io *,unsigned enabled,const struct lg3_plan *);
#endif
