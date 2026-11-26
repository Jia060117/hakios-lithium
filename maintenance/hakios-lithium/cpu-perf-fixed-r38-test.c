/* Host-only mock test. PERF_FIXED_HEADER may select the evidence draft. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef PERF_FIXED_HEADER
#define PERF_FIXED_HEADER "../../ports/hakios-lithium/diagnostics/cpu-perf-fixed-r38.h"
#endif
#include PERF_FIXED_HEADER
/* Keep the frozen API1 draft reproducible while exercising current API2's
 * precise non-ACD-path condition against the production header. */
#if R38_PERF_FIXED_API >= 2
#define VERIFIED_PATH_FIELD non_acd_path_verified
#else
#define VERIFIED_PATH_FIELD acd_disabled_verified
#endif
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned checks;
struct mock {
    uint32_t reg[R38_PERF_REGS], offsets[16], values[16];
    unsigned writes, reads, logs, lock, bad_write, error_write, read_error;
    unsigned frozen, backwards, drift_on_write;
    uint64_t us, fsm_at;
};
static int index_of(uint32_t offset)
{
    for (unsigned i=0;i<R38_PERF_REGS;++i)
        if (r38_perf_offsets[i]==offset) return (int)i;
    CHECK(0); return -1;
}
static int rd(void *ctx,uint32_t off,uint32_t *v)
{
    struct mock *m=ctx;
    int i=index_of(off);
    ++m->reads;
    if (m->read_error) return -1;
    *v=m->reg[i];
    if (i==R38_P_MODE && *v==R38_PERF_FIXED_FSM &&
        m->lock && m->us>=m->fsm_at+50)
        *v |= R38_PERF_FIXED_LOCK|7u;
    return 0;
}
static int wr(void *ctx,uint32_t off,uint32_t v)
{
    struct mock *m=ctx;
    int i=index_of(off);
    CHECK(m->writes<16);
    m->offsets[m->writes]=off; m->values[m->writes]=v; ++m->writes;
    m->reg[i]=v;
    if (i==R38_P_MODE && v==R38_PERF_FIXED_FSM) m->fsm_at=m->us;
    if (m->bad_write==m->writes) m->reg[i]^=0x1000u;
    if (m->drift_on_write==m->writes) m->reg[R38_CLKCTL]^=0x100u;
    return m->error_write==m->writes ? -1:0;
}
static int delay(void *ctx,uint32_t us)
{
    struct mock *m=ctx;
    if (m->backwards) m->us-=us;
    else if (!m->frozen) m->us+=us;
    return 0;
}
static int time_us(void *ctx,uint64_t *us)
{ *us=((struct mock *)ctx)->us; return 0; }
static void log_write(void *ctx,const struct r38_perf_write_log *e)
{ struct mock *m=ctx; CHECK(e->offset==m->offsets[m->logs]); ++m->logs; }
static struct r38_perf_authorization auth(void)
{
    struct r38_perf_authorization a={0};
    a.power_verified=a.pro_verified=a.bin_revision_verified=a.snapshot_verified=1;
    a.exclusive_owner=a.perf_cores_quiesced=a.executing_on_pwr=1;
    a.VERIFIED_PATH_FIELD=1;
    a.approved_bin=a.observed_bin=1;
    a.approved_revision=a.observed_revision=4;
    a.generation=a.power_generation=a.snapshot_generation=123;
    return a;
}
static struct r38_perf_snapshot setup(struct mock *m)
{
    const uint32_t raw[R38_PERF_REGS]={
        0x01000000u,0,0,8,0x200d4828u,6,0x1c000000u,0x4000u,
        0x01000000u,0,0x71u
    };
    struct r38_perf_snapshot s={0};
    memset(m,0,sizeof(*m)); m->lock=1; m->us=1000;
    memcpy(m->reg,raw,sizeof(raw)); memcpy(s.raw,raw,sizeof(raw));
    s.valid=R38_PERF_VALID_ALL; return s;
}
static enum r38_perf_status run(struct mock *m,struct r38_perf_authorization *a,
 struct r38_perf_snapshot *s,struct r38_perf_fixed_result *r)
{
    const struct r38_perf_fixed_io io={m,rd,wr,delay,time_us,log_write};
    enum r38_perf_status result=r38_perf_fixed_run(&io,a,s,r);
    CHECK(m->writes==r->writes); CHECK(m->logs==m->writes);
    return result;
}
int main(void)
{
    struct mock m; struct r38_perf_fixed_result r;
    struct r38_perf_snapshot s; struct r38_perf_authorization a;
    const unsigned offsets[11]={0,0x10,0,8,0x18,0x1c,0x20,0x24,4,0,0x40};
    const uint32_t values[11]={0,0x109,0,0,0x200d4828,6,0x1c000000,0x4000,
                               32,0x00118000,4};
    s=setup(&m); a=auth();
    CHECK(run(&m,&a,&s,&r)==R38_PERF_PROGRAMMED);
    CHECK(m.writes==11 && r.readbacks==11 && m.us>=1056);
    CHECK(r.programmed_primary_hz==614400000 && r.programmed_main_hz==307200000);
    CHECK(r.phase_writes[R38_PERF_INITIALIZE]==7);
    for (unsigned i=0;i<11;++i) {
        CHECK(m.offsets[i]==offsets[i]); CHECK(m.values[i]==values[i]);
        CHECK(r.log[i].read_valid && !r.log[i].write_rc);
    }
    puts("PASS legal Pro integer/FSM sequence, all 11 readbacks and per-phase logs");
    for (unsigned gate=0;gate<14;++gate) {
        s=setup(&m); a=auth();
        switch(gate) {
        case 0:a.power_verified=0;break; case 1:a.pro_verified=0;break;
        case 2:a.bin_revision_verified=0;break;case 3:a.snapshot_verified=0;break;
        case 4:a.exclusive_owner=0;break;case 5:a.perf_cores_quiesced=0;break;
        case 6:a.executing_on_pwr=0;break;case 7:a.VERIFIED_PATH_FIELD=0;break;
        case 8:a.observed_bin=2;break;case 9:a.observed_revision=3;break;
        case 10:a.power_generation=122;break;case 11:a.snapshot_generation=122;break;
        case 12:a.generation=0;break;default:s.valid=0;break;
        }
        CHECK(run(&m,&a,&s,&r)==R38_PERF_UNKNOWN); CHECK(!m.writes);
    }
    puts("PASS missing authorization / bin / revision / generation: zero writes");
    for (unsigned i=0;i<R38_PERF_REGS;++i) {
        s=setup(&m);a=auth();m.reg[i]^=1u;
        CHECK(run(&m,&a,&s,&r)==R38_PERF_UNKNOWN);CHECK(!m.writes);
        s=setup(&m);a=auth();s.raw[i]^=1u;
        CHECK(run(&m,&a,&s,&r)==R38_PERF_UNKNOWN);CHECK(!m.writes);
    }
    puts("PASS every unexpected snapshot field and live read mismatch: zero writes");
    for (unsigned i=1;i<=11;++i) {
        s=setup(&m);a=auth();m.bad_write=i;
        CHECK(run(&m,&a,&s,&r)==R38_PERF_READBACK_ERROR);
        CHECK(m.writes==i && r.readbacks==i);
        CHECK(r.mux_write_attempted==(i==11));
        if (i<11) CHECK(r.mux_xo_confirmed && m.reg[R38_MUX]==0);
        else CHECK(!r.mux_xo_confirmed);
    }
    puts("PASS every write readback mismatch: stop exactly there, no rollback");
    s=setup(&m);a=auth();m.lock=0;
    CHECK(run(&m,&a,&s,&r)==R38_PERF_LOCK_TIMEOUT);
    CHECK(m.writes==10 && r.mux_xo_confirmed && !r.mux_write_attempted);
    CHECK(m.us<=1251);
    puts("PASS bounded lock timeout leaves mux XO and does not disable/retry PLL");
    s=setup(&m);a=auth();m.error_write=5;
    CHECK(run(&m,&a,&s,&r)==R38_PERF_WRITE_ERROR);
    CHECK(m.writes==5 && r.readbacks==5);
    s=setup(&m);a=auth();m.read_error=1;
    CHECK(run(&m,&a,&s,&r)==R38_PERF_READ_ERROR);CHECK(!m.writes);
    s=setup(&m);a=auth();m.frozen=1;
    CHECK(run(&m,&a,&s,&r)==R38_PERF_TIME_ERROR);CHECK(!m.writes);
    s=setup(&m);a=auth();m.backwards=1;
    CHECK(run(&m,&a,&s,&r)==R38_PERF_TIME_ERROR);CHECK(!m.writes);
    s=setup(&m);a=auth();m.drift_on_write=4;
    CHECK(run(&m,&a,&s,&r)==R38_PERF_UNKNOWN);
    CHECK(m.writes==4 && !r.mux_xo_confirmed);
    puts("PASS callback errors, unusable clocks, concurrent AUTO change");
    printf("PASS %u checks; mock only; no hardware calls\n",checks);
    return 0;
}
