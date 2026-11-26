/* SPDX-License-Identifier: MIT
 * Explicit, default-off RPM VERSION/OPEN only. Include after unchanged
 * rpm-host.c (once) or link that C file separately, plus existing R38/G0
 * CPU/MMIO primitives. Main owns parser/build and the single worker.
 * No other XNU RPM consumer may share this isolated profile/worker.
 * Local CAS + fresh zero indices do NOT freeze TZ or another firmware EE.
 * Never reset indices, replay a failed session, close, vote or load a GPU.
 */
#ifndef LITHIUM_GPU_RPM_OPEN_ADAPTER_INC
#define LITHIUM_GPU_RPM_OPEN_ADAPTER_INC
#include "rpm-host.h"
#ifndef LGRO_STORE32
#define LGRO_STORE32(p, value) (*(volatile uint32_t *)(p) = (value))
#endif
#define LGRO_RAM_PA UINT64_C(0x68000)
#define LGRO_RAM_SIZE 0x6000u
#define LGRO_TOC 0x5f00u
#define LGRO_BELL_PA UINT64_C(0x09820010)
#define LGRO_CPU_HZ UINT64_C(614400000)
#define LGRO_TIME_US UINT64_C(100000)
#define LGRO_IO_CAP 10000u
#define LGRO_TRACE_CAP 128u
#define LGRO_RX_TRACE_CAP 64u
struct lgro_trace { uint64_t pa; uint32_t value, kind; };
struct lgro_result {
    int rc, discover_rc, open_rc, adapter_error;
    unsigned operations, reads, write_callbacks, stores_attempted, stores_returned;
    unsigned bell_attempts, bell_returns, published_attempts, orders, waits, rejected;
    unsigned map_attempts, unmaps, lease_held, released, zero_verified, transport_open;
    unsigned traffic_retained, outcome_uncertain, trace_count, trace_omitted;
    uint32_t initial[2][4], initial_valid[2], final[4], final_valid;
    uint64_t toc_valid[2], toc_stable, start_us, last_us, elapsed_us;
    uint64_t map_base[2], map_bytes[2];
    uint32_t toc[2][64];
    unsigned frames[4]; /* VERSION, VERSION_ACK, OPEN, OPEN_ACK publications */
    struct lgro_trace trace[LGRO_TRACE_CAP];
    struct { uint32_t offset, raw; } rx_trace[LGRO_RX_TRACE_CAP];
    unsigned rx_trace_count, rx_trace_omitted;
    struct rpm_host host; /* historical only: io/initialized cleared at end */
};
struct lgro_map { struct r38_mmio_window window; vm_offset_t allocation; };
struct lgro_context {
    struct lgro_result *r;
    struct lgro_map maps[2];
    unsigned lease, write_mode, release_ordered, notify_pending;
    uint32_t tx_words[6], tx_count, tx_length;
    uint32_t rx_header, rx_header_pos;
    unsigned rx_header_valid;
    uint32_t rx_open_name_len;
    unsigned rx_open_name_len_valid;
};
static struct lgro_result lgro_retained;
static unsigned lgro_once, lgro_local_lock;

