/* SPDX-License-Identifier: MIT
 * Short-lived UEFI observation on the boot device tree. Never clock permission.
 * All fields are fixed-width native little-endian in this arm64-only port.
 */
#ifndef LITHIUM_CPU_TSENS_RECEIPT_R38_H
#define LITHIUM_CPU_TSENS_RECEIPT_R38_H
#include <stdint.h>

#define R38_TSENS_RECEIPT_MAGIC UINT64_C(0x31504d4554383352)
#define R38_TSENS_RECEIPT_PROPERTY "lithium-cpu-temp"

struct r38_temp_row {
    uint32_t result, ctrl, trdy, status, status_reads;
};

struct r38_temp_receipt {
    uint64_t magic, generation;
    uint64_t observed_us[2];
    uint32_t phase_mask, target_hz;
    struct r38_temp_row row[2][4];
};
_Static_assert(sizeof(struct r38_temp_receipt) == 200, "thermal receipt layout");
#endif
