/* SPDX-License-Identifier: MIT -- one local CX request/restore, no GPU access. */
#ifndef LITHIUM_CX_CYCLE_CORE_H
#define LITHIUM_CX_CYCLE_CORE_H
#include <stdint.h>
#define LGCX_CONTROL_PA UINT64_C(0x8c4034)
#define LGCX_STATUS_PA UINT64_C(0x8c4038)
#define LGCX_ON UINT32_C(0x80000000)
/* MiCode gdsc.c @1492c8e4373b7428bb0accc4f4881e97b36508b9,
 * physical lines28-34,201-216,305-329,569-583. Other bits are not
 * assigned software ownership here; every store preserves their live RMW value. */
#define LGCX_COLLAPSE UINT32_C(1)
#define LGCX_HW_CONTROL UINT32_C(2)
#define LGCX_SW_OVERRIDE UINT32_C(4)
#define LGCX_MODE (LGCX_HW_CONTROL | LGCX_SW_OVERRIDE)
#define LGCX_TIMING UINT32_C(0x00fff000)
#define LGCX_WAIT UINT32_C(0x00282000)
#define LGCX_RESTORE UINT32_C(0x00fff001)
#define LGCX_OWNED (LGCX_RESTORE | LGCX_MODE)
#define LGCX_NORMAL_US UINT64_C(15000)
#define LGCX_TOTAL_US UINT64_C(20000)
#define LGCX_ON_US UINT64_C(500)
#define LGCX_POLL_CAP 500u
#define LGCX_IO_CAP 2048u
#define LGCX_CAPTURE_CAP 4u
/* Bounded raw capture retained in owned-field revision03. */
enum lgcx_phase { LGCX_PHASE_NONE=0,LGCX_PHASE_SNAPSHOT_CONTROL,
    LGCX_PHASE_SNAPSHOT_STATUS,LGCX_PHASE_PRE_STATUS,LGCX_PHASE_TIMING_SET,
    LGCX_PHASE_REQUEST_ON,LGCX_PHASE_ON_WAIT,LGCX_PHASE_ON_SETTLE,
    LGCX_PHASE_ON_VERIFY,LGCX_PHASE_RESTORE_REQUEST,LGCX_PHASE_RESTORE_WAIT,
    LGCX_PHASE_RESTORE_TIMING,LGCX_PHASE_FINAL_CONTROL,LGCX_PHASE_FINAL_STATUS };
enum lgcx_step { LGCX_STEP_NONE=0,LGCX_STEP_PRE_READ,LGCX_STEP_OWNERSHIP,
    LGCX_STEP_TIMING_GUARD,LGCX_STEP_COMPUTE,LGCX_STEP_WRITE_GATE,
    LGCX_STEP_WRITE,LGCX_STEP_WRITE_POST,LGCX_STEP_BARRIER,
    LGCX_STEP_AFTER_READ,LGCX_STEP_COMPARE,LGCX_STEP_DONE,
    LGCX_STEP_DELAY,LGCX_STEP_READ,LGCX_STEP_FINAL_COMPARE };
struct lgcx_change_capture {
    unsigned phase,step;
    uint32_t mask,value,prior_shadow,prior_attempted,pre,expected,after;
    unsigned prior_uncertain,pre_callback_ok,pre_valid,expected_valid;
    unsigned store_attempted,after_callback_ok,after_valid;
    int rc;
};
enum lgcx_error { LGCX_OK=0,LGCX_ARGUMENT=-1,LGCX_TIME=-2,LGCX_IO=-3,
    LGCX_HANDOFF=-4,LGCX_READBACK=-5,LGCX_ON_TIMEOUT=-6,LGCX_POLL=-7,
    LGCX_CONTEXT=-8,LGCX_MAP=-9,LGCX_BOUNDS=-10,LGCX_BUDGET=-11 };
struct lgcx_ops {
    void *ctx;
    int (*guard)(void *);
    int (*read32)(void *,uint64_t,uint32_t *);
    int (*write32)(void *,uint64_t,uint32_t);
    int (*barrier)(void *);
    int (*delay_us)(void *,uint32_t);
    uint64_t (*now_us)(void *);
};
struct lgcx_result {
    int error,cleanup_error;
    uint32_t original_control,initial_status,on_control,on_status,final_control,final_status;
    unsigned snapshot_valid,initial_status_valid,on_valid,final_control_valid,final_status_valid;
    unsigned inherited,inherited_reason,request_attempted,on_observed,active_verified;
    unsigned reads,write_attempts,writes_returned,barriers,delays,polls,io_calls;
    unsigned dirty,uncertain,ambiguous_stores,restore_verified,cleanup_complete;
    uint64_t started_us,last_us,poll_elapsed_us,elapsed_us;
    unsigned error_phase,error_step,error_change,cleanup_error_phase,cleanup_error_step,cleanup_error_change;
    unsigned capture_count,capture_dropped;
    struct lgcx_change_capture capture[LGCX_CAPTURE_CAP];
};
struct lgcx_core {
    struct lgcx_ops ops;
    struct lgcx_result result;
    uint32_t shadow,attempted;
    unsigned cleanup,poll_active;
    uint64_t poll_start;
    unsigned phase,step,change_index,read_callback_ok;
};
/* One operation. Normal<=15ms, cleanup within same absolute20ms budget.
 * Already-on/inherited local requests are recorded without any control store.
 * No retry, global-off requirement, source-vote token or parent-rail removal.
 */
int lgcx_core_run(struct lgcx_core *,const struct lgcx_ops *,uint64_t started_us);
#endif
