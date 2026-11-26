/* SPDX-License-Identifier: GPL-2.0-only -- main revision15 integration seam. */
#ifndef LITHIUM_G1_POWER_CLOCK_ADAPTER_INC
#define LITHIUM_G1_POWER_CLOCK_ADAPTER_INC
#include "combined-adapter.inc"
static __attribute__((noinline)) void lithium_gpu_g1_power_clock_probe(void)
{ lithium_gpu_g1_combined_probe(); }
#endif
