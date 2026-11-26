/* SPDX-License-Identifier: MIT -- bounded GCC BIMC debug measurement only. */
#ifndef LITHIUM_BIMC_MEASURE_CORE_H
#define LITHIUM_BIMC_MEASURE_CORE_H
#include <stdint.h>
#define LGBM_MUX_PA UINT64_C(0x362000)
#define LGBM_CTL_PA UINT64_C(0x362004)
#define LGBM_STATUS_PA UINT64_C(0x362008)
#define LGBM_XO_PA UINT64_C(0x343008)
#define LGBM_RUN UINT32_C(0x00100000)
#define LGBM_DONE UINT32_C(0x02000000)
#define LGBM_COUNT UINT32_C(0x01ffffff)
#define LGBM_MUX_MASK UINT32_C(0x0001f3ff)
#define LGBM_MUX_VALUE UINT32_C(0x000100ad)
#define LGBM_CTL_MASK UINT32_C(0x001fffff)
#define LGBM_WINDOW_US UINT64_C(20000)
#define LGBM_RUN_US UINT64_C(40000)
#define LGBM_GLOBAL_US UINT64_C(45000)
#define LGBM_POLL_CAP 2000u /* shared by DONE0 and DONE1 within each window */
#define LGBM_IO_CAP 12000u
enum lgbm_error { LGBM_OK=0,LGBM_ARGUMENT=-1,LGBM_TIME=-2,LGBM_WINDOW=-3,
    LGBM_IO=-4,LGBM_HANDOFF=-5,LGBM_READBACK=-6,LGBM_BUSY=-7,
    LGBM_POLL=-8,LGBM_BUDGET=-9,LGBM_COUNT_ERROR=-10,LGBM_CONTEXT=-11,
    LGBM_MAP_ERROR=-12,LGBM_BOUNDS=-13 };
enum lgbm_register { LGBM_MUX,LGBM_CTL,LGBM_XO,LGBM_REGS };
struct lgbm_ops {
    void *ctx;
    int (*guard)(void *);
    int (*read32)(void *,uint64_t,uint32_t *);
    int (*write32)(void *,uint64_t,uint32_t);
    int (*barrier)(void *);
    int (*delay_us)(void *,uint32_t);
    uint64_t (*now_us)(void *);
};
struct lgbm_result {
    int error,cleanup_error;
    uint32_t original[3],final[3],snapshot_valid,restore_verified,dirty,uncertain;
    uint32_t initial_status,ready_status[2],done_status[2],count[2],window_valid;
    unsigned reads,write_attempts,writes_returned,barriers,delays,polls[2],io_calls;
    unsigned ambiguous_stores,measurement_valid,clock_off,cleanup_complete;
    uint64_t hz,started_us,last_us,elapsed_us;
};
struct lgbm_core {
    struct lgbm_ops ops;
    struct lgbm_result result;
    uint32_t shadow[3],attempted[3];
    uint64_t window_start;
    unsigned cleanup,window_active,window_index;
};
/* One call, two fresh windows, no retry. Preserve parent clocks/RPM votes.
 * Poll/time limits cover callbacks which RETURN, not stalled MMIO buses.
 * started_us includes adapter mapping time; reserve final5ms for cleanup.
 */
int lgbm_core_run(struct lgbm_core *,const struct lgbm_ops *,uint64_t started_us);
#endif
