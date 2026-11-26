/* SPDX-License-Identifier: BSD-2-Clause
 * Returned-callback fault injection, never hardware MMIO/SMC/SPMI.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "g1-clock-core.h"
#define ON UINT32_C(0x80000000)
static unsigned long cases, checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
struct event { unsigned kind, reg; uint32_t value; unsigned cleanup; };
enum {EV_DEP,EV_GUARD,EV_READ,EV_WRITE,EV_BARRIER,EV_DELAY,EV_NOW,EV_ID};
struct mock {
    struct lg1c_context *c;
    uint32_t reg[LG1C_REG_COUNT], initial[LG1C_REG_COUNT];
    uint64_t time, dep[LG1C_DEP_COUNT];
    unsigned count, fail, mode, fail_hit, events, idcalls, partial_write;
    unsigned stuck, corrupt_reg, corrupt_kind, corrupt_hit, hw_id, binding;
    uint32_t id, arbitrary;
    int unsupported;
    struct event trace[8192];
};
static unsigned regid(uint32_t address) {
    for (unsigned r=0;r<LG1C_REG_COUNT;r++) if (lg1c_regs[r].address==address) return r;
    CHECK(0); return 0;
}
static int event(struct mock *m,unsigned kind,unsigned reg,uint32_t value) {
    CHECK(m->events<8192);
    m->trace[m->events++]=(struct event){kind,reg,value,m->c->cleanup};
    m->count++; m->time++;
    if (m->fail==m->count) {
        m->fail_hit=1;
        if (m->mode==1) m->time+=250000; /* postaccess budget violation */
        else if (m->mode==2) m->time=0; /* counter rollback */
        else return -1;
    }
    return 0;
}
static int dependency(void *ctx,enum lg1c_dependency d,int acquire,
                       const struct lg1c_request *r,uint64_t *token) {
    struct mock *m=ctx;
    int rc;
    CHECK(r->cpu_hz==614400000 && r->xo_hz==19200000 && r->gpu_hz==133000000);
    CHECK(r->gfx_corner==2 && r->mx_corner==4 && r->gpu_mx_corner==4);
    CHECK(r->bus_master==26 && r->bus_slave==512 && r->bus_index==3);
    CHECK(r->ab_kbytes_s==0 && r->ib_kbytes_s==1600000);
    rc=event(m,EV_DEP,d,(uint32_t)acquire);
    if (acquire) {
        if (m->unsupported==(int)d) return -1;
        for (unsigned n=0;n<d;n++) CHECK(m->dep[n]!=0);
        /* Even a failed partial acquire has a reversible token. */
        m->dep[d]=*token=100+d;
    } else {
        CHECK(*token==m->dep[d] && *token!=0);
        for (unsigned n=d+1;n<LG1C_DEP_COUNT;n++) CHECK(m->dep[n]==0);
        if (!rc) m->dep[d]=*token=0;
    }
    return rc;
}
static int guard(void *ctx,const uint64_t *tokens,int cleanup) {
    struct mock *m=ctx;
    CHECK(cleanup==m->c->cleanup);
    for (unsigned d=0;d<LG1C_DEP_COUNT;d++) CHECK(tokens[d]==m->dep[d] && tokens[d]);
    return event(m,EV_GUARD,0,0);
}
static int read32(void *ctx,uint32_t address,uint32_t *v) {
    struct mock *m=ctx;
    unsigned r=regid(address);
    if (event(m,EV_READ,r,0)) return -1;
    *v=m->reg[r];
    if (m->corrupt_kind && m->corrupt_reg==r && !m->corrupt_hit &&
        m->c->result.phase==LG1C_VERIFY) {
        m->corrupt_hit=1; *v^=m->arbitrary;
    }
    return 0;
}
static void apply_write(struct mock *m,unsigned r,uint32_t v) {
    uint32_t mask=lg1c_regs[r].writable;
    if (r==LG1C_GFX_CMD || r==LG1C_TIMER_CMD) mask|=1;
    CHECK(mask!=0);
    CHECK(((v^m->reg[r])&~mask)==0); /* RMW must preserve all live status bits */
    m->reg[r]=(m->reg[r]&~mask)|(v&mask);
    if (r==LG1C_CX_CTL) {
        m->reg[r]=(m->reg[r]&~ON)|((v&1)?0:ON);
        m->reg[LG1C_CX_STATUS]=(v&1)?0:ON;
        if (m->stuck==1 && !m->c->cleanup) m->reg[LG1C_CX_STATUS]=0;
        if (m->stuck==10 && m->c->cleanup) {
            m->reg[r]|=ON; m->reg[LG1C_CX_STATUS]=ON; /* other firmware vote */
        }
    }
    if (r==LG1C_GX_CTL) {
        if (!(v&1)) {
            CHECK(m->reg[LG1C_CX_STATUS]&ON);
            CHECK((m->reg[LG1C_GFX_CMD]&(ON|2))==2);
            CHECK(!(m->reg[LG1C_CLAMP]&1));
        }
        m->reg[r]=(m->reg[r]&~ON)|((v&1)?0:ON);
        if (m->stuck==2 && !m->c->cleanup) m->reg[r]&=~ON;
        if (m->stuck==8 && m->c->cleanup) m->reg[r]|=ON;
    }
    if (r==LG1C_PLL_MODE) {
        m->reg[r]=(m->reg[r]&~ON)|((v&6)==6?ON:0);
        if (m->stuck==3 && !m->c->cleanup) m->reg[r]&=~ON;
        if (m->stuck==9 && m->c->cleanup) m->reg[r]|=ON;
    }
    if (r==LG1C_GFX_CMD) {
        m->reg[r]&=~1;
        m->reg[r]=(m->reg[r]&~ON)|((v&2)?0:ON);
        if (m->stuck==4 && !m->c->cleanup) m->reg[r]|=ON;
        if (m->stuck==11 && !m->c->cleanup && (v&1)) m->reg[r]|=1;
    }
    if (r==LG1C_TIMER_CMD) {
        m->reg[r]&=~1;
        if (m->stuck==12 && !m->c->cleanup) m->reg[r]|=1;
    }
    if (r==LG1C_GFX_BRANCH || r==LG1C_AHB_BRANCH || r==LG1C_TIMER_BRANCH) {
        m->reg[r]=(m->reg[r]&~ON)|((v&1)?0:ON);
        if (r==LG1C_TIMER_BRANCH) {
            m->reg[LG1C_TIMER_CMD]=(m->reg[LG1C_TIMER_CMD]&~ON)|((v&1)?0:ON);
        }
        if ((m->stuck==5 && r==LG1C_AHB_BRANCH) ||
            (m->stuck==6 && r==LG1C_TIMER_BRANCH) ||
            (m->stuck==7 && r==LG1C_GFX_BRANCH)) {
            if (!m->c->cleanup) m->reg[r]|=ON;
        }
        if (m->stuck==13 && m->c->cleanup && r==LG1C_GFX_BRANCH)
            m->reg[r]&=~ON;
    }
}
static int write32(void *ctx,uint32_t address,uint32_t v) {
    struct mock *m=ctx;
    unsigned r=regid(address);
    int rc=event(m,EV_WRITE,r,v);
    if (!rc || m->partial_write) apply_write(m,r,v);
    return rc;
}
static int barrier(void *ctx) { return event(ctx,EV_BARRIER,0,0); }
static int delay_us(void *ctx,uint32_t us) {
    struct mock *m=ctx;
    int rc=event(m,EV_DELAY,0,us);
    if (!rc) m->time+=us;
    return rc;
}
static uint64_t now_us(void *ctx) {
    struct mock *m=ctx;
    /* A failing clock read cannot signal an int error; returns zero instead. */
    if (event(m,EV_NOW,0,0)) return UINT64_MAX;
    return m->time;
}
static int identify(void *ctx,struct lg1c_identity *id) {
    struct mock *m=ctx;
    m->idcalls++;
    CHECK((m->reg[LG1C_CX_CTL]&(ON|7))==ON && (m->reg[LG1C_CX_STATUS]&ON));
    CHECK((m->reg[LG1C_GX_CTL]&(ON|7))==ON && !(m->reg[LG1C_CLAMP]&1));
    CHECK((m->reg[LG1C_GFX_CMD]&(ON|0xf3))==2);
    CHECK((m->reg[LG1C_GFX_BRANCH]&(ON|0x6003))==0x6001);
    CHECK((m->reg[LG1C_AHB_BRANCH]&(ON|3))==1);
    CHECK((m->reg[LG1C_PLL_MODE]&(ON|7))==(ON|7));
    CHECK((m->reg[LG1C_GFX_CONFIG]&0x371f)==LG1C_GFX_CFG);
    CHECK(m->c->result.clock_readback_valid);
    if (event(m,EV_ID,0,0)) return -1;
    *id=(struct lg1c_identity){0x12345678,m->id,m->binding,m->hw_id};
    return 0;
}
static void reset(struct mock *m,struct lg1c_context *c) {
    memset(c,0,sizeof(*c)); memset(m,0,sizeof(*m)); m->c=c;
    m->time=1000; m->id=LG1C_EXPECTED_DT_CHIP; m->hw_id=1; m->binding=1;
    m->unsupported=-1;
    m->reg[LG1C_CX_CTL]=m->reg[LG1C_GX_CTL]=0x00222001;
    m->reg[LG1C_CLAMP]=1;
    m->reg[LG1C_GFX_CMD]=ON; m->reg[LG1C_TIMER_CMD]=ON;
    m->reg[LG1C_GFX_BRANCH]=m->reg[LG1C_AHB_BRANCH]=m->reg[LG1C_TIMER_BRANCH]=ON;
    m->reg[LG1C_ISENSE_BRANCH]=ON;
    /* Inherited analog context: intentionally non-Linux invented values. */
    m->reg[LG1C_PLL_USER_HI]=0xdeadbeef;
    m->reg[LG1C_PLL_CONFIG]=0x12345678;
    m->reg[LG1C_PLL_TEST_LO]=0x5a5a5a5a;
    m->reg[LG1C_PLL_TEST_HI]=0xa5a5a5a5;
    memcpy(m->initial,m->reg,sizeof(m->reg));
}
static struct lg1c_ops ops(struct mock *m) {
    struct lg1c_ops o={m,dependency,guard,read32,write32,barrier,delay_us,now_us,identify};
    return o;
}
static struct lg1c_config config(void) {
    struct lg1c_config c={1,LG1C_CPU_HZ,200000,200000}; return c;
}
static enum lg1c_status run(struct mock *m) {
    struct lg1c_ops o=ops(m); struct lg1c_config cfg=config();
    enum lg1c_status s=lg1c_prepare(m->c,&o,&cfg);
    return s ? s : lg1c_run(m->c);
}
static void restored(struct mock *m) {
    CHECK(m->c->result.cleanup_complete);
    CHECK(!m->c->result.cleanup_error && !m->c->result.retained_dependencies);
    for (unsigned r=0;r<LG1C_REG_COUNT;r++)
        CHECK((m->reg[r]&lg1c_regs[r].writable)==(m->initial[r]&lg1c_regs[r].writable));
    for (unsigned d=0;d<LG1C_DEP_COUNT;d++) CHECK(m->dep[d]==0);
}
static unsigned successful(struct mock *m,struct lg1c_context *c) {
    unsigned events;
    cases++; reset(m,c);
    enum lg1c_status status=run(m);
    if (status) fprintf(stderr,"status=%u cleanup=%u phase=%u addr=%x raw=%x calls=%u\n",
                         status,c->result.cleanup_error,c->result.failure_phase,
                         c->result.last_address,c->result.last_raw,m->count);
    CHECK(status==LG1C_OK); restored(m);
    CHECK(c->result.clock_readback_valid && c->result.configured_hz==133000000);
    CHECK(c->result.chip_id_valid && c->result.chip_id==0x05030004 && m->idcalls==1);
    CHECK(lg1c_run(c)==LG1C_STATE);
    events=m->count;
    /* Exact PLL fields and source-derived enable sequence, no BCR/GPU writes. */
    unsigned modes=0, ids=0, acq=0, rel=0;
    static const uint32_t mode[]={2,6,7,6,0};
    for (unsigned n=0;n<m->events;n++) {
        struct event *e=&m->trace[n];
        if (e->kind==EV_WRITE) {
            CHECK(lg1c_regs[e->reg].address<0xb00000);
            CHECK(e->reg!=LG1C_BCR && e->reg!=LG1C_ISENSE_BRANCH);
            CHECK(e->reg<LG1C_PLL_USER_HI);
            if (e->reg==LG1C_PLL_MODE) { CHECK(modes<5 && (e->value&7)==mode[modes]); modes++; }
            if (!e->cleanup && e->reg==LG1C_PLL_L_REG) CHECK(e->value==27);
            if (!e->cleanup && e->reg==LG1C_PLL_A_LO) CHECK(e->value==0x55555600);
            if (!e->cleanup && e->reg==LG1C_PLL_A_HI) CHECK(e->value==0xb5);
            if (!e->cleanup && e->reg==LG1C_PLL_USER_REG) CHECK(e->value==LG1C_PLL_USER);
        }
        if (e->kind==EV_ID) { CHECK(!e->cleanup); ids++; }
        if (e->kind==EV_DEP) {
            if (e->value) { CHECK(e->reg==acq); acq++; }
            else { CHECK(e->reg==2-rel); rel++; }
        }
    }
    CHECK(modes==5 && ids==1 && acq==3 && rel==3);
    return events;
}
static void admission(void) {
    struct mock m; struct lg1c_context c; struct lg1c_ops o; struct lg1c_config cfg;
    cases++; reset(&m,&c); o=ops(&m); cfg=config(); cfg.enabled=0;
    CHECK(lg1c_prepare(&c,&o,&cfg)==LG1C_DISABLED && m.count==0);
    CHECK(lg1c_run(&c)==LG1C_STATE && m.count==0);
    for (unsigned n=0;n<12;n++) {
        cases++; reset(&m,&c); o=ops(&m); cfg=config();
        switch(n) {
        case 0:cfg.enabled=2;break; case 1:cfg.cpu_hz=691200000;break;
        case 2:cfg.run_budget_us=999;break; case 3:cfg.cleanup_budget_us=200001;break;
        case 4:o.dependency=NULL;break; case 5:o.guard=NULL;break;
        case 6:o.read32=NULL;break; case 7:o.write32=NULL;break;
        case 8:o.barrier=NULL;break; case 9:o.delay_us=NULL;break;
        case 10:o.now_us=NULL;break; default:cfg.run_budget_us=200001;break;
        }
        CHECK(lg1c_prepare(&c,&o,&cfg)==LG1C_ARGUMENT && !m.count);
    }
    for (unsigned d=0;d<3;d++) {
        cases++; reset(&m,&c); m.unsupported=(int)d;
        CHECK(run(&m)==LG1C_DEPENDENCY && !c.result.writes && !m.idcalls);
        CHECK(c.result.cleanup_complete);
    }
    /* Cold-state refusal: no write resets or stops a live consumer. */
    static const unsigned regs[]={LG1C_CX_CTL,LG1C_CX_STATUS,LG1C_GX_CTL,LG1C_CLAMP,
        LG1C_BCR,LG1C_PLL_MODE,LG1C_GFX_CMD,LG1C_TIMER_CMD,
        LG1C_GFX_CONFIG,LG1C_GFX_BRANCH,LG1C_AHB_BRANCH,LG1C_TIMER_BRANCH,LG1C_ISENSE_BRANCH};
    static const uint32_t bad[]={ON,ON,2,0x10,1,0x100000,2,1,0x300,1,2,1,2};
    for (unsigned n=0;n<sizeof(regs)/sizeof(regs[0]);n++) {
        cases++; reset(&m,&c); m.reg[regs[n]]=bad[n];
        CHECK(run(&m)==LG1C_HANDOFF && !c.result.writes && !m.idcalls);
        CHECK(c.result.cleanup_complete);
    }
}
static void inject(unsigned cap) {
    struct mock *m=malloc(sizeof(*m)); struct lg1c_context c;
    CHECK(m!=NULL);
    for (unsigned mode=0;mode<3;mode++) for (unsigned n=1;n<=cap;n++) {
        cases++; reset(m,&c); m->mode=mode; m->fail=n;
        enum lg1c_status s=run(m);
        CHECK(m->fail_hit);
        /* A time jump before a budget's origin is not an elapsed-time fault. */
        if (mode==1 && s==LG1C_OK) { restored(m); continue; }
        CHECK(s!=LG1C_OK);
        CHECK(m->idcalls<=1);
        CHECK(lg1c_run(&c)==LG1C_STATE);
        CHECK(c.result.error || c.result.cleanup_error);
        if (c.result.cleanup_complete) restored(m);
        else CHECK(c.result.retained_dependencies!=0 || c.result.cleanup_error);
    }
    /* A write can succeed physically and still return an error. All successful
     * reverse cleanups must restore both the old and partially-applied cases. */
    for (unsigned n=1;n<=cap;n++) {
        cases++; reset(m,&c); m->fail=n; m->partial_write=1;
        CHECK(run(m)!=LG1C_OK && m->fail_hit);
        if (c.result.cleanup_complete) restored(m);
    }
    free(m);
}
static void status_and_id(void) {
    struct mock m; struct lg1c_context c;
    for (unsigned stuck=1;stuck<=13;stuck++) {
        cases++; reset(&m,&c); m.stuck=stuck;
        enum lg1c_status s=run(&m);
        if (stuck==10) { CHECK(s==LG1C_OK); restored(&m); }
        else {
            CHECK(s!=LG1C_OK);
            CHECK(c.result.error==LG1C_TIMEOUT || c.result.cleanup_error==LG1C_TIMEOUT ||
                  c.result.cleanup_error==LG1C_HANDOFF);
            if (c.result.cleanup_complete) restored(&m);
            else CHECK(c.result.retained_dependencies==7);
            if (stuck<8 || stuck==11 || stuck==12) CHECK(m.idcalls==0);
        }
    }
    for (unsigned kind=0;kind<4;kind++) {
        cases++; reset(&m,&c);
        struct lg1c_ops o=ops(&m); struct lg1c_config cfg=config();
        if (kind==0) o.identify=NULL;
        if (kind==1) m.hw_id=0;
        if (kind==2) m.binding=0;
        if (kind==3) m.id=0x05030000;
        CHECK(lg1c_prepare(&c,&o,&cfg)==LG1C_OK);
        CHECK(lg1c_run(&c)==(kind==3?LG1C_ID_MISMATCH:LG1C_ID_UNBOUND));
        CHECK(c.result.clock_readback_valid && !c.result.chip_id_valid);
        restored(&m);
    }
    /* Fresh readback catches every controlled clock/power field changed before
     * identity. It does not ask to prove every future TZ access impossible. */
    static const unsigned regs[]={LG1C_CX_CTL,LG1C_CX_STATUS,LG1C_GX_CTL,LG1C_CLAMP,
        LG1C_BCR,LG1C_GFX_CMD,LG1C_GFX_CONFIG,LG1C_PLL_MODE,LG1C_PLL_L_REG,
        LG1C_PLL_A_LO,LG1C_PLL_A_HI,LG1C_PLL_USER_REG,LG1C_AHB_BRANCH,
        LG1C_TIMER_CONFIG,LG1C_TIMER_CMD,LG1C_TIMER_BRANCH,LG1C_GFX_BRANCH,
        LG1C_PLL_USER_HI,LG1C_PLL_CONFIG,LG1C_PLL_TEST_LO,LG1C_PLL_TEST_HI};
    for (unsigned n=0;n<sizeof(regs)/sizeof(regs[0]);n++) {
        cases++; reset(&m,&c); m.corrupt_kind=1; m.corrupt_reg=regs[n];
        m.arbitrary=(regs[n]==LG1C_CX_STATUS)?ON:1;
        CHECK(run(&m)==LG1C_READBACK && m.corrupt_hit && !m.idcalls);
        restored(&m);
    }
}
int main(void) {
    struct mock *m=malloc(sizeof(*m)); struct lg1c_context c;
    CHECK(m!=NULL);
    unsigned cap=successful(m,&c); free(m);
    admission(); inject(cap); status_and_id();
    printf("PASS g1-clock133: %lu cases, %lu checks; %u callback positions injected; no hardware\n",cases,checks,cap);
    return 0;
}
