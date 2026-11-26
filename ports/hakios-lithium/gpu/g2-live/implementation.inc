/* SPDX-License-Identifier: BSD-2-Clause
 * One native GPU worker TU. No boot hook/options are installed here.
 */
#ifndef LITHIUM_G2_LIVE_IMPLEMENTATION_INC
#define LITHIUM_G2_LIVE_IMPLEMENTATION_INC
#include "g2-live.h"

/* Existing g2-zap.c is deliberately reused unchanged. Its private generic
 * names must not collide with rpm-host/clock cores in hakios.input.c.
 */
#pragma push_macro("u16")
#pragma push_macro("u32")
#pragma push_macro("equal")
#pragma push_macro("end_of")
#pragma push_macro("overlaps")
#pragma push_macro("allocation")
#pragma push_macro("hashes")
#pragma push_macro("digest")
#pragma push_macro("fail")
#pragma push_macro("gate")
#pragma push_macro("write_blob")
#pragma push_macro("sync_allocation")
#pragma push_macro("call")
#undef u16
#undef u32
#undef equal
#undef end_of
#undef overlaps
#undef allocation
#undef hashes
#undef digest
#undef fail
#undef gate
#undef write_blob
#undef sync_allocation
#undef call
#define u16 lg2z_u16
#define u32 lg2z_u32
#define equal lg2z_equal
#define end_of lg2z_end_of
#define overlaps lg2z_overlaps
#define allocation lg2z_allocation
#define hashes lg2z_hashes
#define digest lg2z_digest
#define fail lg2z_fail
#define gate lg2z_gate
#define write_blob lg2z_write_blob
#define sync_allocation lg2z_sync_allocation
#define call lg2z_call
#include "../g2-zap.c"
#pragma pop_macro("call")
#pragma pop_macro("sync_allocation")
#pragma pop_macro("write_blob")
#pragma pop_macro("gate")
#pragma pop_macro("fail")
#pragma pop_macro("digest")
#pragma pop_macro("hashes")
#pragma pop_macro("allocation")
#pragma pop_macro("overlaps")
#pragma pop_macro("end_of")
#pragma pop_macro("equal")
#pragma pop_macro("u32")
#pragma pop_macro("u16")

#include "g2-receipt.c"
#include "g2-meta.c"
#include "g2-live.c"

#if defined(__has_include)
#if __has_include("../../../../local-inputs/gpu-zap/zap-local-inputs.inc")
#include "../../../../local-inputs/gpu-zap/zap-local-inputs.inc"
#define LG2LX_LOCAL_INPUTS_PRESENT 1
#endif
#endif
#if defined(LG2LX_LOCAL_INPUTS_PRESENT)
static const struct lg2_image lg2l_workspace_stock={
    {lithium_g2_zap_mdt,sizeof(lithium_g2_zap_mdt)},
    {{lithium_g2_zap_b00,sizeof(lithium_g2_zap_b00)},
     {lithium_g2_zap_b01,sizeof(lithium_g2_zap_b01)},
     {lithium_g2_zap_b02,sizeof(lithium_g2_zap_b02)}}
};
static const struct lg2_image *lg2lx_local_image(void)
{ return &lg2l_workspace_stock; }
#else
static const struct lg2_image *lg2lx_local_image(void)
{ return NULL; }
#endif

#include "live-adapter.inc"
#endif
