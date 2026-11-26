#ifndef LITHIUM_PERIPHERAL_GPU_IOVA_H
#define LITHIUM_PERIPHERAL_GPU_IOVA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LITHIUM_GPU_IOVA_PAGE UINT64_C(4096)
#define LITHIUM_GPU_IOVA_MASK (LITHIUM_GPU_IOVA_PAGE - 1)
#define LITHIUM_GPU_IOVA_MAX_PAGES 64u

enum lithium_gpu_iova_state {
    LITHIUM_GPU_IOVA_DETACHED = 0,
    LITHIUM_GPU_IOVA_READY,
    LITHIUM_GPU_IOVA_FAILED,
};

enum lithium_gpu_iova_result {
    LITHIUM_GPU_IOVA_OK = 0,
    LITHIUM_GPU_IOVA_INVALID,
    LITHIUM_GPU_IOVA_OWNER,
    LITHIUM_GPU_IOVA_DUPLICATE,
    LITHIUM_GPU_IOVA_MISSING,
    LITHIUM_GPU_IOVA_FULL,
    LITHIUM_GPU_IOVA_BUSY,
    LITHIUM_GPU_IOVA_TLB_TIMEOUT,
    LITHIUM_GPU_IOVA_QUARANTINED,
};

enum lithium_gpu_iova_op {
    LITHIUM_GPU_IOVA_MAP,
    LITHIUM_GPU_IOVA_UNMAP,
};

enum lithium_gpu_iova_mock_tlb {
    LITHIUM_GPU_IOVA_SYNC_OK,
    LITHIUM_GPU_IOVA_SYNC_TIMEOUT,
};

/* The caller verifies the aperture, its owner, and both address widths. */
struct lithium_gpu_iova_config {
    uint64_t owner;
    uint64_t base;
    uint64_t bytes;
    unsigned iova_bits;
    unsigned pa_bits;
    bool verified;
    bool nonsecure;
};

struct lithium_gpu_iova_txn {
    enum lithium_gpu_iova_op op;
    uint64_t iova;
    uint64_t bytes;
    /* MAP requires a contiguous PA span verified as nonsecure and owned
     * by the caller. UNMAP leaves both backing fields zero. */
    uint64_t pa;
    bool backing_verified;
};

struct lithium_gpu_iova_page {
    uint64_t iova;
    uint64_t pa;
};

/*
 * CPU-only model. Zero-initialize and serialize calls. The sparse table
 * limits mapped pages, independent of the aperture size. IOVA and PA
 * aliases are rejected. A successful map holds its backing until a
 * successful unmap. A timeout holds both the committed table and the
 * failed transaction in quarantine for the lifetime of this planner.
 */
struct lithium_gpu_iova {
    enum lithium_gpu_iova_state state;
    uint64_t owner;
    uint64_t base;
    uint64_t last;
    unsigned iova_bits;
    unsigned pa_bits;
    size_t count;
    struct lithium_gpu_iova_page pages[LITHIUM_GPU_IOVA_MAX_PAGES];
    size_t tlb_count;
    struct lithium_gpu_iova_page tlb_pages[LITHIUM_GPU_IOVA_MAX_PAGES];
    struct lithium_gpu_iova_txn failed_txn;
};

static inline bool
lithium_gpu_iova_span(uint64_t base, uint64_t bytes, unsigned bits,
    uint64_t *last)
{
    if (last == NULL || bits < 12 || bits > 64 || bytes == 0 ||
        ((base | bytes) & LITHIUM_GPU_IOVA_MASK) != 0 ||
        bytes - 1 > UINT64_MAX - base) {
        return false;
    }
    const uint64_t end = base + (bytes - 1);
    if (bits < 64 && (end >> bits) != 0) {
        return false;
    }
    *last = end;
    return true;
}

static inline enum lithium_gpu_iova_result
lithium_gpu_iova_acquire(struct lithium_gpu_iova *plan,
    struct lithium_gpu_iova_config cfg)
{
    uint64_t last;
    if (plan == NULL) {
        return LITHIUM_GPU_IOVA_INVALID;
    }
    if (plan->state != LITHIUM_GPU_IOVA_DETACHED) {
        return LITHIUM_GPU_IOVA_BUSY;
    }
    if (!cfg.verified || !cfg.nonsecure || cfg.owner == 0 ||
        cfg.pa_bits < 12 || cfg.pa_bits > 64 ||
        !lithium_gpu_iova_span(cfg.base, cfg.bytes, cfg.iova_bits, &last)) {
        return LITHIUM_GPU_IOVA_INVALID;
    }
    *plan = (struct lithium_gpu_iova){
        .state = LITHIUM_GPU_IOVA_READY, .owner = cfg.owner,
        .base = cfg.base, .last = last,
        .iova_bits = cfg.iova_bits, .pa_bits = cfg.pa_bits,
    };
    return LITHIUM_GPU_IOVA_OK;
}

static inline enum lithium_gpu_iova_result
lithium_gpu_iova_access(const struct lithium_gpu_iova *plan, uint64_t owner)
{
    if (plan == NULL || plan->state == LITHIUM_GPU_IOVA_DETACHED) {
        return LITHIUM_GPU_IOVA_INVALID;
    }
    if (owner != plan->owner) {
        return LITHIUM_GPU_IOVA_OWNER;
    }
    if (plan->state == LITHIUM_GPU_IOVA_FAILED ||
        plan->count > LITHIUM_GPU_IOVA_MAX_PAGES ||
        plan->tlb_count > LITHIUM_GPU_IOVA_MAX_PAGES ||
        plan->count != plan->tlb_count) {
        return LITHIUM_GPU_IOVA_QUARANTINED;
    }
    return plan->state == LITHIUM_GPU_IOVA_READY ?
        LITHIUM_GPU_IOVA_OK : LITHIUM_GPU_IOVA_INVALID;
}

