/* SPDX-License-Identifier: MIT
 * Default-off RPM message-RAM evidence only. Include after the existing
 * G0/R38 CPU, PAGE_SIZE mapping and LG1 MMIO primitives.
 * Compile rpm-host.c as a separate TU (see owned rpm-read/README.md).
 * No OPEN, FIFO payload, index reset, doorbell, PMIC or GPU write is called.
 * Software deadlines cannot interrupt a bus access that never returns.
 */
#ifndef LITHIUM_GPU_RPM_READ_ADAPTER_INC
#define LITHIUM_GPU_RPM_READ_ADAPTER_INC
#include "rpm-host.h"

#define LGRR_RAM_PA UINT64_C(0x68000)
#define LGRR_RAM_SIZE 0x6000u
#define LGRR_TOC_OFFSET 0x5f00u
#define LGRR_READ_CAP 256u
#define LGRR_TIME_US UINT64_C(20000)
#define LGRR_CPU_HZ UINT64_C(614400000)
#define LGRR_TOC_ALL UINT64_MAX
struct lgrr_result {
    int capture_rc, discover_rc, adapter_error;
    unsigned reads, write_attempts, orders, wait_attempts, rejected_reads;
    unsigned map_attempts, mapped, unmaps, layout_valid, host_discovered;
    uint32_t toc[2][64], index[2][4];
    uint64_t toc_attempted[2], toc_loaded[2], toc_valid[2], toc_stable;
    unsigned index_attempted[2], index_loaded[2], index_valid[2];
    unsigned index_in_range[2], index_stable;
    uint32_t tx_off, tx_size, rx_off, rx_size, tx_pos, rx_pos;
    uint64_t start_us, last_us, elapsed_us, map_base, map_bytes, data_offset;
    struct rpm_host host; /* historical state; callbacks cleared before return */
};
struct lgrr_context {
    struct lgrr_result *result;
    struct r38_mmio_window window;
    vm_offset_t allocation;
};
static struct lgrr_result lgrr_retained;
static unsigned lgrr_once;

static uint64_t lgrr_clock_raw(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000u;
}
static int lgrr_error(struct lgrr_context *c, int error)
{
    if (!c->result->adapter_error) c->result->adapter_error = error;
    return c->result->adapter_error;
}
static uint64_t lgrr_now(void *opaque)
{
    struct lgrr_context *c = opaque;
    struct lgrr_result *r = c->result;
    uint64_t now = lgrr_clock_raw();
    if (now < r->last_us || now - r->start_us >= LGRR_TIME_US)
        (void)lgrr_error(c, RPM_HOST_ETIME);
    if (now >= r->last_us) r->last_us = now;
    return now;
}
static int lgrr_gate(struct lgrr_context *c)
{
    if (!c || !c->result) return RPM_HOST_EINVAL;
    if (c->result->adapter_error) return c->result->adapter_error;
    (void)lgrr_now(c);
    if (cpu_number() != 0 || LITHIUM_PERIPHERAL_AFFINITY() != 1 ||
        !ml_get_interrupts_enabled())
        return lgrr_error(c, RPM_HOST_EOWNERSHIP);
    return c->result->adapter_error;
}
/* Match the proven discover validator; descriptor offsets include 8 bytes,
 * data lengths exclude them. TOC is never a FIFO descriptor or payload.
 */
