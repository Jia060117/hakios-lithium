#include "gpu-iova.h"

#include <assert.h>
#include <stdio.h>

#define OWNER UINT64_C(7)
#define BASE UINT64_C(0x100000)
#define PA_BASE UINT64_C(0x800000)
#define PAGE LITHIUM_GPU_IOVA_PAGE

static struct lithium_gpu_iova_config
config(uint64_t base, uint64_t bytes, unsigned iova_bits, unsigned pa_bits)
{
    return (struct lithium_gpu_iova_config){
        .owner = OWNER, .base = base, .bytes = bytes,
        .iova_bits = iova_bits, .pa_bits = pa_bits,
        .verified = true, .nonsecure = true,
    };
}

static struct lithium_gpu_iova_txn
map_txn(uint64_t iova, uint64_t pa, uint64_t bytes)
{
    return (struct lithium_gpu_iova_txn){
        .op = LITHIUM_GPU_IOVA_MAP, .iova = iova, .bytes = bytes,
        .pa = pa, .backing_verified = true,
    };
}

static struct lithium_gpu_iova_txn
unmap_txn(uint64_t iova, uint64_t bytes)
{
    return (struct lithium_gpu_iova_txn){
        .op = LITHIUM_GPU_IOVA_UNMAP, .iova = iova, .bytes = bytes,
    };
}

static void
test_acquire_and_address_widths(void)
{
    struct lithium_gpu_iova plan = {0};
    struct lithium_gpu_iova_config cfg = config(BASE, PAGE * 8, 32, 36);

    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_OK);
    assert(plan.base == BASE && plan.last == BASE + PAGE * 8 - 1);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_BUSY);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_OK);

    cfg = config(UINT64_C(0xfffffffffffff000), PAGE, 64, 64);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_OK);
    assert(plan.last == UINT64_MAX);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_OK);

    cfg = config(UINT64_C(0xfffffffffffff000), PAGE * 2, 64, 64);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(UINT64_C(0xfffff000), PAGE * 2, 32, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(UINT64_C(0xfffff000), PAGE, 32, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_OK);
    assert(plan.last == UINT32_MAX);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_OK);
    cfg = config(BASE + 1, PAGE, 32, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, PAGE - 1, 32, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, 0, 32, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, PAGE, 11, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, PAGE, 32, 11);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, PAGE, 32, 65);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, PAGE, 65, 36);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg = config(BASE, PAGE, 32, 36);
    cfg.verified = false;
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg.verified = true;
    cfg.nonsecure = false;
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);
    cfg.nonsecure = true;
    cfg.owner = 0;
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_INVALID);

    cfg = config(UINT64_MAX - PAGE + 1, PAGE, 64, 64);
    assert(lithium_gpu_iova_acquire(&plan, cfg) == LITHIUM_GPU_IOVA_OK);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(UINT64_MAX - PAGE + 1, UINT64_MAX - PAGE + 1, PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_OK);
    assert(plan.count == 1 && plan.tlb_count == 1);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_BUSY);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(UINT64_MAX - PAGE + 1, PAGE), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_OK);
    assert(plan.count == 0 && plan.tlb_count == 0);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_OK);
}

static void
test_map_translate_unmap_and_rejections(void)
{
    struct lithium_gpu_iova plan = {0};
    assert(lithium_gpu_iova_acquire(&plan,
        config(BASE, PAGE * 8, 32, 36)) == LITHIUM_GPU_IOVA_OK);

    const struct lithium_gpu_iova_txn two = map_txn(BASE, PA_BASE, PAGE * 2);
    assert(lithium_gpu_iova_apply(&plan, OWNER, two,
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_OK);
    assert(plan.count == 2 && plan.tlb_count == 2 &&
        plan.state == LITHIUM_GPU_IOVA_READY);
    uint64_t pa = 0;
    assert(lithium_gpu_iova_translate(&plan, OWNER, BASE + 123, &pa) ==
        LITHIUM_GPU_IOVA_OK);
    assert(pa == PA_BASE + 123);
    assert(lithium_gpu_iova_translate(&plan, OWNER, BASE + PAGE, &pa) ==
        LITHIUM_GPU_IOVA_OK);
    assert(pa == PA_BASE + PAGE);
    assert(lithium_gpu_iova_translate(&plan, OWNER + 1, BASE, &pa) ==
        LITHIUM_GPU_IOVA_OWNER);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_BUSY);

    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE, PA_BASE + PAGE * 4, PAGE), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_DUPLICATE);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 2, PA_BASE, PAGE), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_DUPLICATE);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 8, PA_BASE + PAGE * 8, PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_INVALID);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + 1, PA_BASE + PAGE * 4, PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_INVALID);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 4, PA_BASE + 1, PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_INVALID);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 4, UINT64_C(0xfffffff000), PAGE * 2),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_INVALID);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 4, UINT64_C(0x1000000000), PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_INVALID);
    struct lithium_gpu_iova_txn unverified = two;
    unverified.backing_verified = false;
    assert(lithium_gpu_iova_apply(&plan, OWNER, unverified,
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_INVALID);

    assert(lithium_gpu_iova_apply(&plan, OWNER, two,
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_DUPLICATE);
    assert(lithium_gpu_iova_apply(&plan, OWNER + 1, two,
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_OWNER);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(BASE, PAGE * 3), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_MISSING);
    assert(plan.count == 2);

    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(BASE, PAGE * 2), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_OK);
    assert(plan.count == 0 && plan.tlb_count == 0);
    assert(lithium_gpu_iova_translate(&plan, OWNER, BASE, &pa) ==
        LITHIUM_GPU_IOVA_MISSING);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(BASE, PAGE), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_MISSING);
    assert(lithium_gpu_iova_release(&plan, OWNER) == LITHIUM_GPU_IOVA_OK);
}

