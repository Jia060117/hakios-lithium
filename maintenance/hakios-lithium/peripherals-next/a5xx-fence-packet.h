/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LITHIUM_A5XX_FENCE_PACKET_H
#define LITHIUM_A5XX_FENCE_PACKET_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Host-testable A5xx CP_EVENT_WRITE/CACHE_FLUSH_TS packet encoder.
 *
 * The caller supplies an already mapped fence GPU IOVA within the active
 * device aperture. This codec checks the Linux msm/adreno fence-slot minimum
 * and 32-bit store alignment; it does not establish mapping, ownership,
 * cache visibility, ring readiness, or any hardware state.
 *
 * Source basis: Linux v6.12 adc218676eef25575469234709c2d87185ca223a,
 * drivers/gpu/drm/msm/adreno/a5xx_gpu.c:219-227; adreno_gpu.h:637-664;
 * registers/adreno/adreno_pm4.xml:14-17,302,1614-1634.
 */
enum { A5XX_FENCE_PACKET_WORDS = 5 };

enum a5xx_fence_packet_result {
    A5XX_FENCE_PACKET_OK = 0,
    A5XX_FENCE_PACKET_INVALID_ARGUMENT,
    A5XX_FENCE_PACKET_INVALID_IOVA,
    A5XX_FENCE_PACKET_NO_SPACE
};

#define A5XX_FENCE_PACKET_MIN_IOVA UINT64_C(0x01000000)
#define A5XX_FENCE_PACKET_TYPE7 UINT32_C(0x70000000)
#define A5XX_FENCE_PACKET_OPCODE_CP_EVENT_WRITE UINT32_C(0x46)
#define A5XX_FENCE_PACKET_EVENT_CACHE_FLUSH_TS UINT32_C(0x04)
#define A5XX_FENCE_PACKET_IRQ UINT32_C(0x80000000)

/* Linux PM4_PARITY: 1 for even parity, 0 for odd parity. */
static inline uint32_t
a5xx_fence_packet_pm4_parity(uint32_t value)
{
    const uint32_t folded = value ^ (value >> 4) ^ (value >> 8) ^
        (value >> 12) ^ (value >> 16) ^ (value >> 20) ^
        (value >> 24) ^ (value >> 28);

    return (UINT32_C(0x9669) >> (folded & UINT32_C(0x0f))) & UINT32_C(1);
}

/*
 * Emit atomically: on any error, out_words is unchanged. capacity is in
 * uint32_t words and must cover the type-7 header plus four payload words.
 * seq is the raw 32-bit fence value; zero is representable and is not
 * reserved by the pinned Linux fence sequence path.
 *
 * The CP_EVENT_WRITE packet has two complete 32-bit address payload words,
 * so encoding itself accepts the full uint64_t wire address. The actual
 * IOMMU aperture is dynamic and must be enforced by the mapping owner.
 */
static inline enum a5xx_fence_packet_result
a5xx_fence_packet_encode(uint64_t fence_gpu_iova, uint32_t seq,
    uint32_t *out_words, size_t capacity)
{
    if (out_words == NULL)
        return A5XX_FENCE_PACKET_INVALID_ARGUMENT;
    if (fence_gpu_iova < A5XX_FENCE_PACKET_MIN_IOVA ||
        (fence_gpu_iova & UINT64_C(3)) != 0)
        return A5XX_FENCE_PACKET_INVALID_IOVA;
    if (capacity < A5XX_FENCE_PACKET_WORDS)
        return A5XX_FENCE_PACKET_NO_SPACE;

    const uint32_t count = UINT32_C(4);
    const uint32_t opcode = A5XX_FENCE_PACKET_OPCODE_CP_EVENT_WRITE;
    const uint32_t staged[A5XX_FENCE_PACKET_WORDS] = {
        A5XX_FENCE_PACKET_TYPE7 | count |
            (a5xx_fence_packet_pm4_parity(count) << 15) |
            (opcode << 16) |
            (a5xx_fence_packet_pm4_parity(opcode) << 23),
        A5XX_FENCE_PACKET_EVENT_CACHE_FLUSH_TS | A5XX_FENCE_PACKET_IRQ,
        (uint32_t)fence_gpu_iova,
        (uint32_t)(fence_gpu_iova >> 32),
        seq
    };

    memcpy(out_words, staged, sizeof(staged));
    return A5XX_FENCE_PACKET_OK;
}

#endif