static int lgrr_layout(const struct lgrr_result *r)
{
    const struct rpm_host_fifo *tx = &r->host.tx, *rx = &r->host.rx;
    if (r->toc_valid[0] != LGRR_TOC_ALL || r->toc_valid[1] != LGRR_TOC_ALL)
        return 0;
    for (unsigned i = 0; i < 64; ++i)
        if (r->toc[0][i] != r->toc[1][i]) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        const struct rpm_host_fifo *f = i ? rx : tx;
        if ((f->off & 3u) || f->off > LGRR_TOC_OFFSET - 8u ||
            f->size < 64u || (f->size & 7u) ||
            f->size > LGRR_TOC_OFFSET - 8u - f->off) return 0;
    }
    return tx->off + 8u + tx->size <= rx->off ||
        rx->off + 8u + rx->size <= tx->off;
}
static int lgrr_read32(void *opaque, uint64_t pa, uint32_t *out)
{
    struct lgrr_context *c = opaque;
    if (!c || !c->result || !out) return RPM_HOST_EINVAL;
    struct lgrr_result *r = c->result;
    int rc = lgrr_gate(c);
    if (rc) return rc;
    if (!c->allocation || (pa & 3u) || pa < LGRR_RAM_PA ||
        pa - LGRR_RAM_PA > LGRR_RAM_SIZE - 4u) {
        ++r->rejected_reads;
        return lgrr_error(c, RPM_HOST_EBOUNDS);
    }
    if (r->reads >= LGRR_READ_CAP) return lgrr_error(c, RPM_HOST_EBUDGET);
    uint32_t off = (uint32_t)(pa - LGRR_RAM_PA);
    unsigned toc_word = 64, toc_pass = 0;
    if (off >= LGRR_TOC_OFFSET) {
        toc_word = (off - LGRR_TOC_OFFSET) / 4u;
        uint64_t bit = UINT64_C(1) << toc_word;
        toc_pass = !!(r->toc_attempted[0] & bit);
        if (r->toc_attempted[toc_pass] & bit) {
            ++r->rejected_reads;
            return lgrr_error(c, RPM_HOST_ESTATE);
        }
        r->toc_attempted[toc_pass] |= bit;
    } else {
        const struct rpm_host *s = &r->host;
        if (!lgrr_layout(r) ||
            (off != s->tx.off && off != s->tx.off + 4u &&
             off != s->rx.off && off != s->rx.off + 4u)) {
            ++r->rejected_reads;
            return lgrr_error(c, RPM_HOST_EBOUNDS);
        }
    }
    vm_offset_t pointer = c->allocation + (vm_offset_t)c->window.data_offset +
        (vm_offset_t)off;
    ++r->reads; /* physical access attempted, including a late returning load */
    LG1_BARRIER();
    *out = LG1_LOAD32(pointer);
    LG1_BARRIER();
    if (toc_word < 64u) {
        r->toc[toc_pass][toc_word] = *out;
        r->toc_loaded[toc_pass] |= UINT64_C(1) << toc_word;
    }
    rc = lgrr_gate(c); /* never mark a late returning word valid */
    if (!rc && toc_word < 64u)
        r->toc_valid[toc_pass] |= UINT64_C(1) << toc_word;
    return rc;
}
static int lgrr_deny_write(void *opaque, uint64_t pa, uint32_t value)
{
    struct lgrr_context *c = opaque;
    (void)pa; (void)value;
    if (!c || !c->result) return RPM_HOST_EINVAL;
    ++c->result->write_attempts;
    return lgrr_error(c, RPM_HOST_EIO); /* no pointer, store, FIFO or doorbell */
}
static int lgrr_order(void *opaque, enum rpm_host_order order)
{
    struct lgrr_context *c = opaque;
    ++c->result->orders;
    if (order != RPM_HOST_ACQUIRE) return lgrr_error(c, RPM_HOST_ESTATE);
    int rc = lgrr_gate(c);
    if (rc) return rc;
    LG1_BARRIER();
    return lgrr_gate(c);
}
static int lgrr_deny_wait(void *opaque, uint32_t us)
{
    struct lgrr_context *c = opaque;
    (void)us;
    ++c->result->wait_attempts;
    return lgrr_error(c, RPM_HOST_ESTATE); /* discover does not wait or retry */
}
static void lgrr_capture(struct lgrr_context *c)
{
    struct lgrr_result *r = c->result;
    r->start_us = r->last_us = lgrr_clock_raw();
    r->discover_rc = RPM_HOST_ESTATE;
    if (rpm_host_msm8996.ram_pa != LGRR_RAM_PA ||
        rpm_host_msm8996.ram_size != LGRR_RAM_SIZE) {
        (void)lgrr_error(c, RPM_HOST_EBOUNDS); goto cleanup;
    }
    if (lgrr_gate(c)) goto cleanup;
    if (!r38_mmio_window_init(LGRR_RAM_PA, LGRR_RAM_SIZE, PAGE_SIZE, &c->window)) {
        (void)lgrr_error(c, RPM_HOST_EBOUNDS); goto cleanup;
    }
    r->map_base = c->window.physical_base; r->map_bytes = c->window.mapped_bytes;
    r->data_offset = c->window.data_offset;
    ++r->map_attempts;
    c->allocation = ml_io_map_unmappable(c->window.physical_base,
        c->window.mapped_bytes, VM_WIMG_IO);
    if (!c->allocation) { (void)lgrr_error(c, RPM_HOST_EIO); goto cleanup; }
    r->mapped = 1;
    if (lgrr_gate(c)) goto cleanup;
    const struct rpm_host_io io = {
        c, lgrr_read32, lgrr_deny_write, lgrr_order, lgrr_now, lgrr_deny_wait};
    int rc = rpm_host_init(&r->host, &io);
    if (rc) { (void)lgrr_error(c, rc); goto cleanup; }
    const struct rpm_host_limits limits = {LGRR_READ_CAP, 16, (uint32_t)LGRR_TIME_US};
    r->discover_rc = rpm_host_discover(&r->host, limits);
    r->host_discovered = r->host.discovered;
    /* A protocol mismatch can stop pass2 early. Finish only unread TOC slots,
     * once each, under the SAME cap/deadline; never retry a failed access.
     * Fatal callback/time errors suppress all remaining reads.
     */
    for (unsigned pass = 0; pass < 2 && !r->adapter_error; ++pass)
        for (unsigned i = 0; i < 64 && !r->adapter_error; ++i)
            if (!(r->toc_attempted[pass] & (UINT64_C(1) << i))) {
                uint32_t raw = 0;
                (void)lgrr_read32(c, LGRR_RAM_PA + LGRR_TOC_OFFSET + 4u * i, &raw);
            }
    for (unsigned i = 0; i < 64; ++i)
        if ((r->toc_valid[0] & r->toc_valid[1] & (UINT64_C(1) << i)) &&
            r->toc[0][i] == r->toc[1][i]) r->toc_stable |= UINT64_C(1) << i;
    r->layout_valid = lgrr_layout(r);
    if (r->layout_valid && !r->adapter_error) {
        r->tx_off = r->host.tx.off; r->tx_size = r->host.tx.size;
        r->rx_off = r->host.rx.off; r->rx_size = r->host.rx.size;
        r->tx_pos = r->host.tx.pos; r->rx_pos = r->host.rx.pos;
        const uint32_t offsets[4] = {r->tx_off, r->tx_off + 4u, r->rx_off, r->rx_off + 4u};
        for (unsigned pass = 0; pass < 2 && !r->adapter_error; ++pass) {
            LG1_BARRIER();
            for (unsigned i = 0; i < 4 && !r->adapter_error; ++i) {
                unsigned before = r->reads;
                r->index_attempted[pass] |= 1u << i;
                int index_rc = lgrr_read32(c, LGRR_RAM_PA + offsets[i], &r->index[pass][i]);
                if (r->reads > before) r->index_loaded[pass] |= 1u << i;
                if (!index_rc) {
                    r->index_valid[pass] |= 1u << i;
                    uint32_t size = i < 2 ? r->tx_size : r->rx_size;
                    if (r->index[pass][i] < size && !(r->index[pass][i] & 7u))
                        r->index_in_range[pass] |= 1u << i;
                }
            }
        }
        for (unsigned i = 0; i < 4; ++i)
            if ((r->index_valid[0] & r->index_valid[1] & (1u << i)) &&
                r->index[0][i] == r->index[1][i]) r->index_stable |= 1u << i;
    }
    (void)lgrr_gate(c);
cleanup:
    r->capture_rc = r->adapter_error ? r->adapter_error : r->discover_rc;
    r->elapsed_us = r->last_us - r->start_us;
    if (c->allocation) {
        ml_io_unmap(c->allocation, c->window.mapped_bytes);
        c->allocation = 0; r->mapped = 0; ++r->unmaps;
    }
    /* Evidence cannot be reused as a live transport/ownership handle. */
    r->host.io = (struct rpm_host_io){0};
    r->host.initialized = false;
}
static void lgrr_log(const struct lgrr_result *r)
{
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < 64; ++i)
            printf("LITHIUM GPU rpm toc pass=%u word=%u offset=0x%x raw=0x%x attempted=%u loaded=%u valid=%u\n",
                pass, i, LGRR_TOC_OFFSET + 4u * i, r->toc[pass][i],
                (unsigned)((r->toc_attempted[pass] >> i) & 1u),
                (unsigned)((r->toc_loaded[pass] >> i) & 1u),
                (unsigned)((r->toc_valid[pass] >> i) & 1u));
    printf("LITHIUM GPU rpm layout valid=%u tx_off=0x%x tx_size=0x%x rx_off=0x%x rx_size=0x%x tx_pos=0x%x rx_pos=0x%x toc_valid0=0x%llx toc_valid1=0x%llx toc_stable=0x%llx\n",
        r->layout_valid, r->tx_off, r->tx_size, r->rx_off, r->rx_size, r->tx_pos, r->rx_pos,
        (unsigned long long)r->toc_valid[0], (unsigned long long)r->toc_valid[1],
        (unsigned long long)r->toc_stable);
    if (r->layout_valid)
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < 4; ++i) {
                uint32_t off = (i < 2 ? r->tx_off : r->rx_off) + (i & 1u) * 4u;
                printf("LITHIUM GPU rpm index pass=%u index=%u offset=0x%x raw=0x%x loaded=%u valid=%u in_range=%u stable=%u\n",
                    pass, i, off, r->index[pass][i],
                    (r->index_loaded[pass] >> i) & 1u, (r->index_valid[pass] >> i) & 1u,
                    (r->index_in_range[pass] >> i) & 1u, (r->index_stable >> i) & 1u);
            }
    printf("LITHIUM GPU rpm end rc=%d discover_rc=%d adapter_error=%d discovered=%u layout=%u reads=%u read_cap=%u write_attempts=%u orders=%u waits=%u core_reads=%u core_writes=%u core_kicks=%u maps=%u unmaps=%u mapped=%u elapsed_us=%llu timeout_us=%llu index_valid0=0x%x index_valid1=0x%x index_range0=0x%x index_range1=0x%x index_stable=0x%x ownership_proven=0 bus_abort_proven=0\n",
        r->capture_rc, r->discover_rc, r->adapter_error, r->host_discovered, r->layout_valid,
        r->reads, LGRR_READ_CAP, r->write_attempts, r->orders, r->wait_attempts,
        r->host.stats.reads, r->host.stats.writes, r->host.stats.kicks,
        r->map_attempts, r->unmaps, r->mapped, (unsigned long long)r->elapsed_us,
        (unsigned long long)LGRR_TIME_US, r->index_valid[0], r->index_valid[1],
        r->index_in_range[0], r->index_in_range[1], r->index_stable);
}
static __attribute__((noinline)) void lithium_gpu_rpm_read_probe(void)
{
    if (!lithium_peripheral_requested.gpu_rpm_diag) {
        printf("LITHIUM GPU rpm end status=disabled reads=0 writes=0\n"); return;
    }
    uint64_t hz = 0;
    if (!lg0_adapter_admit() ||
        !PE_parse_boot_argn("cpu_perf_hz", &hz, sizeof(hz)) || hz != LGRR_CPU_HZ) {
        printf("LITHIUM GPU rpm end status=blocked reason=ram180_cpu614 reads=0 writes=0\n"); return;
    }
    if (!__sync_bool_compare_and_swap(&lgrr_once, 0, 1)) {
        printf("LITHIUM GPU rpm end status=blocked reason=one_shot reads=0 writes=0\n"); return;
    }
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU rpm end status=blocked reason=cpu_bind reads=0 writes=0\n"); return;
    }
    lgrr_retained = (struct lgrr_result){0};
    struct lgrr_context context = {.result = &lgrr_retained};
    printf("LITHIUM GPU rpm begin logical=%d affinity=0x%llx ram_pa=0x%llx ram_size=0x%x toc_offset=0x%x read_cap=%u timeout_us=%llu doorbell_mapped=0 gpu_aperture=unread smmu=unread writes_allowed=0\n",
        cpu_number(), (unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),
        (unsigned long long)LGRR_RAM_PA, LGRR_RAM_SIZE, LGRR_TOC_OFFSET,
        LGRR_READ_CAP, (unsigned long long)LGRR_TIME_US);
    lgrr_capture(&context);
    lgrr_log(&lgrr_retained); /* mapping already released; logging is outside I/O budget */
    lithium_touch_cpu_leave(&scope);
}
#endif
