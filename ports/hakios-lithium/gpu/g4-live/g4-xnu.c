/* SPDX-License-Identifier: GPL-2.0-only
 * Real XNU APIs + native arm64 loads/stores. Compile using active XNU flags.
 * No host substitute selected by the production implementation.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/cpu_number.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <mach/vm_statistics.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include "g4-xnu.h"
extern vm_offset_t ml_io_map_unmappable(vm_offset_t,vm_size_t,unsigned int);

static int xcpu(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1":"=r"(mpidr));
    return cpu_number()!=0 || (mpidr&UINT64_C(0xff00ffffff))!=1u;
}

static int xguard(void *opaque,enum g4l_requirement r,uint64_t cookie)
{
    struct g4lx_native *s=opaque;
    if(!s || !s->ready || cookie!=s->window_cookie ||
       ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
       !preemption_enabled() || xcpu())return -1;
    return s->provider.guard(s->provider.cookie,r,cookie);
}
static int xacquire(void *opaque,enum g4l_role r,size_t n,size_t a,unsigned p,struct g4l_bo *b)
{
    struct g4lx_native *s=opaque;
    if(xguard(s,G4L_HOST_PREPARE,s->window_cookie))return -1;
    return s->provider.acquire(s->provider.cookie,r,n,a,p,b);
}
static void xrelease(void *opaque,enum g4l_role r,const struct g4l_bo *b)
{
    struct g4lx_native *s=opaque;
    s->provider.release(s->provider.cookie,r,b);
}
static int xsync(void *opaque,const struct g4l_bo *b,size_t at,size_t n,enum g4l_sync d)
{
    struct g4lx_native *s=opaque;
    if(at>b->bytes || n>b->bytes-at ||
       xguard(s,G4L_HOST_PREPARE,s->window_cookie))return -1;
    int rc=s->provider.sync(s->provider.cookie,b,at,n,d);
    __asm__ volatile("dsb sy":::"memory");
    return rc ? rc : xguard(s,G4L_HOST_PREPARE,s->window_cookie);
}
static int xsample(void *opaque,const struct g4l_bo *b,size_t at,uint32_t *v)
{
    struct g4lx_native *s=opaque;
    if(!v || !b || !b->cpu || at>b->cpu_bytes || b->cpu_bytes-at<4u ||
       (((uintptr_t)b->cpu+at)&3u) || xguard(s,G4L_LIVE_WINDOW,s->window_cookie))return -1;
    /* One aligned native load, not four byte reads or a memcpy. Cache
     * completion is the provider's preceding FROM_DEVICE sync obligation. */
    *v=*(volatile const uint32_t *)(b->cpu+at);
    __asm__ volatile("dmb oshld":::"memory");
    return xguard(s,G4L_LIVE_WINDOW,s->window_cookie);
}
static int xread(void *opaque,uint32_t r,uint32_t *v)
{
    struct g4lx_native *s=opaque;
    s->last_reg=r;s->io_executed=0;s->io_write=0;
    if(!v || !g4l_readable_reg(r) || r>=G4L_GPU_RESOURCE_BYTES/4u ||
       !s->mmio[r>>12] ||
       xguard(s,G4L_LIVE_WINDOW,s->window_cookie))return s->last_io_status=-1;
    __asm__ volatile("dsb sy":::"memory");
    *v=s->mmio[r>>12][r&4095u];
    s->last_value=*v;s->io_executed=1;
    __asm__ volatile("dsb sy":::"memory");
    return s->last_io_status=xguard(s,G4L_LIVE_WINDOW,s->window_cookie);
}
static int xwrite(void *opaque,uint32_t r,uint32_t v)
{
    struct g4lx_native *s=opaque;
    s->last_reg=r;s->last_value=v;s->io_executed=0;s->io_write=1;
    if(!g4l_writable_reg(r) || r>=G4L_GPU_RESOURCE_BYTES/4u ||
       !s->mmio[r>>12] ||
       xguard(s,G4L_LIVE_WINDOW,s->window_cookie))return s->last_io_status=-1;
    __asm__ volatile("dsb sy":::"memory");
    s->mmio[r>>12][r&4095u]=v;
    s->io_executed=1;
    __asm__ volatile("dsb sy":::"memory");
    return s->last_io_status=xguard(s,G4L_LIVE_WINDOW,s->window_cookie);
}
static int xbarrier(void *opaque)
{
    struct g4lx_native *s=opaque;
    if(xguard(s,G4L_LIVE_WINDOW,s->window_cookie))return -1;
    __asm__ volatile("dsb sy":::"memory");return 0;
}
static int xnow(void *opaque,uint64_t *n)
{
    struct g4lx_native *s=opaque;
    if(!n || xguard(s,G4L_HOST_PREPARE,s->window_cookie))return -1;
    absolutetime_to_nanoseconds(mach_absolute_time(),n);return 0;
}
static void xrelax(void *opaque)
{ (void)opaque;__asm__ volatile("yield":::"memory"); }
static int xstopped(void *opaque,uint64_t cookie)
{
    struct g4lx_native *s=opaque;
    if(xguard(s,G4L_HOST_PREPARE,cookie))return -1;
    return s->provider.dma_stopped(s->provider.cookie,cookie);
}
enum g4l_status g4lx_bind(struct g4lx_native *s,unsigned enabled,uint64_t cookie,
                        const struct g4lx_provider *p,struct g4l_ops *out)
{
    if(!enabled)return G4L_DISABLED;
    if(!s || !p || !out || !cookie || !p->guard || !p->acquire || !p->release ||
       !p->sync || !p->dma_stopped)return G4L_ARGUMENT;
    if(s->used)return G4L_USED;
    s->used=1;s->provider=*p;s->window_cookie=cookie;
    if(PAGE_SIZE!=G4L_MMIO_BYTES || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || !preemption_enabled() || xcpu() ||
       p->guard(p->cookie,G4L_HOST_PREPARE,cookie))return G4L_PREREQUISITE;
    const unsigned slots[4]={0,3,10,15};
    for(unsigned i=0;i<4u;++i) {
        unsigned slot=slots[i];
        if(p->guard(p->cookie,G4L_HOST_PREPARE,cookie))return G4L_PREREQUISITE;
        vm_offset_t va=ml_io_map_unmappable((vm_offset_t)(G4L_GPU_PA+(uint64_t)slot*G4L_MMIO_BYTES),
                                          G4L_MMIO_BYTES,VM_WIMG_IO);
        if(!va)return G4L_CALLBACK;
        s->mmio[slot]=(volatile uint32_t *)va;
    }
    s->ready=1;
    *out=(struct g4l_ops){s,xguard,xacquire,xrelease,xsync,xsample,xread,xwrite,
                          xbarrier,xnow,xrelax,xstopped};
    return G4L_OK;
}
static int xstopguard(void *opaque,uint64_t cookie)
{ return xguard(opaque,G4L_STOP_WINDOW,cookie); }
static int xstopread(void *opaque,uint32_t r,uint32_t *v)
{
    struct g4lx_native *s=opaque;
    s->last_reg=r;s->io_executed=0;s->io_write=0;
    if(!v || !g4l_stop_readable(r) || r>=G4L_GPU_RESOURCE_BYTES/4u ||
       !s->mmio[r>>12] || xstopguard(s,s->window_cookie))return s->last_io_status=-1;
    __asm__ volatile("dsb sy":::"memory");
    *v=s->mmio[r>>12][r&4095u];s->last_value=*v;s->io_executed=1u;
    __asm__ volatile("dsb sy":::"memory");
    return s->last_io_status=xstopguard(s,s->window_cookie);
}
static int xstopwrite(void *opaque,uint32_t r,uint32_t v)
{
    struct g4lx_native *s=opaque;
    s->last_reg=r;s->last_value=v;s->io_executed=0;s->io_write=1u;
    if(!g4l_stop_writable(r,v) || r>=G4L_GPU_RESOURCE_BYTES/4u ||
       !s->mmio[r>>12] || xstopguard(s,s->window_cookie))return s->last_io_status=-1;
    __asm__ volatile("dsb sy":::"memory");
    s->mmio[r>>12][r&4095u]=v;s->io_executed=1u;
    __asm__ volatile("dsb sy":::"memory");
    return s->last_io_status=xstopguard(s,s->window_cookie);
}
static int xstopbarrier(void *opaque)
{
    struct g4lx_native *s=opaque;
    if(xstopguard(s,s->window_cookie))return -1;
    __asm__ volatile("dsb sy":::"memory");return 0;
}
enum g4l_status g4lx_bind_stop(struct g4lx_native *s,struct g4l_stop_ops *o)
{
    if(!s || !s->ready || !o)return G4L_ARGUMENT;
    *o=(struct g4l_stop_ops){s,xstopguard,xstopread,xstopwrite,xstopbarrier,xnow,xrelax};
    return G4L_OK;
}
void g4lx_log(const struct g4lx_native *s,const struct g4l_context *c)
{
    if(!s || !c)return;
    printf("LITHIUM GPU g4 profile=0x%x source=dt-catalog hardware_chip_id_measured=0\n",c->cfg.chip_id);
    printf("LITHIUM GPU g4 end state=%u error=%u me=%u stat=%u secure=%u fence=%u hold=%u writes=%u reads=%u polls=%u pubs=%u\n",
        (unsigned)c->state,(unsigned)c->error,c->me_completed,c->stat_completed,
        c->secure_completed,c->fence_observed,c->hold_dependencies,c->writes,c->reads,c->polls,c->publications);
    printf("LITHIUM GPU g4 baseline boot=0x%llx window=0x%llx aspace=0x%llx gen=0x%llx complete=%u start_ns=%llu end_ns=%llu\n",
        (unsigned long long)c->baseline.boot_cookie,(unsigned long long)c->baseline.window_cookie,
        (unsigned long long)c->baseline.address_space,(unsigned long long)c->baseline.generation,
        c->baseline.complete,(unsigned long long)c->baseline.start_ns,(unsigned long long)c->baseline.end_ns);
    const struct g4l_pas_auth *a=&c->baseline.auth;
    printf("LITHIUM GPU g4 auth fid=0x%llx pas=%llu cb=%d x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx\n",
        (unsigned long long)a->function_id,(unsigned long long)a->pas_id,a->callback_status,
        (unsigned long long)a->x[0],(unsigned long long)a->x[1],
        (unsigned long long)a->x[2],(unsigned long long)a->x[3]);
    for(unsigned i=0;i<c->baseline.count && i<G4L_BASELINE_REGS;++i) {
        const struct g4l_sample *v=&c->baseline.sample[i];
        printf("LITHIUM GPU g4 initial i=%u reg=0x%x valid=%u raw0=0x%x raw1=0x%x cb0=%d cb1=%d\n",
            i,v->reg,v->valid,v->value[0],v->value[1],v->callback_status[0],v->callback_status[1]);
    }
    for(unsigned i=0;i<c->trace_count && i<G4L_MAX_STORES;++i) {
        const struct g4l_write *v=&c->trace[i];
        printf("LITHIUM GPU g4 store i=%u reg=0x%x value=0x%x attempted=%u before_valid=%u before=0x%x after_valid=%u after=0x%x cb=%d\n",
            i,v->reg,v->value,v->attempted,v->before_valid,v->before,v->after_valid,v->after,v->callback_status);
    }
    for(unsigned i=0;i<G4L_PHASES;++i) {
        const struct g4l_observation *v=&c->phase[i];
        printf("LITHIUM GPU g4 phase i=%u valid=0x%x rptr=%u wptr=%u status=0x%x status3=0x%x irq=0x%x cpirq=0x%x at_ns=%llu\n",
            i,v->valid,v->rptr,v->wptr,v->status,v->status3,v->irq,v->cp_irq,(unsigned long long)v->at_ns);
    }
    printf("LITHIUM GPU g4 native reg=0x%x value=0x%x executed=%u write=%u cb=%d\n",
        s->last_reg,s->last_value,s->io_executed,s->io_write,s->last_io_status);
    const struct g4l_stop_result *t=&c->stop;
    printf("LITHIUM GPU g4 stop used=%u complete=%u error=%u bootstrap_error=%u attempts=%u polls=%u halt_held=%u ack_valid=%u ack=0x%x\n",
        t->used,t->complete,(unsigned)t->error,(unsigned)t->bootstrap_error,
        t->attempts,t->polls,t->halt_held,t->ack_valid,t->ack_after);
    printf("LITHIUM GPU g4 stop_ack initial=0x%x phases_valid=0x%x drain=0x%x clear=0x%x held=0x%x start_ns=%llu end_ns=%llu\n",
        t->ack_before,t->ack_phase_valid,t->ack_value[0],t->ack_value[1],t->ack_value[2],
        (unsigned long long)t->start_ns,(unsigned long long)t->end_ns);
    for(unsigned i=0;i<t->attempts && i<G4L_STOP_STORES;++i) {
        const struct g4l_write *v=&t->store[i];
        printf("LITHIUM GPU g4 stop_store i=%u reg=0x%x value=0x%x attempted=%u before_valid=%u before=0x%x after_valid=%u after=0x%x cb=%d\n",
            i,v->reg,v->value,v->attempted,v->before_valid,v->before,v->after_valid,v->after,v->callback_status);
    }
    for(unsigned i=0;i<t->cold_count && i<G4L_STOP_COLD_REGS;++i) {
        const struct g4l_sample *v=&t->cold[i];
        printf("LITHIUM GPU g4 stop_cold i=%u reg=0x%x valid=%u raw0=0x%x raw1=0x%x cb0=%d cb1=%d\n",
            i,v->reg,v->valid,v->value[0],v->value[1],v->callback_status[0],v->callback_status[1]);
    }
}
