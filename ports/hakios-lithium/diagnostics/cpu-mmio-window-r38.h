/* SPDX-License-Identifier: MIT
 * Describe the complete allocation owned by one MMIO mapping.
 * Hardware register addresses remain data_base + the register offset.
 * Only allocation_base/mapped_bytes may be passed to ml_io_unmap.
 */
#ifndef LITHIUM_CPU_MMIO_WINDOW_R38_H
#define LITHIUM_CPU_MMIO_WINDOW_R38_H
#include <stdint.h>

struct r38_mmio_window {
    uint64_t physical_base;
    uint64_t mapped_bytes;
    uint64_t data_offset;
};

static inline int
r38_mmio_window_init(uint64_t physical, uint64_t requested_bytes,
    uint64_t page_bytes, struct r38_mmio_window *out)
{
    if (!out) return 0;
    out->physical_base = out->mapped_bytes = out->data_offset = 0;
    if (!requested_bytes || !page_bytes || (page_bytes & (page_bytes - 1)))
        return 0;
    const uint64_t maximum = ~(uint64_t)0, mask = page_bytes - 1;
    if (physical > maximum - requested_bytes) return 0;
    const uint64_t offset = physical & mask;
    if (requested_bytes > maximum - offset ||
        requested_bytes + offset > maximum - mask)
        return 0;
    const uint64_t extent = (requested_bytes + offset + mask) & ~mask;
    const uint64_t base = physical & ~mask;
    if (!extent || base > maximum - extent) return 0;
    out->physical_base = base;
    out->mapped_bytes = extent;
    out->data_offset = offset;
    return 1;
}
#endif
