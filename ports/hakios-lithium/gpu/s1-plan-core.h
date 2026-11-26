/* SPDX-License-Identifier: MIT -- sparse, read-only SMEM473/version1 capture. */
#ifndef LITHIUM_S1_PLAN_CORE_H
#define LITHIUM_S1_PLAN_CORE_H
#include <stdint.h>
#define LGSP_BASE UINT64_C(0x86000000)
#define LGSP_BYTES UINT64_C(0x200000)
#define LGSP_TIME_US UINT64_C(10000)
#define LGSP_READ_CAP 66u
#define LGSP_WORD_CAP 28u
#define LGSP_TOC_WORDS 5u
#define LGSP_TOC_ALL 31u
#define LGSP_RAIL_ID UINT32_C(0x101)
enum lgsp_error { LGSP_OK=0,LGSP_ARGUMENT=-1,LGSP_TIME=-2,LGSP_IO=-3,
    LGSP_CONTEXT=-4,LGSP_BOUNDS=-5,LGSP_BUDGET=-6,LGSP_DESCRIPTOR=-7,
    LGSP_VERSION=-8,LGSP_RAIL=-9,LGSP_KIND=-10,LGSP_MODES=-11,
    LGSP_CORNER=-12,LGSP_VOLTAGE=-13,LGSP_CHANGED=-14,LGSP_MAP=-15 };
struct lgsp_ops {
    void *ctx;
    int (*guard)(void *);
    int (*read32)(void *,uint64_t,uint32_t *,unsigned *loaded);
    uint64_t (*now_us)(void *);
};
struct lgsp_result {
    int error,io_error,format_error;
    uint32_t toc[2][LGSP_TOC_WORDS],offset[LGSP_WORD_CAP],word[2][LGSP_WORD_CAP];
    uint32_t toc_loaded[2],toc_valid[2],word_loaded[2],word_valid[2];
    unsigned word_count,read_calls,completed_sweeps,toc_stable,item_stable,stable;
    unsigned descriptor_valid,header_valid,rows_valid,voltage_fields_valid,decoded_valid,bound_850;
    uint32_t item_offset,item_size,version,rail_count,header_offset,kind,parameter0,parameter1;
    uint32_t mode_count,modes_offset,mode_row,resolved_uv,ceiling_uv,floor_uv,initial_uv,required_uv;
    uint64_t started_us,last_us,elapsed_us;
};
struct lgsp_core {
    struct lgsp_ops ops;
    struct lgsp_result result;
    unsigned active,active_item,active_pass,active_index;
};
/* Item offsets/layout come from the stock RPM.BF.1.6 decoder, not SDM660 v2.
 * Invalid/unknown formats still repeat the same safely captured prefix.
 * No writes, allocation, polling, retries, old AP receipt or voltage vote. */
uint64_t lgsp_descriptor_pa(unsigned);
int lgsp_core_run(struct lgsp_core *,const struct lgsp_ops *,uint64_t started_us);
#endif
