/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LITHIUM_G4_XNU_H
#define LITHIUM_G4_XNU_H
#include "g4-live.h"
#include "g4-stop.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Provider supplied by the actual g3 BO/window owner. No defaults, no SID0,
 * fixed IOVA, identity PA->IOVA cast, fake coherency or stopped-DMA result. */
struct g4lx_provider {
    void *cookie;
    int (*guard)(void *,enum g4l_requirement,uint64_t window_cookie);
    int (*acquire)(void *,enum g4l_role,size_t,size_t,unsigned,struct g4l_bo *);
    void (*release)(void *,enum g4l_role,const struct g4l_bo *);
    int (*sync)(void *,const struct g4l_bo *,size_t,size_t,enum g4l_sync);
    int (*dma_stopped)(void *,uint64_t window_cookie);
};
struct g4lx_native {
    unsigned used, ready;
    uint64_t window_cookie;
    volatile uint32_t *mmio[16]; /* only slots0,3,10,15 mapped */
    struct g4lx_provider provider;
    /* Raw last hardware access retained even if the post-access guard fails.
     * io_executed=0 means value is not an observation. No zero substitution. */
    uint32_t last_reg,last_value;
    unsigned io_executed,io_write;
    int last_io_status;
};
/* Native XNU arm64 TU. Maps four 16KiB Device pages for low globals/CP,
 * VBIF, GPMU read-only observation, and Linux TSB setup. No SMMU mapping;
 * 0xf400 TRUST is NEVER read/written, even though it shares the TSB IO page.
 * Context + ops must outlive all retained BOs. Mapping is intentionally kept.
 * No kernel boot hook is installed; main orchestrator must call explicitly.
 */
enum g4l_status g4lx_bind(struct g4lx_native *,unsigned enabled,
                        uint64_t window_cookie,const struct g4lx_provider *,
                        struct g4l_ops *out);
/* RAM-only journal logging AFTER leaving the bounded live window. No MMIO. */
void g4lx_log(const struct g4lx_native *,const struct g4l_context *);
/* Merely binds the separately allowlisted reset/halt IO callbacks; executes
 * no hardware operation. g4l_hard_stop still requires an explicit enabled1. */
enum g4l_status g4lx_bind_stop(struct g4lx_native *,struct g4l_stop_ops *);
#ifdef __cplusplus
}
#endif
#endif
