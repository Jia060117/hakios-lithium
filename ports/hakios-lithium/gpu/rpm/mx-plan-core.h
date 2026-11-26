/* SPDX-License-Identifier: MIT -- sparse, read-only SMEM473/version1 capture. */
#ifndef LITHIUM_MX_PLAN_CORE_H
#define LITHIUM_MX_PLAN_CORE_H
#include <stdint.h>
#define LGMP_BASE UINT64_C(0x86000000)
#define LGMP_BYTES UINT64_C(0x200000)
#define LGMP_TIME_US UINT64_C(10000)
#define LGMP_READ_CAP 66u
#define LGMP_WORD_CAP 28u
#define LGMP_TOC_WORDS 5u
#define LGMP_TOC_ALL 31u
#define LGMP_RAIL_ID UINT32_C(0x100)
enum lgmp_error { LGMP_OK=0,LGMP_ARGUMENT=-1,LGMP_TIME=-2,LGMP_IO=-3,
    LGMP_CONTEXT=-4,LGMP_BOUNDS=-5,LGMP_BUDGET=-6,LGMP_DESCRIPTOR=-7,
    LGMP_VERSION=-8,LGMP_RAIL=-9,LGMP_KIND=-10,LGMP_MODES=-11,
    LGMP_CORNER=-12,LGMP_VOLTAGE=-13,LGMP_CHANGED=-14,LGMP_MAP=-15 };
struct lgmp_ops {
    void *ctx;
    int (*guard)(void *);
    int (*read32)(void *,uint64_t,uint32_t *,unsigned *loaded);
    uint64_t (*now_us)(void *);
};
struct lgmp_result {
    int error,io_error,format_error;
    uint32_t toc[2][LGMP_TOC_WORDS],offset[LGMP_WORD_CAP],word[2][LGMP_WORD_CAP];
    uint32_t toc_loaded[2],toc_valid[2],word_loaded[2],word_valid[2];
    unsigned word_count,read_calls,completed_sweeps,toc_stable,item_stable,stable;
    unsigned descriptor_valid,header_valid,rows_valid,voltage_fields_valid,decoded_valid,bound_850;
    uint32_t item_offset,item_size,version,rail_count,header_offset,kind,parameter0,parameter1;
    uint32_t mode_count,modes_offset,mode_row,resolved_uv,ceiling_uv,floor_uv,initial_uv,required_uv;
    uint64_t started_us,last_us,elapsed_us;
};
struct lgmp_core {
    struct lgmp_ops ops;
    struct lgmp_result result;
    unsigned active,active_item,active_pass,active_index;
};
/* Item offsets/layout come from the stock RPM.BF.1.6 decoder, not SDM660 v2.
 * Invalid/unknown formats still repeat the same safely captured prefix.
 * No writes, allocation, polling, retries, old AP receipt or voltage vote. */
uint64_t lgmp_descriptor_pa(unsigned);
int lgmp_core_run(struct lgmp_core *,const struct lgmp_ops *,uint64_t started_us);
#endif
