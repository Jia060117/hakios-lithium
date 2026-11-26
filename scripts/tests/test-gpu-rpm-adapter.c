/* SPDX-License-Identifier: MIT
 * Actual read-adapter inclusion with fake kernel mappings and message RAM.
 * Link the unchanged rpm-host.c separately. No hardware/device API exists.
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
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
typedef uintptr_t vm_offset_t;
#define PAGE_SIZE UINT64_C(16384)
#define VM_WIMG_IO 7
struct lithium_touch_cpu_scope { unsigned entered; };
static struct lithium_peripheral_options lithium_peripheral_requested;
static struct {
    uint32_t ram[0x6000 / 4];
    uintptr_t allocation;
    uint64_t pa, bytes, us, cpu_hz;
    unsigned maps, unmaps, loads, leaves, begin_logs, end_logs, toc_logs, index_logs;
    unsigned fail_map, map_late, late_load, regress_load, unstable_toc, changing_index;
    unsigned bad_cpu, bad_affinity, bad_irq, fail_bind, missing_hz;
    bool profile;
} m;
static unsigned cases;
static bool lg0_adapter_admit(void) { return m.profile; }
static bool lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *s)
{ if (m.fail_bind) return false; s->entered = 1; return true; }
static void lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *s)
{ assert(s->entered); s->entered = 0; ++m.leaves; }
static int cpu_number(void) { return m.bad_cpu ? 1 : 0; }
static int ml_get_interrupts_enabled(void) { return !m.bad_irq; }
#define LITHIUM_PERIPHERAL_AFFINITY() (m.bad_affinity ? UINT64_C(2) : UINT64_C(1))
static uint64_t mach_absolute_time(void) { return m.us++; }
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *ns) { *ns = t * 1000; }
static bool PE_parse_boot_argn(const char *name, void *out, size_t size)
{
    assert(!strcmp(name, "cpu_perf_hz") && size == sizeof(uint64_t));
    *(uint64_t *)out = m.cpu_hz;
    return !m.missing_hz;
}
static vm_offset_t ml_io_map_unmappable(uint64_t pa, uint64_t bytes, unsigned flags)
{
    assert(!m.allocation && pa == 0x68000 && bytes == 0x8000 && flags == VM_WIMG_IO);
    ++m.maps;
    if (m.fail_map) return 0;
    void *p = NULL;
    assert(!posix_memalign(&p, (size_t)PAGE_SIZE, (size_t)bytes));
    m.allocation = (uintptr_t)p; m.pa = pa; m.bytes = bytes;
    if (m.map_late) m.us += 20000;
    return m.allocation;
}
static void ml_io_unmap(vm_offset_t allocation, uint64_t bytes)
{
    assert(allocation == m.allocation && bytes == m.bytes && m.allocation);
    free((void *)allocation); m.allocation = 0; ++m.unmaps;
}
static uint32_t load32(uintptr_t pointer)
{
    assert(m.allocation && pointer >= m.allocation &&
        pointer - m.allocation <= m.bytes - 4);
    uint32_t off = (uint32_t)(pointer - m.allocation);
    /* Enforce actual accesses: TOC or four descriptor indices ONLY. */
    assert(off < 0x6000 && !(off & 3));
    assert(off >= 0x5f00 || off == 0x100 || off == 0x104 || off == 0x400 || off == 0x404);
    ++m.loads;
    uint32_t raw = m.ram[off / 4];
    if (m.unstable_toc && off == 0x5f00 && m.loads > 64) raw ^= 1;
    if (m.changing_index && off == 0x100 && m.loads > 134)
        raw = m.loads < 139 ? 0x28 : 0x30;
    if (m.loads == m.late_load) m.us += 20000;
    if (m.loads == m.regress_load) m.us = 0;
    return raw;
}
static int quiet_printf(const char *format, ...)
{
    char line[1536];
    va_list args;
    va_start(args, format); int n = vsnprintf(line, sizeof(line), format, args); va_end(args);
    assert(n >= 0 && n < (int)sizeof(line));
    if (strstr(line, "LITHIUM GPU rpm begin")) ++m.begin_logs;
    if (strstr(line, "LITHIUM GPU rpm end")) ++m.end_logs;
    if (strstr(line, "LITHIUM GPU rpm toc ")) { assert(!m.allocation); ++m.toc_logs; }
    if (strstr(line, "LITHIUM GPU rpm index ")) { assert(!m.allocation); ++m.index_logs; }
    return n;
}
#define LG1_BARRIER() ((void)0)
#define LG1_LOAD32(p) load32(p)
#define printf quiet_printf
#include "../../ports/hakios-lithium/gpu/rpm/rpm-read-adapter.inc"
#undef printf
static void reset(void)
{
    assert(!m.allocation);
    memset(&m, 0, sizeof(m)); m.profile = true; m.cpu_hz = 614400000;
    lgrr_once = 0; memset(&lgrr_retained, 0, sizeof(lgrr_retained));
    memset(&lithium_peripheral_requested, 0, sizeof(lithium_peripheral_requested));
    lithium_peripheral_requested.gpu_rpm_diag = true;
    uint32_t *toc = &m.ram[0x5f00 / 4];
    toc[0] = 0x67727430; toc[1] = 2;
    toc[2] = 0x61703272; toc[3] = 0x100; toc[4] = 0x100;
    toc[5] = 0x72326170; toc[6] = 0x400; toc[7] = 0x100;
    m.ram[0x100 / 4] = 8; m.ram[0x104 / 4] = 16;
    m.ram[0x400 / 4] = 24; m.ram[0x404 / 4] = 32;
}
static void probe(void)
{
    ++cases;
    uint32_t before[0x6000 / 4]; memcpy(before, m.ram, sizeof(before));
    lithium_gpu_rpm_read_probe();
    assert(!m.allocation && m.unmaps <= 1 && m.loads <= 256);
    assert(!memcmp(before, m.ram, sizeof(before)));
    assert(!lgrr_retained.write_attempts && !lgrr_retained.wait_attempts);
    assert(!lgrr_retained.host.stats.writes && !lgrr_retained.host.stats.kicks);
    if (m.maps && !m.fail_map) assert(m.unmaps == 1);
}
int main(void)
{
    reset(); probe();
    assert(lgrr_retained.capture_rc == RPM_HOST_OK && lgrr_retained.host_discovered);
    assert(m.loads == 142 && lgrr_retained.reads == 142 && lgrr_retained.host.stats.reads == 134);
    assert(lgrr_retained.layout_valid && lgrr_retained.toc_valid[0] == UINT64_MAX &&
        lgrr_retained.toc_valid[1] == UINT64_MAX && lgrr_retained.toc_stable == UINT64_MAX);
    assert(lgrr_retained.index_valid[0] == 15 && lgrr_retained.index_valid[1] == 15 &&
        lgrr_retained.index_in_range[0] == 15 && lgrr_retained.index_in_range[1] == 15 &&
        lgrr_retained.index_stable == 15);
    assert(m.begin_logs == 1 && m.end_logs == 1 && m.toc_logs == 128 && m.index_logs == 8);
    assert(lgrr_retained.map_base == 0x68000 && lgrr_retained.map_bytes == 0x8000 &&
        !lgrr_retained.host.initialized && !lgrr_retained.host.io.ctx);
    unsigned loads = m.loads; probe(); assert(m.loads == loads && m.maps == 1);
    reset(); lithium_peripheral_requested.gpu_rpm_diag = false; probe(); assert(!m.maps && !m.loads);
    reset(); m.profile = false; probe(); assert(!m.maps);
    reset(); m.cpu_hz = 691200000; probe(); assert(!m.maps);
    reset(); m.missing_hz = 1; probe(); assert(!m.maps);
    reset(); m.fail_bind = 1; probe(); assert(!m.maps);
    reset(); m.bad_cpu = 1; probe(); assert(!m.maps);
    reset(); m.bad_affinity = 1; probe(); assert(!m.maps);
    reset(); m.bad_irq = 1; probe(); assert(!m.maps);
    reset(); m.fail_map = 1; probe(); assert(m.maps == 1 && !m.unmaps && !m.loads);
    reset(); m.map_late = 1; probe(); assert(!m.loads && lgrr_retained.capture_rc == RPM_HOST_ETIME);
    for (unsigned which = 0; which < 5; ++which) {
        reset();
        uint32_t *toc = &m.ram[0x5f00 / 4];
        if (which == 0) toc[0] = 0;
        if (which == 1) toc[1] = 21;
        if (which == 2) toc[4] = 7;
        if (which == 3) toc[6] = 0x100;
        if (which == 4) toc[5] = 0x61703272;
        probe(); assert(lgrr_retained.capture_rc < 0 && m.loads == 128 && !m.index_logs);
    }
    reset(); m.unstable_toc = 1; probe();
    assert(lgrr_retained.discover_rc == RPM_HOST_EPROTO && m.loads == 128 &&
        lgrr_retained.toc_valid[1] == UINT64_MAX && lgrr_retained.toc_stable == UINT64_MAX - 1);
    reset(); m.ram[0x104 / 4] = 0x100; probe();
    assert(lgrr_retained.discover_rc == RPM_HOST_EBOUNDS && lgrr_retained.layout_valid &&
        lgrr_retained.index_valid[0] == 15 && !(lgrr_retained.index_in_range[0] & 2));
    reset(); m.changing_index = 1; probe();
    assert(lgrr_retained.capture_rc == 0 && lgrr_retained.index_stable == 14);
    for (unsigned i = 1; i <= 142; ++i) {
        reset(); m.late_load = i; probe();
        assert(lgrr_retained.capture_rc == RPM_HOST_ETIME && m.loads == i);
        if (i <= 64) {
            uint64_t bit = UINT64_C(1) << (i - 1);
            assert((lgrr_retained.toc_loaded[0] & bit) && !(lgrr_retained.toc_valid[0] & bit));
        }
    }
    reset(); m.regress_load = 40; probe();
    assert(lgrr_retained.capture_rc == RPM_HOST_ETIME && m.loads == 40);
    /* Directly prove the forbidden callback cannot issue ANY store, and the
     * physical read cap/address whitelist does not depend on discover behavior.
     */
    const uint64_t forbidden[] = {0x09820010, 0x06c00000, 0x00670000, 0x6e000, 0x68108};
    for (unsigned i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        reset();
        struct lgrr_result r = {0};
        struct lgrr_context c = {.result = &r};
        r.start_us = r.last_us = lgrr_clock_raw();
        assert(lgrr_deny_write(&c, forbidden[i], 1) < 0 && r.write_attempts == 1 && !m.loads);
        r.adapter_error = 0; uint32_t out = 0;
        assert(lgrr_read32(&c, forbidden[i], &out) < 0 && !m.loads);
        ++cases;
    }
    reset();
    struct lgrr_result r = {.reads = 256};
    struct lgrr_context c = {.result = &r, .allocation = 1};
    r.start_us = r.last_us = lgrr_clock_raw();
    uint32_t out = 0;
    assert(lgrr_read32(&c, 0x6df00, &out) == RPM_HOST_EBUDGET && !m.loads);
    ++cases;
    printf("PASS actual RPM read adapter: cases=%u normal_reads=142 core_reads=134 write_callbacks=denied two_TOC_passes=64x2 indices=4x2 PAGE_SIZE=16384 map_extent=32768\n", cases);
    return 0;
}
