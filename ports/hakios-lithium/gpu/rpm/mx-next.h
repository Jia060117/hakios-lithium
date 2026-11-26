/* SPDX-License-Identifier: MIT
 * Minimal active-set MX corner handoff and round trip. No hardware access.
 */
#ifndef HAKIOS_MX_NEXT_H
#define HAKIOS_MX_NEXT_H
#include "rpm-host.h"

#define MX_NEXT_REQ_BYTES 40u
#define MX_NEXT_FRAME_BYTES 56u
#define MX_NEXT_HANDOFF_BYTES 48u
#define MX_NEXT_TARGET 3u /* selector4 minus CORNER_NONE1, never microvolts */
#define MX_NEXT_MAX_CORNER 6u
#define MX_NEXT_PROPERTY "lithium-rpm-mx-original"

enum mx_next_rc {
    MX_NEXT_OK = 0, MX_NEXT_IGNORE = 1, MX_NEXT_AGAIN = 2, MX_NEXT_NACK = 3,
    MX_NEXT_EINVAL = -1, MX_NEXT_EUNKNOWN = -2, MX_NEXT_EPROTO = -3,
    MX_NEXT_ESTATE = -4, MX_NEXT_EID = -5
};
/* known=false remains unknown even when wire_corner happens to be zero.
 * A known zero is supported ONLY from an exact acknowledged request/shadow.
 */
struct mx_next_original {
    bool known;
    uint32_t wire_corner, ack_msgid;
    uint64_t boot_cookie;
};
struct mx_next_observer {
    struct mx_next_original current;
    uint32_t pending_id, pending_corner;
    unsigned unmatched;
    bool pending, uncertain;
};
/* The caller covers every AP active smpa2/corn publication until handoff.
 * The observer starts UNKNOWN; a fresh FIFO or lack of Mu writes never seeds
 * an old zero. Observe the actual published request, then its matching ACK.
 * Multi-KVP messages are accepted only with one well-formed corn KVP.
 */
void mx_next_observer_init(struct mx_next_observer *, uint64_t boot_cookie);
int mx_next_observe_request(struct mx_next_observer *, const uint8_t *, size_t);
int mx_next_observe_ack(struct mx_next_observer *, const uint8_t *, size_t);
void mx_next_observer_gap(struct mx_next_observer *);
int mx_next_export(const struct mx_next_observer *, uint8_t *, size_t);
int mx_next_import(const uint8_t *, size_t, uint64_t expected_boot_cookie,
                    struct mx_next_original *);
int mx_next_encode(uint32_t msgid, uint32_t wire_corner, uint8_t *, size_t);
/* Exact 40-byte active smpa2/id2/corn/uint32 codec only; no GET, uv or swen. */
int mx_next_validate(const uint8_t *, size_t, uint32_t *id, uint32_t *corner);
/* Exact GLINK intentless TX_DATA frame for local channel1. Useful for the
 * adapter's 14-word sequential whitelist; not a FIFO publication routine.
 */
int mx_next_frame(uint32_t msgid, uint32_t corner, uint8_t *, size_t);

struct mx_next_result {
    int rc, apply_rc, restore_rc;
    uint32_t original, target, apply_id, restore_id;
    bool changed, restored, uncertain;
    struct rpm_host_ack apply_ack, restore_ack;
};
/* Implemented in mx-next-runtime.inc, included after unchanged rpm-host.c
 * in the same translation unit. It uses its bounded GLINK internals without
 * widening the existing MX setter or OPEN-only adapter.
 * Caller supplies the SAME live, mapped OPEN session and writer callbacks
 * able to whitelist exactly this pair of requests. No rediscover/open/reset.
 * One shared budget covers both phases. Maximum limits 10000 I/O callbacks,
 * 2048 steps, 100000 us; no request retries. NACK/uncertainty never claims
 * restoration. A higher original corner is preserved without a request.
 */
int mx_next_roundtrip(struct rpm_host *, struct rpm_host_limits,
                      const struct mx_next_original *, struct mx_next_result *);
#endif
