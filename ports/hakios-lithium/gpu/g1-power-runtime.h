/* SPDX-License-Identifier: GPL-2.0-only
 * Concrete read-only PMIC-arb v2 authorization evidence for RAIL ENABLE only.
 * Linux adc218... spmi-pmic-arb.c:1090-1104,1561-1564: v2 owner & 7 is
 * write_ee (also irq_ee). Stock DTS: cnfg=0x0400a000, APPS ee=0.
 * This is an additional conservative authorization check; the v2 Linux
 * offset function itself does not have the v5-only write_ee software check.
 * No IRQ ACC_ENABLE interpretation or ownership writes, no RPM votes.
 */
#ifndef LITHIUM_G1_POWER_RUNTIME_H
#define LITHIUM_G1_POWER_RUNTIME_H
#include "g1-power-spmi.h"
#define LGPR_CNFG_PA UINT64_C(0x0400a000)
#define LGPR_COUNT 13u
#define LGPR_ALL ((1u << LGPR_COUNT) - 1u)
enum lgpr_field {
    LGPR_VERSION, LGPR_MAP_GFX, LGPR_MAP_MX, LGPR_OWNER_GFX, LGPR_OWNER_MX,
    LGPR_CHANNEL_CONFIG, LGPR_CHANNEL_STATUS, LGPR_CX, LGPR_GX,
    LGPR_AHB, LGPR_AON, LGPR_GFX3D, LGPR_TIMER
};
static const uint64_t lgpr_pa[LGPR_COUNT] = {
    LGS_CORE_PA, LGS_CORE_PA + 0x800u + 4u * 52u,
    LGS_CORE_PA + 0x800u + 4u * 127u,
    LGPR_CNFG_PA + 0x700u + 4u * 52u,
    LGPR_CNFG_PA + 0x700u + 4u * 127u,
    LGPW_CHANNELS_PA + UINT64_C(0x8000) * 52u + 4u,
    LGPW_CHANNELS_PA + UINT64_C(0x8000) * 52u + 8u,
    UINT64_C(0x008c4034), UINT64_C(0x008c4024),
    UINT64_C(0x008c403c), UINT64_C(0x008c4044),
    UINT64_C(0x008c4028), UINT64_C(0x008c40b0)
};
enum lgpr_error {
    LGPR_OK = 0, LGPR_ARGUMENT, LGPR_READ, LGPR_TIME, LGPR_BUDGET,
    LGPR_CHANGED, LGPR_VERSION_BAD, LGPR_MAP_BAD, LGPR_NOT_APPS_OWNER,
    LGPR_CONFIG_BAD, LGPR_CHANNEL_UNPROVEN, LGPR_G0_NOT_COLLAPSED
};
struct lgpr_result {
    enum lgpr_error error;
    int callback_rc;
    uint32_t raw[2][LGPR_COUNT], valid[2], stable, reads;
    uint32_t write_ee_gfx, owner_mx, ee_from_stock_dts;
    uint32_t rail_enable_eligible;
    /* Explicitly stay false: this provider is not consumer/GPU admission. */
    uint32_t rpm_vote_ack_proven, gpu_bus_ready_proven, tz_never_writes_proven;
    uint64_t start_us, last_us, access_pa;
};
static inline enum lgpr_error
lgpr_capture(const struct lgs_io *io, struct lgpr_result *r)
{
    if (!r) return LGPR_ARGUMENT;
    *r = (struct lgpr_result){0};
    if (!io || !io->read32 || !io->time_us) return r->error = LGPR_ARGUMENT;
    int rc = io->time_us(io->context, &r->start_us);
    if (rc) { r->callback_rc=rc; return r->error=LGPR_TIME; }
    r->last_us=r->start_us;
    for (unsigned pass=0;pass<2;++pass) {
        for (unsigned f=0;f<LGPR_COUNT;++f) {
            uint64_t now=0;
            rc=io->time_us(io->context,&now);
            if (rc || now<r->last_us) { r->callback_rc=rc; return r->error=LGPR_TIME; }
            r->last_us=now;
            if (now-r->start_us>=1000u) return r->error=LGPR_BUDGET;
            r->access_pa=lgpr_pa[f]; ++r->reads;
            uint32_t raw=0;
            rc=io->read32(io->context,r->access_pa,&raw);
            if (rc) { r->callback_rc=rc; return r->error=LGPR_READ; }
            r->raw[pass][f]=raw;
            rc=io->time_us(io->context,&now);
            if (rc || now<r->last_us) { r->callback_rc=rc; return r->error=LGPR_TIME; }
            r->last_us=now;
            if (now-r->start_us>=1000u) return r->error=LGPR_BUDGET;
            r->valid[pass]|=1u<<f;
        }
    }
    for (unsigned f=0;f<LGPR_COUNT;++f)
        if (r->raw[0][f]==r->raw[1][f]) r->stable|=1u<<f;
    if (r->stable!=LGPR_ALL) return r->error=LGPR_CHANGED;
    const uint32_t *v=r->raw[1];
    r->write_ee_gfx=v[LGPR_OWNER_GFX]&7u;
    r->owner_mx=v[LGPR_OWNER_MX]&7u;
    r->ee_from_stock_dts=LGS_EE; /* configured EE, not a hardware EE register */
    if (v[LGPR_VERSION]!=LGS_V2_VERSION) return r->error=LGPR_VERSION_BAD;
    if (((v[LGPR_MAP_GFX]>>8)&0xfffu)!=0x317u ||
        ((v[LGPR_MAP_MX]>>8)&0xfffu)!=0x117u) return r->error=LGPR_MAP_BAD;
    if (r->write_ee_gfx!=LGS_EE) return r->error=LGPR_NOT_APPS_OWNER;
    if (v[LGPR_CHANNEL_CONFIG]) return r->error=LGPR_CONFIG_BAD;
    /* Source defines completion/errors, no standalone BUSY bit. Require a
     * stable clean previous DONE. Zero is unproven, not called "idle".
     * No dummy PMIC write is sent to manufacture this evidence. */
    if (v[LGPR_CHANNEL_STATUS]!=1u) return r->error=LGPR_CHANNEL_UNPROVEN;
    for (unsigned f=LGPR_CX;f<=LGPR_GX;++f)
        if ((v[f] & UINT32_C(0x80000007))!=1u)
            return r->error=LGPR_G0_NOT_COLLAPSED;
    for (unsigned f=LGPR_AHB;f<=LGPR_TIMER;++f)
        if ((v[f] & UINT32_C(0x80000001))!=UINT32_C(0x80000000))
            return r->error=LGPR_G0_NOT_COLLAPSED;
    r->rail_enable_eligible=1;
    return LGPR_OK;
}
#endif