static void
test_atomicity_capacity_and_tlb_failure(void)
{
    struct lithium_gpu_iova plan = {0};
    assert(lithium_gpu_iova_acquire(&plan,
        config(BASE, PAGE * LITHIUM_GPU_IOVA_MAX_PAGES * 2, 32, 36)) ==
        LITHIUM_GPU_IOVA_OK);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE, PA_BASE + PAGE, PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_OK);

    /* The second page collides: the first free page must stay unmapped. */
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE, PA_BASE, PAGE * 2), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_DUPLICATE);
    assert(plan.count == 1);
    uint64_t pa = 0;
    assert(lithium_gpu_iova_translate(&plan, OWNER, BASE, &pa) ==
        LITHIUM_GPU_IOVA_MISSING);

    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 2, PA_BASE + PAGE * 2,
            PAGE * (LITHIUM_GPU_IOVA_MAX_PAGES - 1)),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_OK);
    assert(plan.count == LITHIUM_GPU_IOVA_MAX_PAGES);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        map_txn(BASE + PAGE * 65, PA_BASE + PAGE * 65, PAGE),
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_FULL);
    assert(plan.count == LITHIUM_GPU_IOVA_MAX_PAGES);

    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(BASE + PAGE, PAGE), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_OK);
    assert(plan.count == LITHIUM_GPU_IOVA_MAX_PAGES - 1);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(BASE, PAGE * 2), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_MISSING);
    assert(plan.count == LITHIUM_GPU_IOVA_MAX_PAGES - 1);

    /* Map timeout quarantines the attempted backing without committing pages. */
    const struct lithium_gpu_iova_txn failed_map =
        map_txn(BASE + PAGE * 65, PA_BASE + PAGE * 65, PAGE);
    assert(lithium_gpu_iova_apply(&plan, OWNER, failed_map,
        LITHIUM_GPU_IOVA_SYNC_TIMEOUT) == LITHIUM_GPU_IOVA_TLB_TIMEOUT);
    assert(plan.state == LITHIUM_GPU_IOVA_FAILED &&
        plan.count == LITHIUM_GPU_IOVA_MAX_PAGES - 1);
    assert(plan.tlb_count == LITHIUM_GPU_IOVA_MAX_PAGES - 1);
    assert(plan.failed_txn.iova == failed_map.iova &&
        plan.failed_txn.pa == failed_map.pa &&
        plan.failed_txn.backing_verified);
    assert(plan.tlb_pages[0].iova == BASE + PAGE * 2 &&
        plan.tlb_pages[0].pa == PA_BASE + PAGE * 2);
    assert(lithium_gpu_iova_translate(&plan, OWNER, BASE + PAGE, &pa) ==
        LITHIUM_GPU_IOVA_QUARANTINED);
    assert(lithium_gpu_iova_release(&plan, OWNER) ==
        LITHIUM_GPU_IOVA_QUARANTINED);
    assert(lithium_gpu_iova_apply(&plan, OWNER,
        unmap_txn(BASE + PAGE, PAGE), LITHIUM_GPU_IOVA_SYNC_OK) ==
        LITHIUM_GPU_IOVA_QUARANTINED);

    /* An unmap timeout retains committed mappings and backing ownership. */
    plan = (struct lithium_gpu_iova){0};
    assert(lithium_gpu_iova_acquire(&plan,
        config(BASE, PAGE * 8, 32, 36)) == LITHIUM_GPU_IOVA_OK);
    const struct lithium_gpu_iova_txn mapped = map_txn(BASE, PA_BASE, PAGE * 2);
    assert(lithium_gpu_iova_apply(&plan, OWNER, mapped,
        LITHIUM_GPU_IOVA_SYNC_OK) == LITHIUM_GPU_IOVA_OK);
    const struct lithium_gpu_iova_txn failed_unmap = unmap_txn(BASE, PAGE * 2);
    assert(lithium_gpu_iova_apply(&plan, OWNER, failed_unmap,
        LITHIUM_GPU_IOVA_SYNC_TIMEOUT) == LITHIUM_GPU_IOVA_TLB_TIMEOUT);
    assert(plan.state == LITHIUM_GPU_IOVA_FAILED && plan.count == 2);
    assert(plan.tlb_count == 2);
    assert(plan.failed_txn.op == LITHIUM_GPU_IOVA_UNMAP &&
        plan.failed_txn.iova == BASE && plan.failed_txn.bytes == PAGE * 2);
    assert(lithium_gpu_iova_translate(&plan, OWNER, BASE, &pa) ==
        LITHIUM_GPU_IOVA_QUARANTINED);
    assert(lithium_gpu_iova_release(&plan, OWNER) ==
        LITHIUM_GPU_IOVA_QUARANTINED);
}

int
main(void)
{
    test_acquire_and_address_widths();
    test_map_translate_unmap_and_rejections();
    test_atomicity_capacity_and_tlb_failure();
    puts("gpu-iova: 3 test groups passed");
    return 0;
}
