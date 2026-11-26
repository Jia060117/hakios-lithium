#ifndef LITHIUM_PERIPHERAL_GPU_SUBMIT_H
#define LITHIUM_PERIPHERAL_GPU_SUBMIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LITHIUM_GPU_A530_V5 UINT32_C(0x05030004)
#define LITHIUM_GPU_MOCK_MAX_RING_WORDS 8192u
#define LITHIUM_GPU_MOCK_PACKET_WORDS 5u
#define LITHIUM_GPU_MOCK_MAX_TIMEOUT_US UINT64_C(1000000)

enum lithium_gpu_mock_state {
    LITHIUM_GPU_MOCK_DETACHED = 0,
    LITHIUM_GPU_MOCK_IDLE,
    LITHIUM_GPU_MOCK_PENDING,
    LITHIUM_GPU_MOCK_COMPLETE_IRQ,
    LITHIUM_GPU_MOCK_COMPLETE_POLL,
    LITHIUM_GPU_MOCK_WATCHDOG,
    LITHIUM_GPU_MOCK_QUIESCED,
    /* Permanently blocks GPU submission until external recovery is proven. */
    LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK,
};

enum lithium_gpu_mock_result {
    LITHIUM_GPU_MOCK_OK = 0,
    LITHIUM_GPU_MOCK_INVALID,
    LITHIUM_GPU_MOCK_BUSY,
    LITHIUM_GPU_MOCK_NO_SPACE,
    LITHIUM_GPU_MOCK_FENCE_EXHAUSTED,
};

/*
 * Zero-initialize once and serialize all calls. The mock borrows a CPU-only
 * array until release. Observations and quiescence proofs are synthetic.
 */
struct lithium_gpu_mock {
    uint32_t *ring;
    size_t capacity;
    size_t read;
    size_t write;
    uint32_t last_fence;
    uint32_t pending_fence;
    uint64_t last_us;
    uint64_t deadline_us;
    uint64_t iova_base;
    uint64_t iova_last;
    unsigned iova_bits;
    bool iova_configured;
    enum lithium_gpu_mock_state state;
};

static inline bool
lithium_gpu_mock_iova_span(uint64_t base, uint64_t bytes, unsigned bits,
    uint64_t *last)
{
    if (last == NULL || bits == 0 || bits > 64 || bytes == 0 ||
        bytes - 1 > UINT64_MAX - base) {
        return false;
    }
    const uint64_t end = base + bytes - 1;
    if (bits < 64 && (end >> bits) != 0) {
        return false;
    }
    *last = end;
    return true;
}

/* Configure only aperture bounds; this does not prove mapping or ownership. */
static inline bool
lithium_gpu_mock_configure_iova(struct lithium_gpu_mock *mock, uint64_t base,
    uint64_t bytes, unsigned bits)
{
    uint64_t last;
    if (mock->state != LITHIUM_GPU_MOCK_IDLE || mock->iova_configured ||
        mock->read != mock->write ||
        !lithium_gpu_mock_iova_span(base, bytes, bits, &last)) {
        return false;
    }
    mock->iova_base = base;
    mock->iova_last = last;
    mock->iova_bits = bits;
    mock->iova_configured = true;
    return true;
}

static inline bool
lithium_gpu_mock_iova_slot_valid(const struct lithium_gpu_mock *mock,
    uint64_t address)
{
    return mock->iova_configured && (address & UINT64_C(3)) == 0 &&
        address >= mock->iova_base && address <= mock->iova_last &&
        mock->iova_last - address >= sizeof(uint32_t) - 1 &&
        (mock->iova_bits == 64 || (address >> mock->iova_bits) == 0);
}

static inline bool
lithium_gpu_mock_ring_valid(const struct lithium_gpu_mock *mock)
{
    return mock->ring != NULL && mock->capacity >= 2 &&
        mock->capacity <= LITHIUM_GPU_MOCK_MAX_RING_WORDS &&
        (mock->capacity & (mock->capacity - 1)) == 0 &&
        mock->read < mock->capacity && mock->write < mock->capacity;
}