static uint64_t lgro_clock_raw(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000u;
}
static int lgro_error(struct lgro_context *c, int error)
{
    if (!c->r->adapter_error) c->r->adapter_error = error;
    return c->r->adapter_error;
}
static uint64_t lgro_now(void *opaque)
{
    struct lgro_context *c = opaque;
    uint64_t now = lgro_clock_raw();
    if (now < c->r->last_us || now - c->r->start_us >= LGRO_TIME_US)
        (void)lgro_error(c, RPM_HOST_ETIME);
    if (now >= c->r->last_us) c->r->last_us = now;
    return now;
}
static int lgro_gate(struct lgro_context *c)
{
    if (!c || !c->r || !c->lease) return RPM_HOST_EOWNERSHIP;
    if (c->r->adapter_error) return c->r->adapter_error;
    (void)lgro_now(c);
    if (cpu_number() != 0 || LITHIUM_PERIPHERAL_AFFINITY() != 1 ||
        !ml_get_interrupts_enabled()) return lgro_error(c, RPM_HOST_EOWNERSHIP);
    return c->r->adapter_error;
}
static int lgro_take(struct lgro_context *c)
{
    int rc = lgro_gate(c);
    if (rc) return rc;
    if (c->r->operations >= LGRO_IO_CAP) return lgro_error(c, RPM_HOST_EBUDGET);
    ++c->r->operations;
    return 0;
}
static int lgro_layout(const struct rpm_host *s)
{
    for (unsigned i = 0; i < 2; ++i) {
        const struct rpm_host_fifo *f = i ? &s->rx : &s->tx;
        if ((f->off & 3u) || f->off > LGRO_TOC - 8u || f->size < 64u ||
            (f->size & 7u) || f->size > LGRO_TOC - 8u - f->off) return 0;
    }
    return s->tx.off + 8u + s->tx.size <= s->rx.off ||
        s->rx.off + 8u + s->rx.size <= s->tx.off;
}
static vm_offset_t lgro_pointer(struct lgro_context *c, unsigned map, uint64_t off)
{
    struct lgro_map *m = &c->maps[map];
    if (!m->allocation || off > m->window.mapped_bytes - m->window.data_offset - 4u)
        return 0;
    return m->allocation + (vm_offset_t)m->window.data_offset + (vm_offset_t)off;
}
static int lgro_read32(void *opaque, uint64_t pa, uint32_t *out)
{
    struct lgro_context *c = opaque;
    if (!c || !c->r || !out) return RPM_HOST_EINVAL;
    int rc = lgro_take(c);
    if (rc) return rc;
    if ((pa & 3u) || pa < LGRO_RAM_PA || pa - LGRO_RAM_PA > LGRO_RAM_SIZE - 4u)
        return lgro_error(c, RPM_HOST_EBOUNDS);
    uint32_t off = (uint32_t)(pa - LGRO_RAM_PA);
    struct rpm_host *s = &c->r->host;
    int toc = !s->started && off >= LGRO_TOC;
    int allowed = toc;
    if (!allowed && lgro_layout(s)) {
        allowed = off == s->tx.off || off == s->tx.off + 4u ||
            off == s->rx.off || off == s->rx.off + 4u;
        if (c->write_mode && s->started && s->discovered &&
            off >= s->rx.off + 8u && off - s->rx.off - 8u < s->rx.size)
            allowed = 1; /* RX payload only, never a GPU or doorbell read */
    }
    if (!allowed) { ++c->r->rejected; return lgro_error(c, RPM_HOST_EBOUNDS); }
    vm_offset_t pointer = lgro_pointer(c, 0, off);
    if (!pointer) return lgro_error(c, RPM_HOST_EBOUNDS);
    ++c->r->reads;
    LG1_BARRIER(); *out = LG1_LOAD32(pointer); LG1_BARRIER();
    if (c->write_mode && s->started && off >= s->rx.off + 8u &&
        off - s->rx.off - 8u < s->rx.size) {
        /* Retain the existing load; never perform extra diagnostic MMIO. */
        unsigned n = c->r->rx_trace_count;
        if (n < LGRO_RX_TRACE_CAP) {
            c->r->rx_trace[n].offset = off;
            c->r->rx_trace[n].raw = *out;
            ++c->r->rx_trace_count;
        } else ++c->r->rx_trace_omitted;
    }
    rc = lgro_gate(c);
    if (toc) {
        unsigned word = (off - LGRO_TOC) / 4u;
        uint64_t bit = UINT64_C(1) << word;
        unsigned pass = !!(c->r->toc_valid[0] & bit);
        c->r->toc[pass][word] = *out;
        if (!rc) c->r->toc_valid[pass] |= bit;
    }
    if (!rc && s->started && off == s->rx.off + 8u + s->rx.pos) {
        c->rx_header = *out; c->rx_header_pos = s->rx.pos; c->rx_header_valid = 1;
        c->rx_open_name_len_valid = 0;
    }
    if (!rc && s->started && c->rx_header_valid && c->rx_header_pos == s->rx.pos &&
        off == s->rx.off + 8u + (s->rx.pos + 4u) % s->rx.size) {
        c->rx_open_name_len = *out; c->rx_open_name_len_valid = 1;
    }
    return rc;
}
static uint32_t lgro_channel_word(unsigned word)
{
    static const unsigned char channel[16] = "rpm_requests";
    unsigned start = (word - 2u) * 4u;
    return (uint32_t)channel[start] | (uint32_t)channel[start + 1u] << 8 |
        (uint32_t)channel[start + 2u] << 16 | (uint32_t)channel[start + 3u] << 24;
}
static int lgro_payload_word(struct lgro_context *c, uint32_t off, uint32_t value)
{
    const struct rpm_host *s = &c->r->host;
    if (c->tx_count >= 6u ||
        off != s->tx.off + 8u + (s->tx.pos + 4u * c->tx_count) % s->tx.size)
        return 0;
    if (!c->tx_count) {
        uint32_t cmd = value & 0xffffu, p1 = value >> 16;
        /* Exact control-only wire whitelist. TX_DATA/req/KVP cannot pass. */
        if ((cmd == 0u || cmd == 1u || cmd == 2u) ? p1 != 1u :
            (cmd != 4u || !p1)) return 0;
        c->tx_length = cmd == 2u ? 6u : 2u;
    } else if (c->tx_count == 1u) {
        if (value != ((c->tx_words[0] & 0xffffu) == 2u ? 13u : 0u)) return 0;
    } else if (c->tx_length != 6u || value != lgro_channel_word(c->tx_count)) return 0;
    c->tx_words[c->tx_count++] = value;
    return 1;
}
static void lgro_record(struct lgro_result *r, uint64_t pa, uint32_t value, unsigned kind)
{
    if (r->trace_count < LGRO_TRACE_CAP)
        r->trace[r->trace_count++] = (struct lgro_trace){pa, value, kind};
    else ++r->trace_omitted;
}
static int lgro_write32(void *opaque, uint64_t pa, uint32_t value)
{
    struct lgro_context *c = opaque;
    if (!c || !c->r) return RPM_HOST_EINVAL;
    ++c->r->write_callbacks;
    int rc = lgro_take(c);
    if (rc) return rc;
    struct rpm_host *s = &c->r->host;
    if (!c->write_mode || !c->r->zero_verified || !s->started ||
        !s->discovered || !lgro_layout(s) || (pa & 3u))
        return lgro_error(c, RPM_HOST_EOWNERSHIP);
    vm_offset_t pointer = 0;
    unsigned kind = 0;
    if (pa == LGRO_BELL_PA) {
        if (value != 1u || !c->release_ordered || !c->notify_pending)
            goto forbidden;
        pointer = lgro_pointer(c, 1, 0); kind = 4;
    } else if (pa >= LGRO_RAM_PA && pa - LGRO_RAM_PA <= LGRO_RAM_SIZE - 4u) {
        uint32_t off = (uint32_t)(pa - LGRO_RAM_PA);
        if (off >= s->tx.off + 8u && off - s->tx.off - 8u < s->tx.size) {
            if (!lgro_payload_word(c, off, value)) goto forbidden;
            kind = 1; /* bounded sequential protocol payload */
        } else if (off == s->tx.off + 4u) {
            if (!c->release_ordered || !c->tx_length || c->tx_count != c->tx_length ||
                value != (s->tx.pos + c->tx_length * 4u) % s->tx.size) goto forbidden;
            kind = 2;
        } else if (off == s->rx.off) {
            uint32_t cmd = c->rx_header & 0xffffu, frame_bytes = 8u;
            if (cmd == 2u) {
                if (!c->rx_open_name_len_valid || c->rx_open_name_len < 2u ||
                    c->rx_open_name_len > RPM_HOST_OPEN_NAME_MAX) goto forbidden;
                frame_bytes = 8u + ((c->rx_open_name_len + 7u) & ~7u);
                if (frame_bytes > s->rx.size - 8u) goto forbidden;
            }
            if (!c->release_ordered || !c->rx_header_valid || c->rx_header_pos != s->rx.pos ||
                (cmd != 0u && cmd != 1u && cmd != 2u && cmd != 4u && cmd != 13u && cmd != 15u) ||
                value != (s->rx.pos + frame_bytes) % s->rx.size) goto forbidden;
            kind = 3;
        } else goto forbidden; /* no TX tail, RX head, TOC or resource writes */
        pointer = lgro_pointer(c, 0, off);
    } else goto forbidden;
    if (!pointer) goto forbidden;
    ++c->r->stores_attempted; /* no replay even if the store returns late */
    if (kind == 2u || kind == 3u) ++c->r->published_attempts;
    if (kind == 4u) ++c->r->bell_attempts;
    c->r->traffic_retained = 1;
    lgro_record(c->r, pa, value, kind);
    LG1_BARRIER(); LGRO_STORE32(pointer, value); LG1_BARRIER();
    ++c->r->stores_returned;
    if (kind == 2u) {
        unsigned cmd = c->tx_words[0] & 0xffffu;
        ++c->r->frames[cmd == 4u ? 3u : cmd];
        c->tx_count = c->tx_length = 0;
    }
    if (kind == 2u || kind == 3u) c->notify_pending = 1;
    if (kind == 3u) c->rx_header_valid = c->rx_open_name_len_valid = 0;
    if (kind == 4u) { ++c->r->bell_returns; c->notify_pending = 0; }
    c->release_ordered = 0;
    return lgro_gate(c);
forbidden:
    ++c->r->rejected;
    return lgro_error(c, RPM_HOST_EBOUNDS);
}
static int lgro_order(void *opaque, enum rpm_host_order order)
{
    struct lgro_context *c = opaque;
    int rc = lgro_take(c);
    if (rc) return rc;
    ++c->r->orders;
    if (order != RPM_HOST_RELEASE && order != RPM_HOST_ACQUIRE)
        return lgro_error(c, RPM_HOST_EINVAL);
    LG1_BARRIER();
    c->release_ordered = order == RPM_HOST_RELEASE;
    return lgro_gate(c);
}
static int lgro_wait(void *opaque, uint32_t us)
{
    struct lgro_context *c = opaque;
    int rc = lgro_take(c);
    if (rc) return rc;
    ++c->r->waits;
    if (!us || us > 50u) return lgro_error(c, RPM_HOST_EINVAL);
    if (c->r->last_us - c->r->start_us + us >= LGRO_TIME_US)
        return lgro_error(c, RPM_HOST_ETIME);
    LG1_DELAY_US(us);
    return lgro_gate(c);
}
static void lgro_cleanup(struct lgro_context *c)
{
    for (unsigned i = 2; i-- > 0;)
        if (c->maps[i].allocation) {
            ml_io_unmap(c->maps[i].allocation, c->maps[i].window.mapped_bytes);
            c->maps[i].allocation = 0; ++c->r->unmaps;
        }
    if (c->lease) {
        c->r->released = __sync_bool_compare_and_swap(&lgro_local_lock, 1, 0);
        c->r->lease_held = !c->r->released;
        c->lease = c->r->lease_held;
    }
}
static void lgro_capture(struct lgro_context *c)
{
    struct lgro_result *r = c->r;
    r->start_us = r->last_us = lgro_clock_raw();
    r->discover_rc = r->open_rc = RPM_HOST_ESTATE;
    if (!__sync_bool_compare_and_swap(&lgro_local_lock, 0, 1)) {
        r->rc = RPM_HOST_EOWNERSHIP; return;
    }
    c->lease = r->lease_held = 1;
    if (rpm_host_msm8996.ram_pa != LGRO_RAM_PA ||
        rpm_host_msm8996.ram_size != LGRO_RAM_SIZE ||
        rpm_host_msm8996.doorbell_pa != LGRO_BELL_PA ||
        rpm_host_msm8996.doorbell_mask != 1u) {
        (void)lgro_error(c, RPM_HOST_EBOUNDS); goto done;
    }
    const uint64_t pa[2] = {LGRO_RAM_PA, LGRO_BELL_PA};
    const uint64_t bytes[2] = {LGRO_RAM_SIZE, 4u};
    for (unsigned i = 0; i < 2; ++i) {
        struct lgro_map *m = &c->maps[i];
        if (lgro_gate(c)) goto done;
        if (!r38_mmio_window_init(pa[i], bytes[i], PAGE_SIZE, &m->window)) {
            (void)lgro_error(c, RPM_HOST_EBOUNDS); goto done;
        }
        r->map_base[i] = m->window.physical_base; r->map_bytes[i] = m->window.mapped_bytes;
        ++r->map_attempts;
        m->allocation = ml_io_map_unmappable(m->window.physical_base,
            m->window.mapped_bytes, VM_WIMG_IO);
        if (!m->allocation) { (void)lgro_error(c, RPM_HOST_EIO); goto done; }
        if (lgro_gate(c)) goto done;
    }
    const struct rpm_host_io io = {c, lgro_read32, lgro_write32, lgro_order, lgro_now, lgro_wait};
    int rc = rpm_host_init(&r->host, &io);
    if (rc) { (void)lgro_error(c, rc); goto done; }
    r->discover_rc = rpm_host_discover(&r->host,
        (struct rpm_host_limits){256, 16, (uint32_t)LGRO_TIME_US});
    if (r->discover_rc) goto done;
    for (unsigned i = 0; i < 64; ++i)
        if (r->toc[0][i] == r->toc[1][i]) r->toc_stable |= UINT64_C(1) << i;
    const uint32_t offsets[4] = {r->host.tx.off, r->host.tx.off + 4u,
        r->host.rx.off, r->host.rx.off + 4u};
    for (unsigned pass = 0; pass < 2; ++pass) {
        LG1_BARRIER();
        for (unsigned i = 0; i < 4; ++i) {
            if (lgro_read32(c, LGRO_RAM_PA + offsets[i], &r->initial[pass][i])) goto done;
            r->initial_valid[pass] |= 1u << i;
            if (r->initial[pass][i]) { r->open_rc = RPM_HOST_EOWNERSHIP; goto done; }
        }
    }
    r->zero_verified = 1;
    c->write_mode = 1;
    /* True asserts only the isolated XNU worker/local CAS/fresh live edge
     * admission above. It never asserts that TZ cannot access message RAM.
     * The existing core performs its own four current-zero reads again.
     */
    r->open_rc = rpm_host_open(&r->host,
        (struct rpm_host_limits){LGRO_IO_CAP, 2048, (uint32_t)LGRO_TIME_US}, true);
    if (!r->open_rc && !lgro_gate(c)) {
        r->transport_open = r->host.peer_version && r->host.version_acked &&
            r->host.local_open && r->host.remote_open && r->host.open_acked;
        for (unsigned i = 0; i < 4; ++i) {
            if (lgro_read32(c, LGRO_RAM_PA + offsets[i], &r->final[i])) break;
            r->final_valid |= 1u << i;
        }
    }
done:
    r->rc = r->adapter_error ? r->adapter_error :
        (r->discover_rc ? r->discover_rc : r->open_rc);
    if (r->rc) r->transport_open = 0;
    r->outcome_uncertain = r->rc && r->stores_attempted;
    r->elapsed_us = r->last_us - r->start_us;
    lgro_cleanup(c); /* unmap only; no close, vote, reset or FIFO cleanup write */
    r->host.io = (struct rpm_host_io){0}; r->host.initialized = false;
}
static void lgro_log(const struct lgro_result *r)
{
    for (unsigned i = 0; i < r->host.foreign_open_count; ++i) {
        const struct rpm_host_foreign_open *f = &r->host.foreign_open[i];
        char hex[2u * RPM_HOST_OPEN_NAME_MAX + 1u];
        static const char digits[] = "0123456789abcdef";
        for (unsigned j = 0; j < f->name_len; ++j) {
            hex[j * 2u] = digits[f->name[j] >> 4]; hex[j * 2u + 1u] = digits[f->name[j] & 15u];
        }
        hex[f->name_len * 2u] = 0;
        printf("LITHIUM GPU rpm open foreign index=%u rcid=%u name_len=%u name_hex=%s consumed=%u ack_sent=%u scope=transport_only\n",
            i, (unsigned)f->rcid, (unsigned)f->name_len, hex, (unsigned)f->consumed, (unsigned)f->acked);
    }
    for (unsigned i = 0; i < r->rx_trace_count; ++i)
        printf("LITHIUM GPU rpm open rx index=%u offset=0x%x raw=0x%x\n",
            i, r->rx_trace[i].offset, r->rx_trace[i].raw);
    printf("LITHIUM GPU rpm open rx_summary recorded=%u omitted=%u extra_reads=0\n",
        r->rx_trace_count, r->rx_trace_omitted);
    for (unsigned pass = 0; pass < 2; ++pass)
        printf("LITHIUM GPU rpm open initial pass=%u valid=0x%x tx_tail=0x%x tx_head=0x%x rx_tail=0x%x rx_head=0x%x\n",
            pass, r->initial_valid[pass], r->initial[pass][0], r->initial[pass][1],
            r->initial[pass][2], r->initial[pass][3]);
    for (unsigned i = 0; i < r->trace_count; ++i)
        printf("LITHIUM GPU rpm open write index=%u kind=%u pa=0x%llx raw=0x%x\n",
            i, r->trace[i].kind, (unsigned long long)r->trace[i].pa, r->trace[i].value);
    printf("LITHIUM GPU rpm open flags peer_version=%u version_acked=%u local_open=%u remote_open=%u open_acked=%u remote_channel=%u tx_off=0x%x tx_size=0x%x rx_off=0x%x rx_size=0x%x tx_pos=0x%x rx_pos=0x%x toc_valid0=0x%llx toc_valid1=0x%llx toc_stable=0x%llx\n",
        r->host.peer_version, r->host.version_acked, r->host.local_open,
        r->host.remote_open, r->host.open_acked, r->host.rcid,
        r->host.tx.off, r->host.tx.size, r->host.rx.off, r->host.rx.size,
        r->host.tx.pos, r->host.rx.pos, (unsigned long long)r->toc_valid[0],
        (unsigned long long)r->toc_valid[1], (unsigned long long)r->toc_stable);
    printf("LITHIUM GPU rpm open end rc=%d discover_rc=%d open_rc=%d adapter_error=%d transport_open=%u zero_verified=%u operations=%u io_cap=%u reads=%u write_callbacks=%u stores_attempted=%u stores_returned=%u bell_attempts=%u bell_returns=%u published_attempts=%u waits=%u trace_omitted=%u maps=%u unmaps=%u lease_held=%u released=%u traffic_retained=%u uncertain=%u elapsed_us=%llu timeout_us=%llu final_valid=0x%x final_tx_tail=0x%x final_tx_head=0x%x final_rx_tail=0x%x final_rx_head=0x%x resource_requests=0 pmic_writes=0 gpu_writes=0 tz_exclusive_proven=0 bus_abort_proven=0\n",
        r->rc, r->discover_rc, r->open_rc, r->adapter_error, r->transport_open,
        r->zero_verified, r->operations, LGRO_IO_CAP, r->reads, r->write_callbacks,
        r->stores_attempted, r->stores_returned, r->bell_attempts, r->bell_returns,
        r->published_attempts, r->waits, r->trace_omitted, r->map_attempts, r->unmaps,
        r->lease_held, r->released, r->traffic_retained, r->outcome_uncertain,
        (unsigned long long)r->elapsed_us, (unsigned long long)LGRO_TIME_US,
        r->final_valid, r->final[0], r->final[1], r->final[2], r->final[3]);
}
static __attribute__((noinline)) void lithium_gpu_rpm_open_probe(void)
{
    if (!lithium_peripheral_requested.gpu_rpm_open) {
        printf("LITHIUM GPU rpm open end status=disabled writes=0\n"); return;
    }
    uint64_t hz = 0;
    if (lithium_peripheral_requested.gpu_stage != 1 || !lg0_adapter_admit() ||
        !PE_parse_boot_argn("cpu_perf_hz", &hz, sizeof(hz)) || hz != LGRO_CPU_HZ) {
        printf("LITHIUM GPU rpm open end status=blocked reason=stage1_ram180_cpu614 writes=0\n"); return;
    }
    if (!__sync_bool_compare_and_swap(&lgro_once, 0, 1)) {
        printf("LITHIUM GPU rpm open end status=blocked reason=one_shot writes=0\n"); return;
    }
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU rpm open end status=blocked reason=cpu_bind writes=0\n"); return;
    }
    lgro_retained = (struct lgro_result){0};
    struct lgro_context context = {.r = &lgro_retained};
    printf("LITHIUM GPU rpm open begin logical=%d affinity=0x%llx ram_pa=0x%llx ram_size=0x%x doorbell_pa=0x%llx doorbell_mask=1 io_cap=%u timeout_us=%llu scope=transport_only resource_requests=0 pmic_writes=0 gpu_writes=0\n",
        cpu_number(), (unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),
        (unsigned long long)LGRO_RAM_PA, LGRO_RAM_SIZE, (unsigned long long)LGRO_BELL_PA,
        LGRO_IO_CAP, (unsigned long long)LGRO_TIME_US);
    lgro_capture(&context);
    lgro_log(&lgro_retained); /* mappings already released; retain raw outcome */
    lithium_touch_cpu_leave(&scope);
}
#endif
