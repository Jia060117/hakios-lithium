/* SPDX-License-Identifier: MIT
 * Actual OPEN-only adapter + unchanged rpm-host.c. Plain fake memory only.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../ports/hakios-lithium/diagnostics/cpu-mmio-window-r38.h"
typedef uintptr_t vm_offset_t;
#define PAGE_SIZE UINT64_C(16384)
#define VM_WIMG_IO 7
#define TEST_TX 0x800u
#define TEST_RX 0xc00u
#define TEST_SIZE 0x3f8u
struct lithium_touch_cpu_scope { unsigned entered; };
static struct { bool gpu_rpm_open; unsigned gpu_stage; } lithium_peripheral_requested;
static struct {
    uint8_t ram[0x6000];
    struct { uintptr_t allocation; uint64_t pa, bytes; } maps[2];
    uint64_t us, cpu_hz;
    unsigned maps_called, active_maps, unmaps, loads, stores, bells, waits, leaves;
    unsigned sent[5], silent, fail_map, late_map, late_store, late_read, regress_store;
    unsigned bad_name, bad_version, send_close, send_data, change_before_open;
    unsigned bad_cpu, bad_affinity, bad_irq, fail_bind;
    bool profile;
} m;
static unsigned cases;
static uint32_t word(uint32_t off)
{
    assert(off <= sizeof(m.ram) - 4u);
    return (uint32_t)m.ram[off] | (uint32_t)m.ram[off + 1u] << 8 |
        (uint32_t)m.ram[off + 2u] << 16 | (uint32_t)m.ram[off + 3u] << 24;
}
static void put(uint32_t off, uint32_t value)
{
    assert(off <= sizeof(m.ram) - 4u);
    for (unsigned i = 0; i < 4; ++i) m.ram[off + i] = (uint8_t)(value >> (8u * i));
}
static bool lg0_adapter_admit(void) { return m.profile; }
static bool PE_parse_boot_argn(const char *key, void *out, size_t size)
{
    assert(!strcmp(key, "cpu_perf_hz") && size == 8);
    *(uint64_t *)out = m.cpu_hz; return true;
}
static bool lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *s)
{ if (m.fail_bind) return false; s->entered = 1; return true; }
static void lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *s)
{ assert(s->entered); s->entered = 0; ++m.leaves; }
static int cpu_number(void) { return m.bad_cpu ? 1 : 0; }
static int ml_get_interrupts_enabled(void) { return !m.bad_irq; }
#define LITHIUM_PERIPHERAL_AFFINITY() (m.bad_affinity ? UINT64_C(2) : UINT64_C(1))
static uint64_t mach_absolute_time(void) { return m.us++; }
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *out) { *out = t * 1000u; }
static vm_offset_t ml_io_map_unmappable(uint64_t pa, uint64_t bytes, unsigned flags)
{
    unsigned i = m.maps_called++;
    assert(i < 2 && flags == 7);
    assert(pa == (i ? UINT64_C(0x09820000) : UINT64_C(0x68000)));
    assert(bytes == (i ? UINT64_C(0x4000) : UINT64_C(0x8000)));
    if (m.fail_map == i + 1u) return 0;
    void *p = NULL; assert(!posix_memalign(&p, (size_t)PAGE_SIZE, (size_t)bytes));
    m.maps[i].allocation = (uintptr_t)p; m.maps[i].pa = pa; m.maps[i].bytes = bytes;
    ++m.active_maps;
    if (m.late_map == i + 1u) m.us += 100000;
    return (uintptr_t)p;
}
static void ml_io_unmap(vm_offset_t allocation, uint64_t bytes)
{
    assert(m.active_maps);
    /* Stronger than matching extents: actual adapter must release in reverse. */
    unsigned i = m.maps[1].allocation ? 1u : 0u;
    assert(allocation == m.maps[i].allocation && bytes == m.maps[i].bytes);
    free((void *)allocation); m.maps[i].allocation = 0;
    --m.active_maps; ++m.unmaps;
}
static uint64_t physical(uintptr_t pointer)
{
    for (unsigned i = 0; i < 2; ++i)
        if (m.maps[i].allocation && pointer >= m.maps[i].allocation &&
            pointer - m.maps[i].allocation <= m.maps[i].bytes - 4u)
            return m.maps[i].pa + pointer - m.maps[i].allocation;
    assert(!"unmapped MMIO"); return 0;
}
static uint32_t load32(uintptr_t pointer)
{
    uint64_t pa = physical(pointer);
    assert(pa >= 0x68000 && pa <= 0x6dffc && !(pa & 3u));
    ++m.loads;
    uint32_t value = word((uint32_t)(pa - 0x68000));
    if (m.change_before_open && m.loads == 143) value = 8;
    if (m.loads == m.late_read) m.us += 100000;
    return value;
}
static void store32(uintptr_t pointer, uint32_t value)
{
    uint64_t pa = physical(pointer);
    ++m.stores;
    if (pa == 0x09820010) { assert(value == 1); ++m.bells; }
    else {
        assert(pa >= 0x68000 && pa < 0x6df00 && !(pa & 3u));
        uint32_t off = (uint32_t)(pa - 0x68000);
        assert(off == TEST_TX + 4u || off == TEST_RX ||
            (off >= TEST_TX + 8u && off < TEST_TX + 8u + TEST_SIZE));
        put(off, value);
    }
    /* A store can take effect and then return after the deadline. */
    if (m.stores == m.late_store) m.us += 100000;
    if (m.stores == m.regress_store) m.us = 0;
}
static void remote_push(const uint8_t *p, uint32_t n)
{
    uint32_t head = word(TEST_RX + 4u), tail = word(TEST_RX);
    uint32_t used = head >= tail ? head - tail : TEST_SIZE - tail + head;
    assert(!(n & 7u) && n <= TEST_SIZE - used - 8u);
    for (uint32_t i = 0; i < n; ++i) m.ram[TEST_RX + 8u + (head + i) % TEST_SIZE] = p[i];
    put(TEST_RX + 4u, (head + n) % TEST_SIZE); /* peer-owned index */
}
static void peer_command(uint32_t cmd, uint32_t p1, uint32_t p2)
{
    uint8_t b[8];
    uint32_t values[2] = {cmd | p1 << 16, p2};
    for (unsigned w = 0; w < 2; ++w)
        for (unsigned i = 0; i < 4; ++i) b[w * 4u + i] = (uint8_t)(values[w] >> (8u * i));
    remote_push(b, sizeof(b));
}
static uint32_t unpack(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void peer_drain(void)
{
    uint32_t head = word(TEST_TX + 4u), tail = word(TEST_TX);
    while (tail != head) {
        uint8_t b[24] = {0};
        for (unsigned i = 0; i < 8; ++i) b[i] = m.ram[TEST_TX + 8u + (tail + i) % TEST_SIZE];
        uint32_t cmd = unpack(b) & 0xffffu, p1 = unpack(b) >> 16;
        assert(cmd == 0 || cmd == 1 || cmd == 2 || cmd == 4); /* never TX_DATA/request */
        uint32_t n = cmd == 2 ? 24u : 8u;
        for (uint32_t i = 8; i < n; ++i) b[i] = m.ram[TEST_TX + 8u + (tail + i) % TEST_SIZE];
        uint32_t available = head >= tail ? head - tail : TEST_SIZE - tail + head;
        assert(available >= n);
        tail = (tail + n) % TEST_SIZE; put(TEST_TX, tail); ++m.sent[cmd];
        if (cmd == 0) {
            assert(p1 == 1 && !unpack(b + 4));
            if (m.send_close) peer_command(3, 7, 0);
            else if (m.send_data) {
                /* A complete TX_DATA header reaches the core's pending-id
                 * rejection. An 8-byte partial header correctly just waits. */
                uint8_t data_header[16] = {9, 0, 7, 0};
                data_header[8] = 20;
                remote_push(data_header, sizeof(data_header));
            }
            else {
                peer_command(0, m.bad_version ? 2 : 1, 0);
                peer_command(1, 1, 0);
            }
        } else if (cmd == 1) assert(p1 == 1 && !unpack(b + 4));
        else if (cmd == 2) {
            assert(p1 == 1 && unpack(b + 4) == 13 && !memcmp(b + 8, "rpm_requests", 13));
            if (m.silent == 2) continue;
            uint8_t open[24] = {0};
            uint32_t first = 2u | 7u << 16;
            for (unsigned i = 0; i < 4; ++i) open[i] = (uint8_t)(first >> (8u * i));
            open[4] = 13;
            memcpy(open + 8, "rpm_requests", 13);
            if (m.bad_name) open[8] = 'X';
            remote_push(open, 24); peer_command(4, 1, 0);
        } else assert(p1 == 7 && !unpack(b + 4));
    }
}
static void delay_us(uint32_t us)
{
    assert(us && us <= 50); m.us += us; ++m.waits;
    if (m.silent != 1) peer_drain();
}
static int quiet_printf(const char *format, ...)
{
    char line[2048]; va_list args;
    va_start(args, format); int n = vsnprintf(line, sizeof(line), format, args); va_end(args);
    assert(n >= 0 && n < (int)sizeof(line));
    if (strstr(line, "LITHIUM GPU rpm open flags") || strstr(line, "LITHIUM GPU rpm open write"))
        assert(!m.active_maps);
    return n;
}
#define LG1_BARRIER() ((void)0)
#define LG1_LOAD32(p) load32(p)
#define LG1_DELAY_US(us) delay_us(us)
#define LGRO_STORE32(p, v) store32((p), (v))
#define printf quiet_printf
#include "../../ports/hakios-lithium/gpu/rpm/rpm-open-adapter.inc"
#undef printf
static void reset(void)
{
    assert(!m.active_maps);
    memset(&m, 0, sizeof(m)); m.profile = true; m.cpu_hz = 614400000;
    lgro_once = lgro_local_lock = 0;
    memset(&lgro_retained, 0, sizeof(lgro_retained));
    lithium_peripheral_requested.gpu_rpm_open = true;
    lithium_peripheral_requested.gpu_stage = 1;
    put(0x5f00, 0x67727430); put(0x5f04, 2);
    put(0x5f08, 0x61703272); put(0x5f0c, TEST_TX); put(0x5f10, TEST_SIZE);
    put(0x5f14, 0x72326170); put(0x5f18, TEST_RX); put(0x5f1c, TEST_SIZE);
}
static void probe(void)
{
    ++cases;
    lithium_gpu_rpm_open_probe();
    assert(!m.active_maps && m.unmaps <= 2 && lgro_retained.operations <= 10000);
    assert(!lgro_retained.host.pending_msgid && !lgro_retained.host.next_msgid &&
        !lgro_retained.host.ack_ready);
    if (lgro_retained.rc && lgro_retained.stores_attempted)
        assert(lgro_retained.outcome_uncertain && lgro_retained.traffic_retained);
}
static void direct_context(struct lgro_context *c, struct lgro_result *r)
{
    memset(r, 0, sizeof(*r)); memset(c, 0, sizeof(*c)); c->r = r;
    c->lease = c->write_mode = 1; r->zero_verified = 1; lgro_local_lock = 1;
    r->host.discovered = r->host.started = true;
    r->host.tx = (struct rpm_host_fifo){TEST_TX, TEST_SIZE, 0};
    r->host.rx = (struct rpm_host_fifo){TEST_RX, TEST_SIZE, 0};
    r->start_us = r->last_us = lgro_clock_raw();
    const uint64_t pa[2] = {0x68000, 0x09820010}, bytes[2] = {0x6000, 4};
    for (unsigned i = 0; i < 2; ++i) {
        assert(r38_mmio_window_init(pa[i], bytes[i], PAGE_SIZE, &c->maps[i].window));
        c->maps[i].allocation = ml_io_map_unmappable(c->maps[i].window.physical_base,
            c->maps[i].window.mapped_bytes, VM_WIMG_IO);
    }
}
int main(void)
{
    reset(); probe();
    assert(!lgro_retained.rc && lgro_retained.transport_open && lgro_retained.zero_verified &&
        lgro_retained.initial_valid[0] == 15 && lgro_retained.initial_valid[1] == 15 &&
        lgro_retained.final_valid == 15 && lgro_retained.host.rcid == 7);
    assert(lgro_retained.frames[0] == 1 && lgro_retained.frames[1] == 1 &&
        lgro_retained.frames[2] == 1 && lgro_retained.frames[3] == 1);
    assert(m.maps_called == 2 && m.unmaps == 2 && !lgro_local_lock &&
        lgro_retained.released && !lgro_retained.outcome_uncertain);
    assert(m.loads == 190 && m.stores == 24 && m.bells == 4);
    assert(lgro_retained.rx_trace_count && !lgro_retained.rx_trace_omitted);
    for (unsigned i = 0; i < lgro_retained.rx_trace_count; ++i)
        assert(lgro_retained.rx_trace[i].offset >= TEST_RX + 8u &&
            lgro_retained.rx_trace[i].offset < TEST_RX + 8u + TEST_SIZE);
    unsigned baseline_stores = m.stores, baseline_reads = m.loads;
    printf("open-only baseline reads=%u stores=%u bells=%u operations=%u elapsed_us=%llu\n",
        m.loads, m.stores, m.bells, lgro_retained.operations, (unsigned long long)lgro_retained.elapsed_us);
    probe(); assert(m.stores == baseline_stores); /* no second negotiation */
    reset(); lithium_peripheral_requested.gpu_rpm_open = false; probe(); assert(!m.maps_called);
    reset(); lithium_peripheral_requested.gpu_stage = 0; probe(); assert(!m.maps_called);
    reset(); m.profile = false; probe(); assert(!m.maps_called);
    reset(); m.cpu_hz = 691200000; probe(); assert(!m.maps_called);
    reset(); m.fail_bind = 1; probe(); assert(!m.maps_called);
    reset(); m.bad_cpu = 1; probe(); assert(!m.maps_called && !lgro_local_lock);
    reset(); m.bad_affinity = 1; probe(); assert(!m.maps_called);
    reset(); m.bad_irq = 1; probe(); assert(!m.maps_called);
    reset(); lgro_local_lock = 1; probe(); assert(!m.maps_called && lgro_local_lock == 1);
    lgro_local_lock = 0;
    for (unsigned i = 1; i <= 2; ++i) {
        reset(); m.fail_map = i; probe(); assert(!m.stores && m.unmaps == i - 1u && !lgro_local_lock);
        reset(); m.late_map = i; probe(); assert(!m.stores && m.unmaps == i && !lgro_local_lock);
    }
    const uint32_t offsets[4] = {TEST_TX, TEST_TX + 4u, TEST_RX, TEST_RX + 4u};
    for (unsigned i = 0; i < 4; ++i) {
        reset(); put(offsets[i], 8); probe();
        assert(lgro_retained.rc == RPM_HOST_EOWNERSHIP && !m.stores && !m.bells);
    }
    reset(); m.change_before_open = 1; probe();
    assert(lgro_retained.zero_verified && lgro_retained.rc == RPM_HOST_EOWNERSHIP && !m.stores);
    reset(); put(0x5f00, 0); probe(); assert(lgro_retained.rc == RPM_HOST_EPROTO && !m.stores);
    for (unsigned silent = 1; silent <= 2; ++silent) {
        reset(); m.silent = silent; probe();
        assert(lgro_retained.rc == RPM_HOST_ETIME && !lgro_retained.transport_open &&
            lgro_retained.outcome_uncertain && m.bells && lgro_retained.frames[0] == 1);
        if (silent == 2) assert(lgro_retained.frames[2] == 1 && !lgro_retained.host.open_acked);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        reset();
        if (mode == 0) m.bad_version = 1;
        if (mode == 1) m.bad_name = 1;
        if (mode == 2) m.send_close = 1;
        if (mode == 3) m.send_data = 1;
        probe(); assert(lgro_retained.rc == RPM_HOST_EPROTO && !lgro_retained.transport_open);
        assert(lgro_retained.rx_trace_count >= 2 && !lgro_retained.rx_trace_omitted);
    }
    for (unsigned i = 1; i <= baseline_stores; ++i) {
        reset(); m.late_store = i; probe();
        assert(lgro_retained.rc == RPM_HOST_ETIME && m.stores == i &&
            lgro_retained.outcome_uncertain && !lgro_retained.transport_open);
        unsigned stores = m.stores; probe(); assert(m.stores == stores);
    }
    for (unsigned i = 1; i <= baseline_reads; i += 17) {
        reset(); m.late_read = i; probe();
        assert(lgro_retained.rc == RPM_HOST_ETIME && m.loads == i);
    }
    reset(); m.regress_store = 4; probe();
    assert(lgro_retained.rc == RPM_HOST_ETIME && m.stores == 4);
    /* Independently deny foreign addresses, forbidden index writes and data
     * frames before any store. This includes an otherwise writable TX slot.
     */
    const uint64_t forbidden[] = {0x09820014, 0x09820010, 0x06c00000, 0x68100,
        0x68000 + TEST_TX, 0x68000 + TEST_RX + 4u, 0x6df00};
    for (unsigned i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        reset(); struct lgro_context c; struct lgro_result r;
        direct_context(&c, &r);
        assert(lgro_write32(&c, forbidden[i], 0) < 0 && !m.stores);
        lgro_cleanup(&c); ++cases;
    }
    reset(); struct lgro_context c; struct lgro_result r;
    direct_context(&c, &r);
    assert(lgro_write32(&c, 0x68000 + TEST_TX + 8u, 9u | 1u << 16) < 0 && !m.stores);
    lgro_cleanup(&c); ++cases;
    reset(); direct_context(&c, &r); r.operations = 10000;
    assert(lgro_write32(&c, 0x68000 + TEST_TX + 8u, 1u << 16) == RPM_HOST_EBUDGET && !m.stores);
    lgro_cleanup(&c); ++cases;
    reset(); direct_context(&c, &r);
    put(TEST_RX + 8u, 0xdeadbeefu);
    unsigned loads = m.loads;
    for (unsigned i = 0; i < 70; ++i) {
        uint32_t value = 0;
        assert(!lgro_read32(&c, 0x68000 + TEST_RX + 8u, &value));
        assert(value == 0xdeadbeefu);
    }
    assert(m.loads == loads + 70 && r.rx_trace_count == 64 && r.rx_trace_omitted == 6);
    for (unsigned i = 0; i < 64; ++i)
        assert(r.rx_trace[i].offset == TEST_RX + 8u && r.rx_trace[i].raw == 0xdeadbeefu);
    lgro_cleanup(&c); ++cases;
    printf("PASS actual RPM OPEN-only adapter: cases=%u fresh_live_zero=2x4 control_frames_only=1 no_resource_requests=1 late_store_sweep=%u maps_exact=2 no_retry=1\n",
        cases, baseline_stores);
    return 0;
}
