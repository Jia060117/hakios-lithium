/* SPDX-License-Identifier: GPL-2.0-only
 * Source-backed one-shot A530 stop. Separate from the bootstrap MMIO policy.
 * No stop register becomes writable through g4l_writable_reg().
 */
#ifndef LITHIUM_G4_STOP_H
#define LITHIUM_G4_STOP_H
#include "g4-live.h"
#ifdef __cplusplus
extern "C" {
#endif
#define G4L_STOP_BLOCK_RESET 0x45u
struct g4l_stop_ops {
    void *cookie;
    int (*guard)(void *,uint64_t window_cookie);
    int (*read_reg)(void *,uint32_t dword_reg,uint32_t *);
    int (*write_reg)(void *,uint32_t dword_reg,uint32_t value);
    int (*barrier)(void *);
    int (*now_ns)(void *,uint64_t *);
    void (*relax)(void *);
};
/* Explicit request only: enabled0 performs no callbacks. Never runs during
 * read-only SMMU inventory. Requires previously exposed G4 and all4 BO pins.
 * Soft reset pulse -> 4-XIN drain -> A530 VBIF reset -> fresh 4-XIN halt/ACK
 * -> double cold-root checks + CPU cache synchronization. Keeps final halt
 * asserted; parent G1 power collapse resets it. No old command can restart.
 * No retry after ANY attempt/error; raw stop and original bootstrap failure
 * remain independent. complete1 is set only after real callbacks all succeed.
 */
enum g4l_status g4l_hard_stop(struct g4l_context *,const struct g4l_stop_ops *,unsigned enabled);
int g4l_stop_readable(uint32_t);
int g4l_stop_writable(uint32_t,uint32_t value);
int g4l_stop_proof_current(const struct g4l_context *);
#ifdef __cplusplus
}
#endif
#endif
