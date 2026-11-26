/* SPDX-License-Identifier: BSD-2-Clause -- actual production helper. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../ports/hakios-lithium/gpu/g2-live/scm-continuation.h"
static unsigned checks,cases;
#define CHECK(x) do { ++checks;assert(x); } while(0)
struct mock {
    uint64_t original[8],now,tick,final_x0,final_x1;
    unsigned calls,interrupts,guards,fail_guard,fail_raw,backwards,late;
};
static int guard(void *p)
{ struct mock *m=p;return ++m->guards==m->fail_guard; }
static uint64_t now_ns(void *p)
{
    struct mock *m=p;
    if(m->backwards&&m->calls)return 0;
    if(m->late&&m->calls)return LG2S_MAX_NS+1u;
    return m->now+=m->tick;
}
static int raw(void *p,uint64_t regs[8])
{
    struct mock *m=p;unsigned n=m->calls++;
    CHECK(regs[0]==(n?1u:m->original[0]));
    for(unsigned j=1;j<8u;++j) {
        uint64_t expected=j==6u&&n?UINT64_C(0x61900002)+n:m->original[j];
        CHECK(regs[j]==expected);
    }
    /* These mutations MUST NOT replace original arguments on continuation. */
    for(unsigned j=1;j<8u;++j)regs[j]=UINT64_C(0xfedcba0000000000)+j+n;
    regs[0]=n<m->interrupts?1u:m->final_x0;
    regs[1]=n<m->interrupts?0x82u:m->final_x1;
    regs[6]=UINT64_C(0x61900003)+n;
    return m->fail_raw==n+1u?-91:0;
}
static struct mock fresh(void)
{
    struct mock m={.original={0x42000201u,0x82u,13u,UINT64_C(0x1b8000000),0,0,0,0},
        .tick=1};return m;
}
static enum lg2s_status run(struct mock *m,struct lg2s_result *t,uint64_t regs[8])
{
    memcpy(regs,m->original,sizeof(m->original));
    const struct lg2s_ops o={m,guard,now_ns,raw};
    return lg2s_run(regs,t,&o);
}
static void completed(unsigned interrupted,uint64_t final0,uint64_t final1)
{
    ++cases;struct mock m=fresh();m.interrupts=interrupted;m.final_x0=final0;m.final_x1=final1;
    struct lg2s_result t={0};uint64_t regs[8];
    CHECK(run(&m,&t,regs)==LG2S_OK);
    CHECK(t.complete==1u&&t.steps==interrupted+1u&&t.resumes==interrupted);
    CHECK(m.calls==interrupted+1u&&regs[0]==final0&&regs[1]==final1);
    CHECK(t.used==1u&&t.error==LG2S_OK);
    for(unsigned k=0;k<t.steps;++k) {
        CHECK(t.step[k].executed==1u&&t.step[k].after_valid==1u);
        CHECK(t.step[k].callback_status==0&&t.step[k].after_ns>=t.step[k].before_ns);
        CHECK(t.step[k].out[0]==(k<interrupted?1u:final0));
        CHECK(t.step[k].out[6]==UINT64_C(0x61900003)+k);
    }
    unsigned calls=m.calls;
    CHECK(run(&m,&t,regs)==LG2S_USED&&m.calls==calls);
}
int main(void)
{
    /* No interrupt: exactly the original single native SMC, including errors.
     * -12 BUSY is a final result; it never restarts INIT. */
    completed(0,0,0);completed(0,(uint64_t)-12,0);completed(0,(uint64_t)-1,0);
    completed(0,0,5);completed(1,0,0);completed(3,0,0);completed(63,0,0);
    completed(1,(uint64_t)-12,0);completed(2,0,9);
    for(unsigned fail=1;fail<=5u;++fail) {
        ++cases;struct mock m=fresh();m.interrupts=3;m.fail_guard=fail;
        struct lg2s_result t={0};uint64_t regs[8];
        CHECK(run(&m,&t,regs)==LG2S_CONTEXT);CHECK(!t.complete);
        CHECK(t.steps==m.calls&&m.calls<=2u);
        if(m.calls)CHECK(t.step[m.calls-1u].out[0]==1u);
    }
    for(unsigned failed=1;failed<=3u;++failed) {
        ++cases;struct mock m=fresh();m.interrupts=3;m.fail_raw=failed;
        struct lg2s_result t={0};uint64_t regs[8];
        CHECK(run(&m,&t,regs)==LG2S_CALLBACK);CHECK(t.steps==failed&&!t.complete);
        CHECK(t.step[failed-1u].executed==1u&&t.step[failed-1u].callback_status==-91);
    }
    {
        ++cases;struct mock m=fresh();m.interrupts=64;
        struct lg2s_result t={0};uint64_t regs[8];
        CHECK(run(&m,&t,regs)==LG2S_LIMIT);CHECK(m.calls==64u&&t.steps==64u&&t.resumes==63u);
        CHECK(!t.complete&&regs[0]==1u&&regs[6]==0x61900042u);
    }
    for(unsigned mode=0;mode<2u;++mode) {
        ++cases;struct mock m=fresh();m.backwards=!mode;m.late=mode;
        struct lg2s_result t={0};uint64_t regs[8];
        CHECK(run(&m,&t,regs)==LG2S_TIME);CHECK(m.calls==1u&&t.steps==1u&&!t.complete);
        CHECK(t.step[0].executed&&t.step[0].after_valid);
    }
    {
        ++cases;struct mock m=fresh();m.tick=LG2S_MAX_NS;
        struct lg2s_result t={0};uint64_t regs[8];
        CHECK(run(&m,&t,regs)==LG2S_TIME);CHECK(m.calls==0u&&t.steps==0u);
    }
    {
        ++cases;struct mock m=fresh();struct lg2s_result t={0};uint64_t regs[8]={0};
        struct lg2s_ops o={&m,guard,now_ns,raw};
        CHECK(lg2s_run(NULL,&t,&o)==LG2S_ARGUMENT);
        CHECK(lg2s_run(regs,NULL,&o)==LG2S_ARGUMENT);
        CHECK(lg2s_run(regs,&t,NULL)==LG2S_ARGUMENT);
        o.raw_smc=NULL;CHECK(lg2s_run(regs,&t,&o)==LG2S_ARGUMENT);
        CHECK(!t.used&&m.calls==0u);
    }
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u}\n",cases,checks);
    return 0;
}
