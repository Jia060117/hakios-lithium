/* SPDX-License-Identifier: MIT
 * G1 prerequisites, never power-on: two exact SPMI peripheral snapshots and
 * immutable QFPROM rows. Observer CMD writes request READs only; there are no
 * PMIC, clock, GDSC, IRQ or GPU aperture writes. Call once in an isolated RAM
 * boot after UEFI CPU initialization, bound to logical0/affinity1. Disabled
 * PMIC/cpufreq drivers and the one-shot worker avoid another XNU channel user;
 * route/config checks do not prove exclusion of TZ or firmware clients.
 */
#ifndef LITHIUM_GPU_G1_PRECHECK_INC
#define LITHIUM_GPU_G1_PRECHECK_INC
#include "g1-spmi-read.h"
#include "g1-fuse.h"

#define LG1_BUDGET_US UINT64_C(250000)
#define LG1_ACCESS_CAP 9000u
#define LG1_COMMAND_CAP (2u * LGS_RAIL_COUNT * LGS_FIELD_COUNT)
#define LG1_FUSE_BASE UINT64_C(0x00074000)

struct lg1_mapping { struct r38_mmio_window window; vm_offset_t allocation; };
static struct {
    unsigned used, reads, writes, fuse_reads;
    uint64_t start_us, last_us;
    bool poisoned;
    struct lg1_mapping core, observer, fuses;
    uint64_t observer_pa;
    uint32_t allowed_cmd;
    struct lgs_result transaction;
    struct lgs_route routes[LGS_RAIL_COUNT];
    unsigned rail_valid[LGS_RAIL_COUNT][2];
    uint8_t rail_raw[LGS_RAIL_COUNT][2][LGS_FIELD_COUNT];
    unsigned rail_stable[LGS_RAIL_COUNT], rail_error[LGS_RAIL_COUNT];
    uint64_t fuse_raw[2][LGF_ROW_COUNT];
    unsigned fuse_valid[2], fuse_stable;
    struct lgf_result fuse_decoded;
} lg1;

