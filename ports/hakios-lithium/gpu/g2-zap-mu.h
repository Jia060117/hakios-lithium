/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LITHIUM_GPU_G2_ZAP_MU_H
#define LITHIUM_GPU_G2_ZAP_MU_H
#include "g2-zap.h"

/* Bind only to lg2_ops.smc / sha256. No entry point/INF is installed.
 * Allocation, memory attributes, barriers, serialization, capability/owner
 * receipts and preemptible SCM context remain mandatory platform callbacks.
 * Dependencies: ArmSmcLib, BaseCryptLib; compile against actual pinned Mu.
 */
int lg2_mu_smc(void *, struct lg2_regs *);
int lg2_mu_sha256(void *, const uint8_t *, size_t, uint8_t out[32]);
#endif
