#ifndef LITHIUM_TOUCH_R21_PROBE_H
#define LITHIUM_TOUCH_R21_PROBE_H

#include "qup-combined.h"
#include "touch-r21-options.h"

/* First transaction only. Ownership is supplied by a separately verified
 * platform adapter. Merely parsing touch_stage, or seeing an idle status,
 * does not prove ownership. Zero-initialize and serialize on one worker. */
struct lithium_r21_probe {
    bool attempted;
    bool parked_or_quarantined;
    struct lqt_outcome last;
};

static inline enum lqt_status
lithium_r21_read_id_once(struct lithium_r21_probe *probe, const struct lqt_io *io,
    unsigned long long affinity, int max_cpu, uint64_t deadline, uint8_t *id)
{
    if (!probe || !id || !deadline || probe->attempted) return LQT_INVALID;
    if (affinity != 1 || max_cpu != 0 || !lqt_ready(io))
        return LQT_PLATFORM_UNVERIFIED;

    probe->attempted = true;
    probe->last = lqc_probe_chip_id(io, id, deadline);
    /* The parked controller stays claimed; any failure after programming
     * requires quarantine. Neither result is safe-release evidence. */
    probe->parked_or_quarantined = probe->last.status == LQT_OK ||
        probe->last.cleanup != LQT_OK;
    return probe->last.status;
}

#endif
