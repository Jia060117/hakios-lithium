/* SPDX-License-Identifier: MIT
 * Host-prepared, callback-only MSM8996 RPM GLINK model. No runtime adapter.
 */
#ifndef HAKIOS_RPM_HOST_H
#define HAKIOS_RPM_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RPM_HOST_PAYLOAD_MAX 256u
#define RPM_HOST_ERROR_MAX 128u
#define RPM_HOST_CHANNEL 1u
/* Local admission limits, not claimed protocol maxima. */
#define RPM_HOST_OPEN_NAME_MAX 32u
#define RPM_HOST_FOREIGN_OPEN_MAX 4u
struct rpm_host_foreign_open {
    uint16_t rcid, name_len;
    uint8_t name[RPM_HOST_OPEN_NAME_MAX];
    bool acked, consumed;
};
#define RPM_HOST_MX_SELECTOR 4u
#define RPM_HOST_MX_WIRE_CORNER 3u /* DT selector4 - CORNER_NONE1; never uV */

enum rpm_host_status {
    RPM_HOST_OK = 0,
    RPM_HOST_AGAIN = 1,
    RPM_HOST_NACK = 2,
    RPM_HOST_BUS_DEFERRED = 3,
    RPM_HOST_EINVAL = -1,
    RPM_HOST_EIO = -2,
    RPM_HOST_EBOUNDS = -3,
    RPM_HOST_EPROTO = -4,
    RPM_HOST_ETIME = -5,
    RPM_HOST_EBUDGET = -6,
    RPM_HOST_EOWNERSHIP = -7,
    RPM_HOST_ESTATE = -8
};
enum rpm_host_order { RPM_HOST_ACQUIRE, RPM_HOST_RELEASE };

struct rpm_host_binding {
    uint64_t ram_pa, doorbell_pa;
    uint32_t ram_size, doorbell_mask, gic_spi, irq_flags;
    const char *compatible, *edge, *channel;
};
extern const struct rpm_host_binding rpm_host_msm8996;

/* Numeric B/s conversion is an annotation, NOT an RPM bus/clock vote.
 * DT master/slave IDs are not RPM resource IDs.
 */
struct rpm_host_bus_plan {
    uint32_t master, slave, index, ab_kbps, ib_kbps;
    uint64_t ab_bps, ib_bps;
    uint32_t master_rpm_id, slave_rpm_id;
    bool wire_encoding_available;
};
extern const struct rpm_host_bus_plan rpm_host_stock_bus3;

/* Callbacks are supplied ONLY by host tests in this package. Addresses are
 * concrete DT physical addresses so a fake can verify the binding. Each
 * callback must itself return promptly; no volatile pointer dereferences,
 * mapping, IRQ registration, kernel hook or device launcher is provided.
 * The integrator must provide real exclusivity/ordering/lifetime evidence
 * before separately designing any runtime adapter. Single caller only.
 */
struct rpm_host_io {
    void *ctx;
    int (*read32)(void *, uint64_t, uint32_t *);
    int (*write32)(void *, uint64_t, uint32_t);
    int (*order)(void *, enum rpm_host_order);
    uint64_t (*now_us)(void *);
    int (*wait_us)(void *, uint32_t);
};
struct rpm_host_limits {
    uint32_t io_calls, steps, timeout_us;
};
struct rpm_host_ack {
    uint32_t msgid;
    bool error; /* presence of err => NACK, not an invented numeric errno */
    uint32_t error_len;
    uint8_t error_bytes[RPM_HOST_ERROR_MAX];
};
struct rpm_host_stats {
    uint32_t reads, writes, orders, waits, kicks, frames_tx, frames_rx;
    uint32_t unmatched_acks;
};
struct rpm_host_fifo { uint32_t off, size, pos; };
struct rpm_host {
    struct rpm_host_io io;
    struct rpm_host_fifo tx, rx;
    struct rpm_host_stats stats;
    struct rpm_host_ack ack;
    uint64_t deadline, last_time;
    uint32_t calls_left, steps_left, next_msgid, pending_msgid;
    uint16_t rcid;
    unsigned foreign_open_count;
    struct rpm_host_foreign_open foreign_open[RPM_HOST_FOREIGN_OPEN_MAX];
    int fault;
    bool initialized, discovered, started, peer_version, version_acked;
    bool local_open, remote_open, open_acked, ack_ready;
};

int rpm_host_init(struct rpm_host *, const struct rpm_host_io *);
/* Read-only: stable TOC double snapshot, no FIFO index reset. */
int rpm_host_discover(struct rpm_host *, struct rpm_host_limits);
/* Fresh, EXCLUSIVELY owned edge assertion is a HOST MODEL precondition.
 * Refuses nonzero initial indices; never takes over an existing live edge.
 * Faults/timeouts latch permanently. Reopening/retrying is unsupported.
 */
int rpm_host_open(struct rpm_host *, struct rpm_host_limits,
                  bool exclusive_fresh_edge);
int rpm_host_mx_corner4(struct rpm_host *, struct rpm_host_limits,
                        struct rpm_host_ack *);
/* Pure encoders/parsers: no callbacks. Active-set corn3 ONLY; no swen vote,
 * sleep vote, guessed voltage, clock scaling, global clock handoff or retry.
 */
int rpm_host_encode_mx_corner4(uint32_t msgid, uint8_t *out, size_t capacity,
                               size_t *written);
int rpm_host_decode_ack(const uint8_t *, size_t, struct rpm_host_ack *);
/* Always deferred, sets written=0 and leaves output bytes untouched. */
int rpm_host_encode_bus3(uint32_t msgid, uint8_t *out, size_t capacity,
                         size_t *written);
#endif