static inline bool
lithium_gpu_mock_attach(struct lithium_gpu_mock *mock, uint32_t chip_id,
    uint32_t *ring, size_t capacity)
{
    if (mock->state != LITHIUM_GPU_MOCK_DETACHED ||
        chip_id != LITHIUM_GPU_A530_V5 || ring == NULL ||
        capacity < 2 || capacity > LITHIUM_GPU_MOCK_MAX_RING_WORDS ||
        (capacity & (capacity - 1)) != 0) {
        return false;
    }
    mock->ring = ring;
    mock->capacity = capacity;
    mock->state = LITHIUM_GPU_MOCK_IDLE;
    return true;
}

static inline size_t
lithium_gpu_mock_free_words(const struct lithium_gpu_mock *mock)
{
    if (mock->state == LITHIUM_GPU_MOCK_DETACHED ||
        mock->state == LITHIUM_GPU_MOCK_QUIESCED ||
        !lithium_gpu_mock_ring_valid(mock)) {
        return 0;
    }
    /* Reserve one word to distinguish full from empty. */
    const size_t mask = mock->capacity - 1;
    const size_t used = (mock->write + mock->capacity - mock->read) & mask;
    return mask - used;
}

static inline enum lithium_gpu_mock_result
lithium_gpu_mock_submit(struct lithium_gpu_mock *mock, uint64_t now_us,
    uint64_t timeout_us, uint64_t fence_iova, uint32_t *fence)
{
    if (mock->state != LITHIUM_GPU_MOCK_IDLE) {
        return LITHIUM_GPU_MOCK_BUSY;
    }
    if (!lithium_gpu_mock_ring_valid(mock)) {
        mock->state = LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK;
        return LITHIUM_GPU_MOCK_INVALID;
    }
    if (fence == NULL || now_us < mock->last_us || timeout_us == 0 ||
        timeout_us > LITHIUM_GPU_MOCK_MAX_TIMEOUT_US ||
        now_us > UINT64_MAX - timeout_us ||
        !lithium_gpu_mock_iova_slot_valid(mock, fence_iova)) {
        return LITHIUM_GPU_MOCK_INVALID;
    }
    if (lithium_gpu_mock_free_words(mock) < LITHIUM_GPU_MOCK_PACKET_WORDS) {
        return LITHIUM_GPU_MOCK_NO_SPACE;
    }
    if (mock->last_fence == UINT32_MAX) {
        return LITHIUM_GPU_MOCK_FENCE_EXHAUSTED;
    }
    const uint32_t next = mock->last_fence + 1;
    /*
     * Linux v6.12 adc218676eef25575469234709c2d87185ca223a,
     * drivers/gpu/drm/msm/adreno/a5xx_gpu.c:219-227 and
     * adreno_gpu.h:637-664. Encode only the
     * CP_EVENT_WRITE/CACHE_FLUSH_TS fence packet with IRQ requested.
     * The address words contain the caller's candidate IOVA. This CPU-only
     * model checks aperture bounds and alignment, but has no backing mapping.
     */
    const uint32_t packet[LITHIUM_GPU_MOCK_PACKET_WORDS] = {
        UINT32_C(0x70460004), UINT32_C(0x80000004),
        (uint32_t)fence_iova, (uint32_t)(fence_iova >> 32),
        next,
    };
    const size_t mask = mock->capacity - 1;
    size_t write = mock->write;
    for (size_t i = 0; i < LITHIUM_GPU_MOCK_PACKET_WORDS; ++i) {
        mock->ring[write] = packet[i];
        write = (write + 1) & mask;
    }
    mock->write = write;
    mock->last_fence = next;
    mock->pending_fence = next;
    mock->last_us = now_us;
    mock->deadline_us = now_us + timeout_us;
    mock->state = LITHIUM_GPU_MOCK_PENDING;
    *fence = next;
    return LITHIUM_GPU_MOCK_OK;
}