static uint64_t lg1_now(void)
{
    uint64_t ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    return ns/1000;
}
static bool lg1_owner(void)
{
    return cpu_number()==0 && LITHIUM_PERIPHERAL_AFFINITY()==1 &&
        ml_get_interrupts_enabled();
}
static bool lg1_open(void)
{
    const uint64_t now=lg1_now();
    if (lg1.poisoned || !lg1_owner() || now<lg1.last_us ||
        now-lg1.start_us>=LG1_BUDGET_US ||
        lg1.reads+lg1.writes+lg1.fuse_reads>=LG1_ACCESS_CAP) {
        lg1.poisoned=true;
        return false;
    }
    lg1.last_us=now;
    return true;
}
static bool lg1_map(struct lg1_mapping *m,uint64_t pa,uint64_t bytes)
{
    if (!lg1_open() || !r38_mmio_window_init(pa,bytes,PAGE_SIZE,&m->window)) return false;
    m->allocation=ml_io_map_unmappable(m->window.physical_base,
        m->window.mapped_bytes,VM_WIMG_IO);
    printf("LITHIUM GPU supply map physical=0x%llx allocation=0x%lx bytes=%llu valid=%u\n",
        (unsigned long long)m->window.physical_base,(unsigned long)m->allocation,
        (unsigned long long)m->window.mapped_bytes,(unsigned)(m->allocation!=0));
    return m->allocation!=0;
}
static void lg1_unmap(struct lg1_mapping *m)
{
    if (!m->allocation) return;
    printf("LITHIUM GPU supply unmap allocation=0x%lx bytes=%llu\n",
        (unsigned long)m->allocation,(unsigned long long)m->window.mapped_bytes);
    ml_io_unmap(m->allocation,m->window.mapped_bytes);
    m->allocation=0;
}
static vm_offset_t lg1_pointer(const struct lg1_mapping *m,uint64_t pa,unsigned bytes)
{
    if (!m->allocation || pa< m->window.physical_base || !bytes ||
        bytes>m->window.mapped_bytes || pa-m->window.physical_base>m->window.mapped_bytes-bytes)
        return 0;
    return m->allocation+pa-m->window.physical_base;
}
static int lg1_read32(void *opaque,uint64_t pa,uint32_t *out)
{
    (void)opaque;
    vm_offset_t p=0;
    if (!out || (pa&3) || !lg1_open()) return -1;
    if (pa==LGS_CORE_PA ||
        (pa>=LGS_CORE_PA+0x800 && pa<LGS_CORE_PA+0x800+4*LGS_CHANNELS))
        p=lg1_pointer(&lg1.core,pa,4);
    else if (lg1.observer_pa && (pa==lg1.observer_pa+4 || pa==lg1.observer_pa+8 ||
        pa==lg1.observer_pa+0x18 || pa==lg1.observer_pa+0x1c))
        p=lg1_pointer(&lg1.observer,pa,4);
    if (!p) return -2;
    LG1_BARRIER();
    *out=LG1_LOAD32(p);
    LG1_BARRIER();
    ++lg1.reads;
    if (!lg1_open()) return -3; /* returning load may itself exhaust budget */
    return 0;
}
static int lg1_read_command(void *opaque,uint64_t pa,uint32_t cmd)
{
    (void)opaque;
    if (!lg1_open() || !lg1.observer_pa || pa!=lg1.observer_pa ||
        !lg1.allowed_cmd || cmd!=lg1.allowed_cmd || (cmd>>27)!=1 ||
        lg1.writes>=LG1_COMMAND_CAP) return -1;
    const vm_offset_t p=lg1_pointer(&lg1.observer,pa,4);
    if (!p) return -2;
    LG1_BARRIER();
    LG1_STORE_READ_COMMAND(p,cmd);
    LG1_BARRIER();
    ++lg1.writes;
    if (!lg1_open()) return -3; /* attempted command remains recorded by core */
    return 0;
}
static int lg1_time(void *opaque,uint64_t *out)
{
    (void)opaque;
    if (!out || !lg1_open()) return -1;
    *out=lg1.last_us;
    return 0;
}
static int lg1_delay(void *opaque,uint32_t us)
{
    (void)opaque;
    if (us>1000 || !lg1_open()) return -1;
    LG1_DELAY_US(us);
    return lg1_open()?0:-1;
}
static void lg1_capture_fuses(void)
{
    if (!lg1_map(&lg1.fuses,LG1_FUSE_BASE,0x1000)) return;
    for(unsigned pass=0;pass<2;++pass)
        for(unsigned i=0;i<LGF_ROW_COUNT;++i) {
            const uint64_t pa=LG1_FUSE_BASE+8*lgf_rows[i].rowid;
            const vm_offset_t p=lg1_pointer(&lg1.fuses,pa,8);
            if (!p || (pa&7) || !lg1_open()) continue;
            LG1_BARRIER();
            lg1.fuse_raw[pass][i]=LG1_LOAD64(p);
            LG1_BARRIER();
            ++lg1.fuse_reads;
            if (!lg1_open()) continue;
            lg1.fuse_valid[pass]|=1u<<i;
        }
    lg1_unmap(&lg1.fuses);
    for(unsigned i=0;i<LGF_ROW_COUNT;++i) {
        const unsigned bit=1u<<i;
        if ((lg1.fuse_valid[0]&lg1.fuse_valid[1]&bit) &&
            lg1.fuse_raw[0][i]==lg1.fuse_raw[1][i]) lg1.fuse_stable|=bit;
        for(unsigned pass=0;pass<2;++pass) {
            if (lg1.fuse_valid[pass]&bit)
                printf("LITHIUM GPU fuse pass=%u row=%u pa=0x%llx bits=64 valid=1 raw=0x%016llx stable=%u\n",
                    pass,lgf_rows[i].rowid,(unsigned long long)(LG1_FUSE_BASE+8*lgf_rows[i].rowid),
                    (unsigned long long)lg1.fuse_raw[pass][i],(lg1.fuse_stable&bit)!=0);
            else
                printf("LITHIUM GPU fuse pass=%u row=%u bits=64 valid=0 raw=UNREAD stable=0\n",pass,lgf_rows[i].rowid);
        }
    }
    if (lg1.fuse_stable==LGF_ALL_VALID &&
        lgf_decode(lg1.fuse_raw[0],lg1.fuse_valid[0],&lg1.fuse_decoded)) {
        const struct lgf_result *d=&lg1.fuse_decoded;
        printf("LITHIUM GPU fuse decode valid=1 revision=%u speedbin=%u limitation=%u aging_raw=%u init0=%u init1=%u init2=%u init3=%u offset1=%u offset3=%u voltage_uv=UNVERIFIED\n",
            d->revision,d->speedbin,d->limitation,d->aging,d->initcode[0],d->initcode[1],
            d->initcode[2],d->initcode[3],d->offsetraw[1],d->offsetraw[3]);
    } else printf("LITHIUM GPU fuse decode valid=0 reason=missing_or_unstable_rows voltage_uv=UNVERIFIED\n");
}
static void lg1_capture_rail(const struct lgs_io *io,unsigned index)
{
    const struct lgs_rail *rail=&lgs_rails[index];
    struct lgs_result *r=&lg1.transaction;
    enum lgs_error error=lgs_probe(io,rail->sid,rail->base,r);
    printf("LITHIUM GPU supply route rail=%s sid=%u base=0x%x error=%u version=0x%08x apid=%u mapping=0x%08x matches=%u observer_pa=0x%llx\n",
        rail->name,rail->sid,rail->base,error,r->route.version,r->route.apid,
        r->route.mapping_raw,r->matches,(unsigned long long)r->route.observer_pa);
    if (error) goto done;
    lg1.routes[index]=r->route;
    if (!lg1_map(&lg1.observer,r->route.observer_pa,r->route.observer_bytes)) {
        error=LGS_ARGUMENT;
        goto done;
    }
    lg1.observer_pa=r->route.observer_pa;
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned field=0;field<LGS_FIELD_COUNT;++field) {
            const unsigned offset=lgs_fields[field].offset;
            lg1.allowed_cmd=(1u<<27)|(offset<<4);
            error=lgs_read(io,&lg1.routes[index],rail->sid,rail->base+offset,1,r);
            lg1.allowed_cmd=0;
            if (r->data_valid) {
                lg1.rail_raw[index][pass][field]=r->data[0];
                lg1.rail_valid[index][pass]|=1u<<field;
            }
            if (r->data_valid)
                printf("LITHIUM GPU supply pass=%u rail=%s sid=%u reg=%s addr=0x%x valid=1 raw=0x%02x error=%u phase=%u status_valid=%u status=0x%x attempted=%u completed=%u polls=%u\n",
                    pass,rail->name,rail->sid,lgs_fields[field].name,rail->base+offset,r->data[0],error,r->phase,
                    r->status_valid,r->status_raw,r->cmd_attempted,r->cmd_completed,r->polls);
            else
                printf("LITHIUM GPU supply pass=%u rail=%s sid=%u reg=%s addr=0x%x valid=0 raw=UNREAD error=%u phase=%u status_valid=%u status=0x%x attempted=%u completed=%u polls=%u\n",
                    pass,rail->name,rail->sid,lgs_fields[field].name,rail->base+offset,error,r->phase,
                    r->status_valid,r->status_raw,r->cmd_attempted,r->cmd_completed,r->polls);
            if (error) goto done;
        }
        if (!pass && lg1_delay(NULL,1000)) { error=LGS_DELAY_ERROR; goto done; }
    }
