/* SPDX-License-Identifier: GPL-2.0-only
 * S1 observer only. Caller owns the bound RAM worker; no PMIC setting write.
 * Fresh two-pass bytes, exact dynamic APID, shared SPMI CAS and full VM unmap.
 */
#ifndef LITHIUM_GPU_S1_EVIDENCE_ADAPTER_INC
#define LITHIUM_GPU_S1_EVIDENCE_ADAPTER_INC
#include "s1-evidence-core.h"
#define LGS1A_LOAD_CAP 2500u
#define LGS1A_TIME_US UINT64_C(20000)
struct lgs1_adapter_result {
    uint8_t raw[2][LGS1_FIELD_COUNT];
    uint32_t valid[2], apid, mapping, commands, loads, maps, unmaps;
    unsigned stable, complete, digital_valid, ready_valid, ready, released, route_valid;
    uint32_t mapping_reads, matches;
    uint32_t digital_uv;
    int error, callback_error;
    struct lgs1_result last;
    uint64_t started_us, last_us, elapsed_us;
};
struct lgs1_adapter_context {
    struct lgs1_adapter_result *r;
    struct lgpa_map map[2];
    struct lgs1_route route;
    unsigned route_valid, held;
};
static int lgs1a_decode_uv(const uint8_t *v, uint32_t *uv)
{
    uint32_t base,step,min,max;
    if (v[2]>1u) return 0;
    if (v[0]==0x1cu && v[1]==9u) {
        base=v[2]?160000u:80000u; step=v[2]?10000u:5000u;
        min=v[2]?1360000u:350000u; max=v[2]?2200000u:1355000u;
    } else if (v[0]==0x1cu && v[1]==8u) {
        base=0; step=v[2]?10000u:5000u;
        min=v[2]?1280000u:350000u; max=v[2]?2040000u:1275000u;
    } else if (v[0]==3u && v[1]==8u) {
        base=v[2]?1550000u:375000u; step=v[2]?25000u:12500u;
        min=v[2]?1575000u:375000u; max=v[2]?3125000u:1562500u;
    } else return 0;
    uint32_t value=base+step*v[3];
    if (value<min || value>max) return 0;
    *uv=value; return 1;
}
static int lgs1a_gate(struct lgs1_adapter_context *c)
{
    uint64_t t=lgpa_now();
    if (t<c->r->last_us || t-c->r->started_us>=LGS1A_TIME_US ||
        cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 || !ml_get_interrupts_enabled())
        return -1;
    c->r->last_us=t;
    return 0;
}
static int lgs1a_time(void *opaque, uint64_t *out)
{
    struct lgs1_adapter_context *c=opaque;
    if (!out || lgs1a_gate(c)) return -1;
    *out=c->r->last_us; return 0;
}
static vm_offset_t lgs1a_pointer(struct lgs1_adapter_context *c, uint64_t pa, unsigned slot)
{
    struct lgpa_map *m=&c->map[slot];
    if (!m->allocation) {
        if (!r38_mmio_window_init(pa,4u,PAGE_SIZE,&m->window) || PAGE_SIZE!=16384u ||
            m->window.mapped_bytes!=PAGE_SIZE) return 0;
        m->allocation=ml_io_map_unmappable(m->window.physical_base,m->window.mapped_bytes,VM_WIMG_IO);
        if (!m->allocation) return 0;
        ++c->r->maps;
    }
    if (pa<m->window.physical_base || pa-m->window.physical_base>m->window.mapped_bytes-4u)
        return 0;
    return m->allocation+(vm_offset_t)(pa-m->window.physical_base);
}
static int lgs1a_read(void *opaque, uint64_t pa, uint32_t *raw)
{
    struct lgs1_adapter_context *c=opaque;
    if (!raw || (pa&3u) || lgs1a_gate(c) || c->r->loads>=LGS1A_LOAD_CAP) return -1;
    unsigned slot;
    if (pa==LGS1_CORE_PA || (pa>=LGS1_CORE_PA+0x800u && pa<=LGS1_CORE_PA+LGS1_CORE_BYTES-4u)) slot=0;
    else if (c->route_valid && lgs1_route_valid(&c->route,1u,0x1400u) &&
             (pa==c->route.observer_pa+4u ||
             pa==c->route.observer_pa+8u || pa==c->route.observer_pa+0x18u)) slot=1;
    else return -2;
    vm_offset_t p=lgs1a_pointer(c,pa,slot);
    if (!p || lgs1a_gate(c)) return -3;
    ++c->r->loads; LG1_BARRIER(); *raw=LG1_LOAD32(p); LG1_BARRIER();
    return lgs1a_gate(c);
}
static int lgs1a_write(void *opaque, uint64_t pa, uint32_t value)
{
    struct lgs1_adapter_context *c=opaque;
    uint32_t addr=0x1400u|((value>>4)&255u);
    if (!c->route_valid || !lgs1_route_valid(&c->route,1u,0x1400u) ||
        pa!=c->route.observer_pa || c->r->commands>=16u ||
        value!=((UINT32_C(1)<<27)|((addr&255u)<<4)) ||
        !lgs1_request_valid(1u,addr,1u) || lgs1a_gate(c)) return -1;
    vm_offset_t p=lgs1a_pointer(c,pa,1);
    if (!p || lgs1a_gate(c)) return -2;
    ++c->r->commands; LG1_BARRIER(); LGPA_STORE32(p,value); LG1_BARRIER();
    return lgs1a_gate(c);
}
static int lgs1a_delay(void *opaque, uint32_t us)
{
    struct lgs1_adapter_context *c=opaque;
    if (us!=1u || lgs1a_gate(c)) return -1;
    LG1_DELAY_US(us); return lgs1a_gate(c);
}
static void lgs1a_capture(struct lgs1_adapter_result *r)
{
    *r=(struct lgs1_adapter_result){0};
    struct lgs1_adapter_context c={.r=r};
    r->started_us=r->last_us=lgpa_now();
    if (lgpa_builtin_acquire(0)) { r->error=-1; return; }
    c.held=1;
    const struct lgs1_io io={&c,lgs1a_read,lgs1a_write,lgs1a_time,lgs1a_delay};
    enum lgs1_error probe_error=lgs1_probe(&io,1u,0x1400u,&r->last);
    c.route=r->last.route;
    r->apid=c.route.apid; r->mapping=c.route.mapping_raw;
    r->matches=r->last.matches; r->mapping_reads=r->last.mapping_reads;
    if (probe_error) { r->error=-2; goto cleanup; }
    c.route_valid=r->route_valid=1;
    for (unsigned pass=0;pass<2u;++pass) for (unsigned f=0;f<LGS1_FIELD_COUNT;++f) {
        enum lgs1_error read_error=lgs1_read(&io,&c.route,1u,0x1400u+lgs1_fields[f].offset,1u,&r->last);
        r->mapping_reads+=r->last.mapping_reads;
        if (read_error ||
            !r->last.data_valid) { r->error=-3; goto cleanup; }
        r->raw[pass][f]=r->last.data[0]; r->valid[pass]|=1u<<f;
    }
    r->stable=1;
    for (unsigned f=0;f<LGS1_FIELD_COUNT;++f)
        if (r->raw[0][f]!=r->raw[1][f]) r->stable=0;
    r->complete=r->stable && r->valid[0]==255u && r->valid[1]==255u;
    /* Pinned qpnp-regulator.c legacy ranges; unsupported subtypes remain raw. */
    r->digital_valid=r->complete && lgs1a_decode_uv(r->raw[1],&r->digital_uv);
    r->ready_valid=r->digital_valid;
    r->ready=r->ready_valid && (r->raw[1][5]&0x80u) && (r->raw[1][7]&0x81u)==0x81u;
cleanup:
    if (r->last.error==LGS1_ROUTE_CHANGED || r->last.error==LGS1_VERSION ||
        r->last.error==LGS1_OBSERVER_RANGE) r->route_valid=0;
    r->callback_error=r->last.callback_rc;
    for (unsigned i=2;i-- >0;) if (c.map[i].allocation) {
        ml_io_unmap(c.map[i].allocation,c.map[i].window.mapped_bytes);
        c.map[i].allocation=0; ++r->unmaps;
    }
    if (c.held) { r->released=lgpa_builtin_release(0)==0; if (!r->released) r->error=-4; }
    uint64_t t=lgpa_now();
    r->elapsed_us=t>=r->started_us?t-r->started_us:0;
    if (t<r->last_us || t<r->started_us || r->elapsed_us>=LGS1A_TIME_US) r->error=-5;
    if (r->error) { r->complete=0; r->digital_valid=0; r->ready_valid=0; r->ready=0; }
}
static void lgs1a_log(const struct lgs1_adapter_result *r)
{
    for (unsigned pass=0;pass<2u;++pass) for (unsigned f=0;f<LGS1_FIELD_COUNT;++f)
        printf("LITHIUM GPU S1 byte pass=%u field=%u addr=0x%x raw=0x%x valid=%u\n",
            pass,f,0x1400u+lgs1_fields[f].offset,r->raw[pass][f],!!(r->valid[pass]&(1u<<f)));
    printf("LITHIUM GPU S1 end error=%d callback_error=%d last_error=%u last_phase=%u last_matches=%u last_mapping_reads=%u matches=%u mapping_reads=%u route_valid=%u apid=%u mapping=0x%x complete=%u stable=%u digital_valid=%u digital_uv=%u ready_valid=%u ready=%u commands=%u loads=%u maps=%u unmaps=%u released=%u elapsed_us=%llu pmic_setting_writes=0\n",
        r->error,r->callback_error,(unsigned)r->last.error,(unsigned)r->last.phase,
        r->last.matches,r->last.mapping_reads,r->matches,r->mapping_reads,r->route_valid,
        r->apid,r->mapping,r->complete,r->stable,r->digital_valid,r->digital_uv,
        r->ready_valid,r->ready,r->commands,r->loads,r->maps,r->unmaps,r->released,(unsigned long long)r->elapsed_us);
}
#endif