/*
 * Consume exactly one sample. The fence word is authoritative and the IRQ
 * only identifies how completion was seen. A match at the deadline counts,
 * but a late sample or an already latched watchdog cannot complete the work.
 */
static inline bool
lithium_gpu_mock_sample(struct lithium_gpu_mock *mock, uint64_t now_us,
    uint32_t fence_word, bool irq)
{
    if (mock->state != LITHIUM_GPU_MOCK_PENDING || now_us < mock->last_us) {
        return false;
    }
    mock->last_us = now_us;
    if (now_us <= mock->deadline_us && fence_word == mock->pending_fence) {
        mock->state = irq ? LITHIUM_GPU_MOCK_COMPLETE_IRQ :
            LITHIUM_GPU_MOCK_COMPLETE_POLL;
    } else if (now_us >= mock->deadline_us) {
        mock->state = LITHIUM_GPU_MOCK_WATCHDOG;
    }
    return true;
}

static inline bool
lithium_gpu_mock_retire(struct lithium_gpu_mock *mock, uint32_t fence)
{
    if ((mock->state != LITHIUM_GPU_MOCK_COMPLETE_IRQ &&
        mock->state != LITHIUM_GPU_MOCK_COMPLETE_POLL) ||
        fence != mock->pending_fence) {
        return false;
    }
    mock->read = mock->write;
    mock->pending_fence = 0;
    mock->deadline_us = 0;
    mock->state = LITHIUM_GPU_MOCK_IDLE;
    return true;
}

/*
 * The mock engine must be idle and its IRQ drained before quiescing.
 * Quiescence can discard pending or timed-out work. Completed work must
 * be retired explicitly. Quiescence closes the ring to further submission.
 */
static inline bool
lithium_gpu_mock_quiesce(struct lithium_gpu_mock *mock, bool engine_idle,
    bool irq_drained)
{
    if (!engine_idle || !irq_drained ||
        !lithium_gpu_mock_ring_valid(mock) ||
        (mock->state != LITHIUM_GPU_MOCK_IDLE &&
        mock->state != LITHIUM_GPU_MOCK_PENDING &&
        mock->state != LITHIUM_GPU_MOCK_WATCHDOG)) {
        return false;
    }
    mock->read = mock->write;
    mock->pending_fence = 0;
    mock->deadline_us = 0;
    mock->state = LITHIUM_GPU_MOCK_QUIESCED;
    return true;
}

/*
 * Reset acknowledgements and idle/IRQ proofs are synthetic test inputs. A
 * failed or incomplete recovery latches software-only fallback and retains
 * the ring; it never certifies that real DMA has stopped.
 */
static inline bool
lithium_gpu_mock_recover(struct lithium_gpu_mock *mock, bool reset_ack,
    bool engine_idle, bool irq_drained)
{
    if (mock->state != LITHIUM_GPU_MOCK_WATCHDOG &&
        mock->state != LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK) {
        return false;
    }
    if (!reset_ack || !engine_idle || !irq_drained ||
        !lithium_gpu_mock_ring_valid(mock)) {
        mock->state = LITHIUM_GPU_MOCK_SOFTWARE_FALLBACK;
        return false;
    }
    /* Treat acknowledged reset as a fresh empty-ring boundary. */
    for (size_t i = 0; i < mock->capacity; ++i) {
        mock->ring[i] = 0;
    }
    mock->read = 0;
    mock->write = 0;
    mock->pending_fence = 0;
    mock->deadline_us = 0;
    mock->state = LITHIUM_GPU_MOCK_IDLE;
    return true;
}

static inline bool
lithium_gpu_mock_release(struct lithium_gpu_mock *mock)
{
    if (mock->state != LITHIUM_GPU_MOCK_QUIESCED) {
        return false;
    }
    /* Keep fence identities unique if this control object is attached again. */
    const uint32_t last_fence = mock->last_fence;
    *mock = (struct lithium_gpu_mock){.last_fence = last_fence};
    return true;
}

#endif