static inline enum lithium_gpu_iova_result
lithium_gpu_iova_apply(struct lithium_gpu_iova *plan, uint64_t owner,
    struct lithium_gpu_iova_txn txn, enum lithium_gpu_iova_mock_tlb tlb)
{
    const enum lithium_gpu_iova_result access = lithium_gpu_iova_access(plan,
        owner);
    if (access != LITHIUM_GPU_IOVA_OK) {
        return access;
    }

    const bool map = txn.op == LITHIUM_GPU_IOVA_MAP;
    uint64_t last, pa_last = 0;
    if ((!map && txn.op != LITHIUM_GPU_IOVA_UNMAP) ||
        (tlb != LITHIUM_GPU_IOVA_SYNC_OK &&
         tlb != LITHIUM_GPU_IOVA_SYNC_TIMEOUT) ||
        !lithium_gpu_iova_span(txn.iova, txn.bytes, plan->iova_bits, &last) ||
        txn.iova < plan->base || last > plan->last) {
        return LITHIUM_GPU_IOVA_INVALID;
    }
    if (map) {
        if (!txn.backing_verified ||
            !lithium_gpu_iova_span(txn.pa, txn.bytes, plan->pa_bits, &pa_last)) {
            return LITHIUM_GPU_IOVA_INVALID;
        }
    } else if (txn.pa != 0 || txn.backing_verified) {
        return LITHIUM_GPU_IOVA_INVALID;
    }

    size_t matched = 0;
    for (size_t i = 0; i < plan->count; ++i) {
        const struct lithium_gpu_iova_page page = plan->pages[i];
        const bool in_range = txn.iova <= page.iova && page.iova <= last;
        if (map && (in_range || (txn.pa <= page.pa && page.pa <= pa_last))) {
            return LITHIUM_GPU_IOVA_DUPLICATE;
        }
        if (in_range) {
            ++matched;
        }
    }
    const uint64_t count = txn.bytes / LITHIUM_GPU_IOVA_PAGE;
    if (map && count > LITHIUM_GPU_IOVA_MAX_PAGES - plan->count) {
        return LITHIUM_GPU_IOVA_FULL;
    }
    if (!map && count != matched) {
        return LITHIUM_GPU_IOVA_MISSING;
    }

    /* The synthetic invalidation and sync cover the entire transaction. */
    if (tlb == LITHIUM_GPU_IOVA_SYNC_TIMEOUT) {
        plan->failed_txn = txn;
        plan->state = LITHIUM_GPU_IOVA_FAILED;
        return LITHIUM_GPU_IOVA_TLB_TIMEOUT;
    }

    if (map) {
        for (uint64_t offset = 0; offset < txn.bytes;
             offset += LITHIUM_GPU_IOVA_PAGE) {
            const struct lithium_gpu_iova_page page = {
                .iova = txn.iova + offset, .pa = txn.pa + offset,
            };
            plan->pages[plan->count++] = page;
            plan->tlb_pages[plan->tlb_count++] = page;
        }
    } else {
        size_t kept = 0;
        for (size_t i = 0; i < plan->count; ++i) {
            const struct lithium_gpu_iova_page page = plan->pages[i];
            if (page.iova < txn.iova || page.iova > last) {
                plan->pages[kept++] = page;
            }
        }
        for (size_t i = kept; i < plan->count; ++i) {
            plan->pages[i] = (struct lithium_gpu_iova_page){0};
        }
        plan->count = kept;

        kept = 0;
        for (size_t i = 0; i < plan->tlb_count; ++i) {
            const struct lithium_gpu_iova_page page = plan->tlb_pages[i];
            if (page.iova < txn.iova || page.iova > last) {
                plan->tlb_pages[kept++] = page;
            }
        }
        for (size_t i = kept; i < plan->tlb_count; ++i) {
            plan->tlb_pages[i] = (struct lithium_gpu_iova_page){0};
        }
        plan->tlb_count = kept;
    }
    return LITHIUM_GPU_IOVA_OK;
}

/* Translate through the mock TLB view, including page offset. */
static inline enum lithium_gpu_iova_result
lithium_gpu_iova_translate(const struct lithium_gpu_iova *plan, uint64_t owner,
    uint64_t iova, uint64_t *pa)
{
    const enum lithium_gpu_iova_result access = lithium_gpu_iova_access(plan,
        owner);
    if (access != LITHIUM_GPU_IOVA_OK) {
        return access;
    }
    if (pa == NULL || iova < plan->base || iova > plan->last) {
        return LITHIUM_GPU_IOVA_INVALID;
    }
    for (size_t i = 0; i < plan->tlb_count; ++i) {
        if (plan->tlb_pages[i].iova == (iova & ~LITHIUM_GPU_IOVA_MASK)) {
            *pa = plan->tlb_pages[i].pa + (iova & LITHIUM_GPU_IOVA_MASK);
            return LITHIUM_GPU_IOVA_OK;
        }
    }
    return LITHIUM_GPU_IOVA_MISSING;
}

static inline enum lithium_gpu_iova_result
lithium_gpu_iova_release(struct lithium_gpu_iova *plan, uint64_t owner)
{
    const enum lithium_gpu_iova_result access = lithium_gpu_iova_access(plan,
        owner);
    if (access != LITHIUM_GPU_IOVA_OK) {
        return access;
    }
    if (plan->count != 0) {
        return LITHIUM_GPU_IOVA_BUSY;
    }
    *plan = (struct lithium_gpu_iova){0};
    return LITHIUM_GPU_IOVA_OK;
}

#endif
