/* SPDX-License-Identifier: BSD-2-Clause
 * Scalar read-only SCM query plan; no allocation, PAS INIT or pool pointer.
 */
#ifndef LITHIUM_G2_SCM_AVAILABILITY_H
#define LITHIUM_G2_SCM_AVAILABILITY_H
#include <stdint.h>

enum lg2q_abi { LG2Q_SMC32=1, LG2Q_SMC64=2 };
enum lg2q_status {
    LG2Q_OK, LG2Q_ARGUMENT, LG2Q_USED, LG2Q_CONTEXT, LG2Q_CALLBACK,
    LG2Q_TRANSPORT, LG2Q_CONVENTION_UNCONFIRMED, LG2Q_NOT_ALL_AVAILABLE
};
struct lg2q_frame {
    uint64_t input[8], output[4];
    int callback_status;
};
struct lg2q_result {
    unsigned used, calls, abi_confirmed, availability_known, available;
    enum lg2q_abi candidate;
    enum lg2q_status status;
    struct lg2q_frame frame[4]; /* self, INIT availability, MEM, AUTH */
};
struct lg2q_io {
    void *ctx;
    int (*guard)(void *); /* caller's existing CPU/context/serialization check */
    int (*call)(void *, const uint64_t in[8], uint64_t out[4]);
};
/* Exactly one chosen candidate, no automatic SMC32 fallback/retry.
 * On valid self-query, obtain all three command availability results.
 * x0 != 0 is terminal/unknown, including INTERRUPTED/BUSY; never translated
 * into a false "unsupported". This does not establish per-PAS13 support.
 */
enum lg2q_status lg2q_run_once(struct lg2q_result *, const struct lg2q_io *,
                              enum lg2q_abi);
#endif
