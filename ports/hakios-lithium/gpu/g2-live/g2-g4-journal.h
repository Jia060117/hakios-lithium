/* SPDX-License-Identifier: GPL-2.0-only
 * Copy observations received INSIDE the accepted G2 handler. No eligibility,
 * SMMU setup, CP stop proof, timestamps or cookies are synthesized here. */
#ifndef LITHIUM_G2_G4_JOURNAL_H
#define LITHIUM_G2_G4_JOURNAL_H
#include "g2-live.h"
#include "../g4-live/g4-live.h"
static inline void lg2l_g4_auth_copy(const struct lg2l_auth_journal *j,
                                    struct g4l_pas_auth *a)
{
    *a=(struct g4l_pas_auth){.boot_cookie=j->boot_cookie,
        .window_cookie=j->window_cookie,.before_ns=j->stamp.before_ns,
        .after_ns=j->stamp.after_ns,.function_id=j->raw.input[0],
        .pas_id=j->raw.input[2],.callback_status=j->raw.callback_status};
    for(unsigned i=0;i<4u;++i)a->x[i]=j->raw.output[i];
}
#endif