done:
    lg1.rail_error[index]=error;
    for(unsigned field=0;field<LGS_FIELD_COUNT;++field) {
        unsigned bit=1u<<field;
        if ((lg1.rail_valid[index][0]&lg1.rail_valid[index][1]&bit) &&
            lg1.rail_raw[index][0][field]==lg1.rail_raw[index][1][field])
            lg1.rail_stable[index]|=bit;
    }
    lg1_unmap(&lg1.observer);
    lg1.observer_pa=0;
    lg1.allowed_cmd=0;
}
static __attribute__((noinline)) void lithium_gpu_g1_precheck(void)
{
    if (!lg0_adapter_admit() || !__sync_bool_compare_and_swap(&lg1.used,0,1)) {
        printf("LITHIUM GPU supply blocked=1 reason=once_ram_profile mmio=0\n");
        return;
    }
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU supply blocked=1 reason=cpu_bind mmio=0\n");
        return;
    }
    lg1.start_us=lg1.last_us=lg1_now();
    for(unsigned i=0;i<LGS_RAIL_COUNT;++i) lg1.rail_error[i]=LGS_ARGUMENT;
    printf("LITHIUM GPU supply begin logical=%d affinity=0x%llx gpu_power_diag=1 max_commands=%u budget_us=%llu gpu_writes=0 regulator_writes=0 spmi_operation=EXT_READL ownership=UNVERIFIED\n",
        cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),LG1_COMMAND_CAP,
        (unsigned long long)LG1_BUDGET_US);
    lg1_capture_fuses();
    if (lg1_map(&lg1.core,LGS_CORE_PA,LGS_CORE_BYTES)) {
        const struct lgs_io io={NULL,lg1_read32,lg1_read_command,lg1_time,lg1_delay};
        for(unsigned i=0;i<LGS_RAIL_COUNT && !lg1.poisoned;++i) lg1_capture_rail(&io,i);
    }
    lg1_unmap(&lg1.core);
    printf("LITHIUM GPU supply end fuse_valid0=0x%x fuse_valid1=0x%x fuse_stable=0x%x gfx_valid0=0x%x gfx_valid1=0x%x gfx_stable=0x%x gfx_error=%u mx_valid0=0x%x mx_valid1=0x%x mx_stable=0x%x mx_error=%u reads=%u fuse_reads=%u observer_cmd_writes=%u poisoned=%u gpu_writes=0 regulator_writes=0 settled=UNVERIFIED chip_id=UNREAD\n",
        lg1.fuse_valid[0],lg1.fuse_valid[1],lg1.fuse_stable,
        lg1.rail_valid[0][0],lg1.rail_valid[0][1],lg1.rail_stable[0],lg1.rail_error[0],
        lg1.rail_valid[1][0],lg1.rail_valid[1][1],lg1.rail_stable[1],lg1.rail_error[1],
        lg1.reads,lg1.fuse_reads,lg1.writes,(unsigned)lg1.poisoned);
    lithium_touch_cpu_leave(&scope);
}
#endif
