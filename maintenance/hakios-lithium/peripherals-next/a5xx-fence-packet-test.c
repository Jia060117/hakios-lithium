/* SPDX-License-Identifier: GPL-2.0-only */
#include "a5xx-fence-packet.h"

#include <stdio.h>

#define SENTINEL UINT32_C(0xfeedface)
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/* Independent reference: count set bits directly rather than using the
 * nibble-fold/table expression used by the production encoder. */
static uint32_t
golden_even_parity(uint32_t value)
{
    unsigned ones = 0;
    while (value != 0) {
        ones += value & UINT32_C(1);
        value >>= 1;
    }
    return (ones & 1u) ? UINT32_C(0) : UINT32_C(1);
}

static uint32_t
golden_type7_header(uint32_t opcode, uint32_t payload_words)
{
    return UINT32_C(0x70000000) | payload_words |
        (golden_even_parity(payload_words) << 15) |
        (opcode << 16) | (golden_even_parity(opcode) << 23);
}

static int
expect_packet(uint64_t iova, uint32_t seq, size_t capacity)
{
    uint32_t actual[A5XX_FENCE_PACKET_WORDS + 2] = {
        SENTINEL, SENTINEL, SENTINEL, SENTINEL, SENTINEL, SENTINEL, SENTINEL
    };
    const uint32_t golden[A5XX_FENCE_PACKET_WORDS] = {
        golden_type7_header(UINT32_C(0x46), UINT32_C(4)),
        UINT32_C(0x80000004),
        (uint32_t)iova,
        (uint32_t)(iova >> 32),
        seq
    };

    CHECK(golden[0] == UINT32_C(0x70460004));
    CHECK(a5xx_fence_packet_encode(iova, seq, actual, capacity) ==
        A5XX_FENCE_PACKET_OK);
    for (size_t i = 0; i < A5XX_FENCE_PACKET_WORDS; ++i)
        CHECK(actual[i] == golden[i]);
    for (size_t i = A5XX_FENCE_PACKET_WORDS; i <
        A5XX_FENCE_PACKET_WORDS + 2; ++i)
        CHECK(actual[i] == SENTINEL);
    return 0;
}

static int
test_golden_and_sequence_boundaries(void)
{
    CHECK(expect_packet(UINT64_C(0x123456789abcdef0), UINT32_C(0),
        A5XX_FENCE_PACKET_WORDS) == 0);
    CHECK(expect_packet(A5XX_FENCE_PACKET_MIN_IOVA, UINT32_MAX,
        A5XX_FENCE_PACKET_WORDS + 2) == 0);
    /* The packet carries two address dwords; aperture validation belongs to
     * the caller that owns the IOMMU mapping, not this wire encoder. */
    CHECK(expect_packet(UINT64_C(0xfffffffffffffffc), UINT32_C(0x80000000),
        A5XX_FENCE_PACKET_WORDS) == 0);
    return 0;
}

static int
test_iova_alignment_and_linux_minimum(void)
{
    uint32_t words[A5XX_FENCE_PACKET_WORDS + 1];
    for (size_t i = 0; i < A5XX_FENCE_PACKET_WORDS + 1; ++i)
        words[i] = SENTINEL;

    CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA - 4, 1,
        words, A5XX_FENCE_PACKET_WORDS) == A5XX_FENCE_PACKET_INVALID_IOVA);
    CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA + 1, 1,
        words, A5XX_FENCE_PACKET_WORDS) == A5XX_FENCE_PACKET_INVALID_IOVA);
    CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA + 2, 1,
        words, A5XX_FENCE_PACKET_WORDS) == A5XX_FENCE_PACKET_INVALID_IOVA);
    CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA + 3, 1,
        words, A5XX_FENCE_PACKET_WORDS) == A5XX_FENCE_PACKET_INVALID_IOVA);
    for (size_t i = 0; i < A5XX_FENCE_PACKET_WORDS + 1; ++i)
        CHECK(words[i] == SENTINEL);
    return 0;
}

static int
test_capacity_and_failure_transaction(void)
{
    uint32_t words[A5XX_FENCE_PACKET_WORDS + 1];
    for (size_t i = 0; i < A5XX_FENCE_PACKET_WORDS + 1; ++i)
        words[i] = SENTINEL;

    CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA, 0, NULL,
        A5XX_FENCE_PACKET_WORDS) == A5XX_FENCE_PACKET_INVALID_ARGUMENT);
    for (size_t capacity = 0; capacity < A5XX_FENCE_PACKET_WORDS; ++capacity) {
        CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA, 0, words,
            capacity) == A5XX_FENCE_PACKET_NO_SPACE);
        for (size_t i = 0; i < A5XX_FENCE_PACKET_WORDS + 1; ++i)
            CHECK(words[i] == SENTINEL);
    }
    CHECK(a5xx_fence_packet_encode(A5XX_FENCE_PACKET_MIN_IOVA - 4, 7, words,
        A5XX_FENCE_PACKET_WORDS) == A5XX_FENCE_PACKET_INVALID_IOVA);
    for (size_t i = 0; i < A5XX_FENCE_PACKET_WORDS + 1; ++i)
        CHECK(words[i] == SENTINEL);
    return 0;
}

int
main(void)
{
    CHECK(test_golden_and_sequence_boundaries() == 0);
    CHECK(test_iova_alignment_and_linux_minimum() == 0);
    CHECK(test_capacity_and_failure_transaction() == 0);
    puts("PASS: A5xx CP_EVENT_WRITE fence packet golden/boundary/transaction tests");
    return 0;
}
