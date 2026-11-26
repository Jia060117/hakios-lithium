/* SPDX-License-Identifier: MIT
 * Exercise the actual kernel adapter with MMIO/kernel seams. No device I/O.
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
struct allocation { uintptr_t pointer; uint64_t pa, bytes; };
static struct {
    struct allocation maps[6];
    uint64_t us;
    unsigned nmap, nunmap, commands, wdata, stores, loads;
    unsigned selected[2], staged, enabled;
    unsigned fail_map, deny_command, never_ready, bad_mx, bad_owner;
    unsigned enabled_values[2];
    bool profile;
} m;
struct lithium_touch_cpu_scope { unsigned entered; };
static struct lithium_peripheral_options lithium_peripheral_requested;
static bool lg0_adapter_admit(void) { return m.profile; }
static bool lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *s)
{ s->entered = 1; return true; }
static void lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *s)
{ assert(s->entered); s->entered = 0; }
static int cpu_number(void) { return 0; }
static int ml_get_max_cpu_number(void) { return 2; }
static bool ml_get_interrupts_enabled(void) { return true; }
#define LITHIUM_PERIPHERAL_AFFINITY() UINT64_C(1)
static uint64_t mach_absolute_time(void) { return m.us++; }
static void absolutetime_to_nanoseconds(uint64_t v, uint64_t *out) { *out = v * 1000; }
static bool PE_parse_boot_argn(const char *key, void *out, size_t size)
{
    assert(!strcmp(key, "cpu_perf_hz") && size == 8);
    *(uint64_t *)out = 614400000;
    return true;
}
static vm_offset_t ml_io_map_unmappable(uint64_t pa, uint64_t bytes, unsigned attr)
{
    assert(m.nmap < 6 && bytes == PAGE_SIZE && pa % PAGE_SIZE == 0 && attr == 7);
    unsigned i = m.nmap++;
    if (m.fail_map == i + 1) return 0;
    void *p = NULL;
    assert(!posix_memalign(&p, PAGE_SIZE, bytes));
    m.maps[i] = (struct allocation){(uintptr_t)p, pa, bytes};
    return (uintptr_t)p;
}
static void ml_io_unmap(vm_offset_t pointer, uint64_t bytes)
{
    for (unsigned i = 0; i < m.nmap; ++i)
        if (m.maps[i].pointer == pointer) {
            assert(bytes == m.maps[i].bytes);
            free((void *)pointer); m.maps[i].pointer = 0; ++m.nunmap; return;
        }
    assert(!"unmap must use original allocation");
}
static uint64_t physical(uintptr_t pointer)
{
    for (unsigned i = 0; i < m.nmap; ++i)
        if (m.maps[i].pointer && pointer >= m.maps[i].pointer &&
            pointer - m.maps[i].pointer <= m.maps[i].bytes - 4)
            return m.maps[i].pa + pointer - m.maps[i].pointer;
    assert(!"unmapped MMIO"); return 0;
}
static unsigned pmic(unsigned rail, unsigned address)
{
    if (address == 0x1708)
        return rail ? (m.bad_mx ? 0 : 0x81) : (m.enabled && !m.never_ready ? 0x81 : 0);
    switch (address) {
    case 0x1704: return 0x1c;
    case 0x1705: return 9;
    case 0x1740: return 0;
    case 0x1741: return rail ? 0x9a : 0xb8;
    case 0x1745: return 0x40;
    case 0x1746: return rail ? 0x80 : m.enabled;
    case 0x1761: return 0x82;
    }
    assert(!"unauthorized PMIC read"); return 0;
}
static uint32_t load32(uintptr_t pointer)
{
    uint64_t pa = physical(pointer); ++m.loads;
    if (pa == 0x0400f000) return 0x20010000;
    if (pa >= 0x0400f800 && pa < 0x0400fa00) {
        unsigned index = (unsigned)(pa - 0x0400f800) / 4;
        return index == 52 ? 0x31700 : index == 127 ? 0x11700 : 0;
    }
    if (pa == 0x0400a7d0) return m.bad_owner ? 4 : 0;
    if (pa == 0x0400a8fc) return 4;
    if (pa == 0x008c4034 || pa == 0x008c4024) return 0x00222001;
    if (pa == 0x008c403c || pa == 0x008c4044 ||
        pa == 0x008c4028 || pa == 0x008c40b0) return 0x80000000;
    if (pa == 0x045a0004) return 0;
    if (pa == 0x045a0008) return m.deny_command == m.commands && m.commands ? 5 : 1;
    for (unsigned rail = 0; rail < 2; ++rail) {
        uint64_t base = UINT64_C(0x04c00000) + UINT64_C(0x8000) * (rail ? 127 : 52);
        if (pa == base + 4) return 0;
        if (pa == base + 8) return 1;
        if (pa == base + 0x18) return pmic(rail, m.selected[rail]);
    }
    assert(!"unauthorized physical read"); return 0;
}
static void store32(uintptr_t pointer, uint32_t value)
{
    uint64_t pa = physical(pointer); ++m.stores;
    for (unsigned rail = 0; rail < 2; ++rail) {
        uint64_t base = UINT64_C(0x04c00000) + UINT64_C(0x8000) * (rail ? 127 : 52);
        if (pa == base) {
            assert((value & ~UINT32_C(0xff0)) == (1u << 27));
            m.selected[rail] = 0x1700 | ((value >> 4) & 0xff);
            return;
        }
    }
    if (pa == 0x045a0010) {
        assert(m.wdata < 2 && value == (m.wdata ? 0 : 0x80));
        m.staged = value; ++m.wdata; return;
    }
    assert(pa == 0x045a0000 && value == 0x460 && m.commands < 2 &&
        m.wdata == m.commands + 1);
    m.enabled_values[m.commands++] = m.staged;
    if (m.deny_command != m.commands) m.enabled = m.staged;
}
static int quiet_printf(const char *format, ...)
{
    char line[768]; va_list args;
    va_start(args, format); int result = vsnprintf(line, sizeof(line), format, args);
    va_end(args); assert(result >= 0 && result < (int)sizeof(line)); return result;
}
#define LG1_BARRIER() ((void)0)
#define LG1_LOAD32(p) load32(p)
#define LG1_DELAY_US(us) (m.us += (us))
#define LGPA_STORE32(p,v) store32(p,v)
#define printf quiet_printf
#include "../../ports/hakios-lithium/gpu/g1-power-adapter.inc"
#include "../../ports/hakios-lithium/gpu/g1-rail-adapter.inc"
#undef printf
static void reset(void)
{
    memset(&m, 0, sizeof(m)); m.profile = true;
    lgpa_once = lgpa_local_bus_lock = 0;
    memset(&lgrc_retained, 0, sizeof(lgrc_retained));
    memset(&lgrc_authorization, 0, sizeof(lgrc_authorization));
    assert(lithium_peripheral_options_parse("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1",
        strlen("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1"), &lithium_peripheral_requested));
}
int main(void)
{
    reset(); lithium_gpu_g1_rail_cycle_probe();
    assert(lgrc_retained.error == 0 && lgrc_retained.ready_seen &&
        lgrc_retained.original_verified && !m.enabled && m.commands == 2 &&
        m.nmap == 6 && m.nunmap == 6 && !lgpa_local_bus_lock);
    unsigned stores = m.stores;
    lithium_gpu_g1_rail_cycle_probe(); assert(m.stores == stores);
    reset(); lithium_peripheral_requested.gpu_rail_cycle = false;
    lithium_gpu_g1_rail_cycle_probe(); assert(!m.loads && !m.stores);
    reset(); m.bad_owner = 1; lithium_gpu_g1_rail_cycle_probe(); assert(!m.commands);
    reset(); m.bad_mx = 1; lithium_gpu_g1_rail_cycle_probe(); assert(!m.commands);
    reset(); m.never_ready = 1; lithium_gpu_g1_rail_cycle_probe();
    assert(lgrc_retained.power.error == LGP_SETTLE_TIMEOUT &&
        lgrc_retained.original_verified && m.commands == 2 && !m.enabled);
    reset(); m.deny_command = 1; lithium_gpu_g1_rail_cycle_probe();
    assert(m.commands == 1 && !lgrc_retained.original_verified);
    reset(); m.deny_command = 2; lithium_gpu_g1_rail_cycle_probe();
    assert(m.commands == 2 && !lgrc_retained.original_verified && lgrc_retained.retain_power_state);
    for (unsigned i = 1; i <= 6; ++i) {
        reset(); m.fail_map = i; lithium_gpu_g1_rail_cycle_probe();
        assert(!m.commands && m.nunmap == i - 1 && !lgpa_local_bus_lock);
    }
    puts("PASS actual GPU rail adapter: success, defaults, reuse, ownership, readiness, transport and six mapping failures");
}
