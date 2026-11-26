/* SPDX-License-Identifier: MIT
 * Pure host plan for the pinned MSM8996 GPU vector3. No MMIO or transport.
 */
#ifndef HAKIOS_GPU_BUS_PLAN_H
#define HAKIOS_GPU_BUS_PLAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RPM_BUS_CLK_TYPE UINT32_C(0x326b6c63) /* downstream "clk2", NOT mclk */
#define RPM_BUS_RATE_KEY UINT32_C(0x007a484b) /* KHz, uint32 */
#define RPM_BUS_SLV_TYPE UINT32_C(0x766c7362) /* bslv */
#define RPM_BUS_BW_KEY UINT32_C(0x00007762)   /* bw, uint64 B/s */
#define RPM_BUS_IB_BPS UINT64_C(1600000000)
#define RPM_BUS_FLOOR_HZ UINT64_C(200000000)
#define RPM_BUS_FLOOR_KHZ UINT32_C(200000)
#define RPM_BUS_QOS_PA UINT64_C(0x0040c300)
#define RPM_BUS_MESSAGE_MAX 44u
#define RPM_BUS_ACTION_MAX 2u
#define RPM_BUS_UNMATCHED_MAX 8u
#define RPM_BUS_TIMEOUT_MAX_US 1000000u

enum rpm_bus_rc {
    RPM_BUS_OK = 0, RPM_BUS_AGAIN = 1, RPM_BUS_IDLE = 2, RPM_BUS_NACK = 3,
    RPM_BUS_EINVAL = -1, RPM_BUS_ESTATE = -2, RPM_BUS_EORIGINAL = -3,
    RPM_BUS_EOWNERSHIP = -4, RPM_BUS_EPROTO = -5, RPM_BUS_ETIME = -6,
    RPM_BUS_EID = -7, RPM_BUS_EBOUNDS = -8
};
enum rpm_bus_endpoint { RPM_BUS_BIMC_RATE, RPM_BUS_EBI_AB };
enum rpm_bus_kind { RPM_BUS_RPM, RPM_BUS_QOS };
enum rpm_bus_phase {
    RPM_BUS_APPLY, RPM_BUS_ACTIVE, RPM_BUS_RESTORE, RPM_BUS_DONE,
    RPM_BUS_UNCERTAIN
};

struct rpm_bus_node {
    uint32_t id, rpm_id, width, agg_ports;
    bool ap_owned, rpm_bw_path;
    uint64_t ab_bps, ib_bps, request_hz;
};
struct rpm_bus_derivation {
    struct rpm_bus_node node[2];
    uint32_t vector_index, fabric_id, util_percent, vrail_percent;
    uint64_t fabric_hz;
    uint32_t wire_khz;
};

/* Authority is the integrator's evidence, not something a pure encoder can
 * discover. clk_khz MUST be the original AP active-set clk2/id0/KHz KVP from
 * an authoritative request shadow, including every enabled AP peer/voter.
 * Actual frequency, aggregate RPM output, ACK and boot defaults cannot supply
 * this field. An absent/untracked KVP is unsupported, never assumed zero.
 * Resource exclusivity covers both peers and all voters until restoration.
 * qos_word is an exact stable read of 0x0040c300 under the same lease.
 * Sleep and slave AB are never changed by this active-only G1 subset.
 */
struct rpm_bus_original {
    uint32_t clk_khz, qos_word;
    bool clk_kvp_known_present, qos_word_known, exclusive_resources;
};
struct rpm_bus_step {
    enum rpm_bus_kind kind;
    uint32_t original, target;
};
struct rpm_bus_plan {
    struct rpm_bus_step step[RPM_BUS_ACTION_MAX];
    uint32_t count, floor_khz;
};
struct rpm_bus_action {
    enum rpm_bus_kind kind;
    bool restoring;
    uint32_t expected, desired, mask, msgid;
    uint64_t pa;
    size_t length;
    uint8_t message[RPM_BUS_MESSAGE_MAX];
};
/* Single caller. next() reserves ONE action and never returns it again.
 * The adapter must compare QoS expected before a masked write, order it,
 * read it back, and then call qos_complete(). No hardware is accessed here.
 * ACK faults/timeouts retain uncertainty and never silently retry/reopen.
 */
struct rpm_bus_session {
    struct rpm_bus_plan plan;
    struct rpm_bus_action pending;
    enum rpm_bus_phase phase;
    uint64_t deadline_us, last_us;
    uint32_t applied, last_msgid, unmatched;
    int fault;
    bool initialized, pending_valid;
};

void rpm_bus_derive_stock3(struct rpm_bus_derivation *);
int rpm_bus_make_plan(const struct rpm_bus_original *, struct rpm_bus_plan *);
/* Only active clk2/id0/KHz or bslv/id0/bw. bslv is a supported standalone
 * wire codec, NOT an action emitted for this AB0 vector. No master codec.
 * Failure sets written=0 and leaves output bytes untouched.
 */
int rpm_bus_encode_vote(enum rpm_bus_endpoint, uint32_t msgid, uint64_t value,
                        uint8_t *, size_t capacity, size_t *written);
/* The caller zero-initializes s once. Reinitializing a live or faulted session
 * is refused; storage must not be discarded before its result is retained.
 */
int rpm_bus_begin(struct rpm_bus_session *, const struct rpm_bus_original *,
                  uint32_t previous_msgid, uint64_t now_us, uint32_t timeout_us);
int rpm_bus_next(struct rpm_bus_session *, uint64_t now_us,
                 struct rpm_bus_action *);
int rpm_bus_ack(struct rpm_bus_session *, uint64_t now_us,
                const uint8_t *message, size_t length);
int rpm_bus_qos_complete(struct rpm_bus_session *, uint64_t now_us,
                         uint32_t before, uint32_t after);
/* Reverse only completed actions; usable for partial apply rollback.
 * Refuses an outstanding action because publication/result is uncertain.
 */
int rpm_bus_start_restore(struct rpm_bus_session *, uint64_t now_us);
bool rpm_bus_restored(const struct rpm_bus_session *);
#endif
