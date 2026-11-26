#include "gpu-submit.h"

#include <assert.h>
#include <stdio.h>

#define UNTOUCHED UINT32_C(0xfeedface)
#define FENCE_IOVA UINT64_C(0x0000000100000040)

static void
configure_iova(struct lithium_gpu_mock *mock)
{
    assert(lithium_gpu_mock_configure_iova(mock, FENCE_IOVA, 8, 40));
}

static void
test_geometry_and_capacity(void)
{
    struct lithium_gpu_mock mock = {0};
    uint32_t ring[8] = {UNTOUCHED, UNTOUCHED, UNTOUCHED, UNTOUCHED};
    uint32_t fence = UNTOUCHED;

    assert(!lithium_gpu_mock_release(&mock));
    assert(!lithium_gpu_mock_attach(&mock, UINT32_C(0x05030002), ring, 8));
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, NULL, 8));
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 0));
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 1));
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 7));
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, SIZE_MAX));
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring,
        LITHIUM_GPU_MOCK_MAX_RING_WORDS * 2));
    assert(mock.state == LITHIUM_GPU_MOCK_DETACHED);

    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 4));
    configure_iova(&mock);
    assert(!lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    assert(lithium_gpu_mock_free_words(&mock) == 3);
    assert(lithium_gpu_mock_submit(&mock, 0, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_NO_SPACE);
    assert(fence == UNTOUCHED && mock.state == LITHIUM_GPU_MOCK_IDLE);
    for (size_t i = 0; i < 4; ++i) {
        assert(ring[i] == UNTOUCHED);
    }
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(lithium_gpu_mock_release(&mock) && mock.ring == NULL);
}

static void
test_exact_packet_boundary_and_iova(void)
{
    struct lithium_gpu_mock mock = {0};
    uint32_t ring[8];
    uint32_t fence = UNTOUCHED;
    for (size_t i = 0; i < 8; ++i)
        ring[i] = UNTOUCHED;
    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    mock.read = 2;
    mock.write = 4; /* Exactly five writable words remain in this ring. */
    assert(lithium_gpu_mock_free_words(&mock) == LITHIUM_GPU_MOCK_PACKET_WORDS);
    assert(lithium_gpu_mock_submit(&mock, 0, 10, FENCE_IOVA + 4, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(fence == 1 && mock.write == 1 && mock.read == 2);
    assert(lithium_gpu_mock_free_words(&mock) == 0);
    assert(ring[4] == UINT32_C(0x70460004) &&
        ring[5] == UINT32_C(0x80000004) &&
        ring[6] == (uint32_t)(FENCE_IOVA + 4) && ring[7] == 1 && ring[0] == 1);

    struct lithium_gpu_mock boundary = {0};
    uint32_t boundary_ring[8];
    for (size_t i = 0; i < 8; ++i)
        boundary_ring[i] = UNTOUCHED;
    assert(lithium_gpu_mock_attach(&boundary, LITHIUM_GPU_A530_V5,
        boundary_ring, 8));
    configure_iova(&boundary);
    fence = UNTOUCHED;
    assert(lithium_gpu_mock_submit(&boundary, 0, 10, FENCE_IOVA + 8,
        &fence) == LITHIUM_GPU_MOCK_INVALID);
    assert(lithium_gpu_mock_submit(&boundary, 0, 10, FENCE_IOVA + 1,
        &fence) == LITHIUM_GPU_MOCK_INVALID);
    assert(lithium_gpu_mock_submit(&boundary, 0, 10, FENCE_IOVA - 4,
        &fence) == LITHIUM_GPU_MOCK_INVALID);
    assert(fence == UNTOUCHED && boundary.write == 0 &&
        boundary.state == LITHIUM_GPU_MOCK_IDLE);
    for (size_t i = 0; i < 8; ++i)
        assert(boundary_ring[i] == UNTOUCHED);

    struct lithium_gpu_mock invalid = {0};
    uint32_t invalid_ring[8] = {0};
    assert(lithium_gpu_mock_attach(&invalid, LITHIUM_GPU_A530_V5,
        invalid_ring, 8));
    assert(!lithium_gpu_mock_configure_iova(&invalid, FENCE_IOVA, 0, 40));
    assert(!lithium_gpu_mock_configure_iova(&invalid, UINT64_MAX - 3, 8, 64));
    assert(!lithium_gpu_mock_configure_iova(&invalid, UINT64_C(0xfffffffc),
        8, 32));
}

static void
test_malformed_ring_fails_closed(void)
{
    struct lithium_gpu_mock mock = {0};
    uint32_t ring[8];
    uint32_t fence = UNTOUCHED;
    for (size_t i = 0; i < 8; ++i)
        ring[i] = UNTOUCHED;
    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    mock.write = mock.capacity;
    assert(lithium_gpu_mock_submit(&mock, 0, 10, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_INVALID);
    assert(mock.state == LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK);
    assert(mock.write == mock.capacity && mock.read == 0 &&
        mock.pending_fence == 0 && fence == UNTOUCHED);
    assert(!lithium_gpu_mock_quiesce(&mock, true, true));
    assert(!lithium_gpu_mock_recover(&mock, true, true, true));
    for (size_t i = 0; i < 8; ++i)
        assert(ring[i] == UNTOUCHED);
}

static void
test_packet_wrap_irq_and_poll(void)
{
    struct lithium_gpu_mock mock = {0};
    uint32_t ring[8];
    uint32_t fence;
    const uint32_t first[8] = {
        0x70460004u, 0x80000004u, 0x40u, 1u, 1u,
        UNTOUCHED, UNTOUCHED, UNTOUCHED,
    };
    const uint32_t wrapped[8] = {
        1u, 2u, 0x40u, 1u, 1u, 0x70460004u, 0x80000004u, 0x40u,
    };
    for (size_t i = 0; i < 8; ++i) {
        ring[i] = UNTOUCHED;
    }
    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    assert(lithium_gpu_mock_free_words(&mock) == 7);
    assert(lithium_gpu_mock_submit(&mock, 20, 10, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(fence == 1 && mock.write == 5 && mock.read == 0);
    assert(lithium_gpu_mock_free_words(&mock) == 2);
    assert(lithium_gpu_mock_submit(&mock, 21, 10, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_BUSY);
    for (size_t i = 0; i < 8; ++i) {
        assert(ring[i] == first[i]);
    }
    assert(!lithium_gpu_mock_release(&mock));
    assert(!lithium_gpu_mock_retire(&mock, fence));
    assert(lithium_gpu_mock_sample(&mock, 21, 0, true)); /* Spurious IRQ. */
    assert(lithium_gpu_mock_sample(&mock, 22, 2, true)); /* Future fence. */
    assert(mock.state == LITHIUM_GPU_MOCK_PENDING);
    assert(!lithium_gpu_mock_sample(&mock, 21, fence, true));
    assert(lithium_gpu_mock_sample(&mock, 23, fence, true));
    assert(mock.state == LITHIUM_GPU_MOCK_COMPLETE_IRQ);
    assert(lithium_gpu_mock_free_words(&mock) == 2);
    assert(!lithium_gpu_mock_quiesce(&mock, true, true));
    assert(!lithium_gpu_mock_retire(&mock, 2));
    assert(lithium_gpu_mock_retire(&mock, fence));
    assert(lithium_gpu_mock_free_words(&mock) == 7 && mock.read == 5);
    assert(!lithium_gpu_mock_retire(&mock, fence));
    assert(!lithium_gpu_mock_release(&mock));

    assert(lithium_gpu_mock_submit(&mock, 23, 10, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(fence == 2 && mock.write == 2 && mock.read == 5);
    for (size_t i = 0; i < 8; ++i) {
        assert(ring[i] == wrapped[i]);
    }
    assert(lithium_gpu_mock_sample(&mock, 24, 1, true));
    assert(mock.state == LITHIUM_GPU_MOCK_PENDING);
    assert(lithium_gpu_mock_sample(&mock, 33, fence, false)); /* Lost IRQ. */
    assert(mock.state == LITHIUM_GPU_MOCK_COMPLETE_POLL);
    assert(lithium_gpu_mock_retire(&mock, fence));
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(lithium_gpu_mock_submit(&mock, 34, 10, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_BUSY);
    assert(lithium_gpu_mock_release(&mock));
}

static void
test_watchdog_and_quiescence(void)
{
    struct lithium_gpu_mock mock = {0};
    uint32_t ring[8] = {0};
    uint32_t fence;

    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    assert(lithium_gpu_mock_submit(&mock, 100, 5, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(lithium_gpu_mock_sample(&mock, 104, 0, true));
    assert(mock.state == LITHIUM_GPU_MOCK_PENDING);
    assert(lithium_gpu_mock_sample(&mock, 105, 0, false));
    assert(mock.state == LITHIUM_GPU_MOCK_WATCHDOG);
    assert(!lithium_gpu_mock_sample(&mock, 106, fence, true));
    assert(!lithium_gpu_mock_retire(&mock, fence));
    assert(!lithium_gpu_mock_quiesce(&mock, false, true));
    assert(!lithium_gpu_mock_quiesce(&mock, true, false));
    assert(!lithium_gpu_mock_release(&mock));
    assert(mock.ring == ring && mock.pending_fence == fence && mock.read == 0);
    assert(!lithium_gpu_mock_recover(&mock, false, true, true));
    assert(mock.state == LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK &&
        mock.pending_fence == fence && mock.write == 5);
    assert(lithium_gpu_mock_submit(&mock, 106, 5, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_BUSY);
    assert(!lithium_gpu_mock_recover(&mock, true, true, false));
    assert(mock.state == LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK);
    assert(lithium_gpu_mock_recover(&mock, true, true, true));
    assert(mock.state == LITHIUM_GPU_MOCK_IDLE && mock.read == 0 &&
        mock.write == 0 && mock.pending_fence == 0);
    assert(lithium_gpu_mock_submit(&mock, 107, 5, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(fence == 2);
    assert(lithium_gpu_mock_sample(&mock, 108, fence, false));
    assert(lithium_gpu_mock_retire(&mock, fence));
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(mock.pending_fence == 0 && mock.read == mock.write);
    assert(!lithium_gpu_mock_sample(&mock, 107, fence, true));
    assert(lithium_gpu_mock_release(&mock));

    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    assert(lithium_gpu_mock_submit(&mock, 1, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(fence == 3); /* A stale sample from the previous attachment. */
    assert(lithium_gpu_mock_sample(&mock, 1, 1, true));
    assert(mock.state == LITHIUM_GPU_MOCK_PENDING);
    assert(lithium_gpu_mock_sample(&mock, 3, fence, true));
    assert(mock.state == LITHIUM_GPU_MOCK_WATCHDOG);
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(lithium_gpu_mock_release(&mock));

    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    assert(lithium_gpu_mock_submit(&mock, 1, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(!lithium_gpu_mock_quiesce(&mock, false, true));
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(!lithium_gpu_mock_retire(&mock, fence));
    assert(lithium_gpu_mock_release(&mock));
}

static void
test_time_and_fence_limits(void)
{
    struct lithium_gpu_mock mock = {.last_fence = UINT32_MAX - 1};
    uint32_t fence = UNTOUCHED;
    uint32_t ring[8] = {UNTOUCHED};

    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    assert(lithium_gpu_mock_submit(&mock, 0, 0, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_INVALID);
    assert(lithium_gpu_mock_submit(&mock, 0, 1, FENCE_IOVA, NULL) ==
        LITHIUM_GPU_MOCK_INVALID);
    assert(lithium_gpu_mock_submit(&mock, 0, LITHIUM_GPU_MOCK_MAX_TIMEOUT_US + 1,
        FENCE_IOVA, &fence) == LITHIUM_GPU_MOCK_INVALID);
    assert(lithium_gpu_mock_submit(&mock, UINT64_MAX, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_INVALID);
    assert(ring[0] == UNTOUCHED && fence == UNTOUCHED && mock.write == 0);
    assert(lithium_gpu_mock_submit(&mock, UINT64_MAX - 1, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_OK);
    assert(fence == UINT32_MAX && ring[4] == UINT32_MAX);
    assert(lithium_gpu_mock_sample(&mock, UINT64_MAX, fence, false));
    assert(lithium_gpu_mock_retire(&mock, fence));
    assert(lithium_gpu_mock_submit(&mock, UINT64_MAX - 2, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_INVALID);
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(lithium_gpu_mock_release(&mock));
    assert(lithium_gpu_mock_attach(&mock, LITHIUM_GPU_A530_V5, ring, 8));
    configure_iova(&mock);
    assert(lithium_gpu_mock_submit(&mock, 0, 1, FENCE_IOVA, &fence) ==
        LITHIUM_GPU_MOCK_FENCE_EXHAUSTED);
    assert(mock.state == LITHIUM_GPU_MOCK_IDLE && mock.write == 0);
    assert(lithium_gpu_mock_quiesce(&mock, true, true));
    assert(lithium_gpu_mock_release(&mock));
}

int
main(void)
{
    test_geometry_and_capacity();
    test_exact_packet_boundary_and_iova();
    test_malformed_ring_fails_closed();
    test_packet_wrap_irq_and_poll();
    test_watchdog_and_quiescence();
    test_time_and_fence_limits();
    puts("B4 A530 v5 GPU mock: 6 test groups passed");
    return 0;
}
