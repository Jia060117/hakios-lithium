/* SPDX-License-Identifier: GPL-2.0-only */
#include "touch-r21-probe.h"
#include <assert.h>
#include <stdio.h>

/* Independent stateful FIFO model: consumes only programmed counts, enforces
 * RESET/RUN/PAUSE and config-during-run, retains BUS_ACTIVE between messages. */
struct fake {
    uint32_t state, mode, config, wc, rc, fifo[3], rx[9], flags;
    uint8_t stream[9], reg;
    size_t calls, fail_at, times, delays, resets, sw_resets, transitions, tx, rxpos;
    size_t phase, stream_len, staged_words, transactions, reads[8];
    uint64_t now;
    uint32_t bus_error, core_error, reject_state;
    bool freeze, invalid, no_master, no_done, stuck, cleanup_bad, reset_bad;
    bool active, master, backward, late, fail_second, stale_read_count, phase2_nack;
    bool rearm, assert_deadline, reject_final_reset, entry_error;
    size_t advance_on_call;
    uint64_t hard_deadline;
};
static bool access(struct fake *f)
{
    if (f->assert_deadline) assert(f->now < f->hard_deadline);
    if (++f->calls == f->advance_on_call) f->now = f->hard_deadline;
    return f->calls != f->fail_at;
}
static bool rd(void *p, uint32_t off, uint32_t *v)
{
    struct fake *f = p;
    if (!access(f)) return false;
    switch (off) {
    case LQT_STATE:
        *v = (f->invalid || (f->cleanup_bad && f->resets > 1)) ? 0 :
            LQT_STATE_VALID | (f->master ? LQT_MASTER : 0) | f->state; break;
    case LQT_IO_MODE: *v = f->mode; break;
    case LQT_I2C_STATUS:
        *v = (f->active || f->stuck ? LQT_BUS_ACTIVE : 0) |
            ((f->entry_error && !f->phase) ||
             (f->phase && (!f->phase2_nack || f->phase == 2)) ? f->bus_error : 0); break;
    case LQT_ERROR_FLAGS: *v = f->phase ? f->core_error : 0; break;
    case LQT_OPERATIONAL: *v = f->no_done ? 0 : f->flags; break;
    case LQT_IN_FIFO:
        assert(f->phase == 2 && f->state == 1 && f->rxpos < (f->rc + 3u) / 4u);
        *v = f->rx[f->rxpos++];
        if (f->rxpos == (f->rc + 3u) / 4u) f->active = false;
        break;
    default: assert(0);
    }
    return true;
}
static void consume(struct fake *f)
{
    if (!f->staged_words) return;
    assert(f->phase < 2);
    if (!f->phase) {
        assert(f->wc == 5 && f->config == 0x287 && f->staged_words == 2);
        if (f->stale_read_count) assert(f->rc == UINT32_C(0x12345678));
        f->reg = (uint8_t)f->fifo[1];
        f->active = true;
    } else {
        assert(f->wc == 4 && f->config == 0x207 && f->staged_words == 1);
        assert(f->rc >= 3 && f->rc <= 34 && f->active);
        assert(f->transactions < 8);
        f->reads[f->transactions++] = f->rc - 2;
        memset(f->rx, 0, sizeof(f->rx));
        /* Deliberately arbitrary tag bytes: Linux skips, does not validate. */
        for (size_t i = 0; i < f->rc; ++i) {
            uint8_t x = i < 2 ? 0xee : (uint8_t)(f->reg + i - 2);
            f->rx[i / 4] |= (uint32_t)x << (8 * (i % 4));
        }
    }
    for (size_t i = 0; i < f->wc; ++i) {
        assert(f->stream_len < sizeof(f->stream));
        f->stream[f->stream_len++] = (uint8_t)(f->fifo[i / 4] >> (8 * (i % 4)));
    }
    ++f->phase;
    f->flags = LQT_OUT_SERVICE | (f->phase == 2 ? LQT_IN_SERVICE : 0);
    f->staged_words = 0;
}
static bool wr(void *p, uint32_t off, uint32_t v)
{
    struct fake *f = p;
    if (!access(f)) return false;
    switch (off) {
    case LQT_SW_RESET:
        assert(v == 1);
        if (f->reset_bad && f->resets) return false;
        ++f->resets; ++f->sw_resets; f->phase = f->rxpos = f->staged_words = f->stream_len = 0;
        f->state = 0; f->master = f->active = false; f->flags = 0; break;
    case LQT_STATE:
        assert(v == 0 || v == 1 || v == 3);
        ++f->transitions;
        if (f->reject_state == v + 1 || (v == 0 && f->phase == 2 && !f->active &&
            f->reject_final_reset)) break;
        if (!v) {
            assert(!f->active);
            ++f->resets; f->state = 0;
            if (f->rearm) {
                f->phase = f->rxpos = f->staged_words = f->stream_len = 0;
                f->master = false; f->flags = 0;
            }
            if (f->late) f->now = 10000;
        } else { f->state = v; if (v == 1) consume(f); }
        break;
    case LQT_MASTER_GEN: assert(v == 1 && f->state == 0); f->master = !f->no_master; break;
    case LQT_IO_MODE: assert(v == 0xc000 && f->state == 0); break;
    case LQT_CONFIG: f->config = v; break;
    case LQT_MX_OUTPUT_CNT: case LQT_MX_INPUT_CNT: assert(!v && !f->state); break;
    case LQT_MX_WRITE_CNT:
        if (f->phase) assert(f->phase == 1 && f->state == 3 && (v & LQC_CONFIG_RUN));
        else assert(f->state == 0 && !(v & LQC_CONFIG_RUN));
        f->wc = v & ~LQC_CONFIG_RUN; break;
    case LQT_MX_READ_CNT:
        assert(f->phase == 1 && f->state == 3 && (v & LQC_CONFIG_RUN));
        f->rc = v & ~LQC_CONFIG_RUN; break;
    case LQT_CLK_CTL: assert(f->state == 1 && !f->phase && v == 0x35d); break;
    case LQT_OUT_FIFO:
        assert(f->state == 3 && f->staged_words < 3);
        f->fifo[f->staged_words++] = v; ++f->tx; break;
    case LQT_OPERATIONAL:
        assert(v == LQT_OUT_SERVICE || v == LQT_IN_SERVICE); f->flags &= ~v; break;
    case LQT_I2C_STATUS: assert(v && !(v & ~LQT_BUS_ERRORS)); f->bus_error &= ~v; break;
    case LQT_ERROR_FLAGS: assert(v && !(v & ~LQT_CORE_ERRORS)); f->core_error &= ~v; break;
    default: assert(0);
    }
    return true;
}
static uint64_t tm(void *p) { struct fake *f = p; ++f->times; return f->now; }
static void delay(void *p, uint32_t n)
{
    struct fake *f = p; assert(n == 1); ++f->delays;
    if (!f->freeze) f->now = f->backward ? f->now - n : f->now + n;
}
static struct lqt_io setup(struct fake *f)
{
    memset(f, 0, sizeof(*f));
    f->mode = 1u | 1u << 2 | 1u << 5 | 1u << 7; /* both 64 bytes */
    return (struct lqt_io){f, rd, wr, tm, delay, LQT_ALL_VERIFIED, 0x35d, 100, 8};
}
static struct lqt_outcome run(struct lqt_io *io, uint8_t *out, size_t n)
{ return lqc_read_register(io, 0x38, 0xa3, out, 32, n, 10000); }
static void unchanged(const uint8_t *out, size_t n)
{ for (size_t i = 0; i < n; ++i) assert(out[i] == 0x5a); }
static void lengths(void)
{
    for (size_t n = 1; n <= 32; ++n) {
        struct fake f; struct lqt_io io = setup(&f); uint8_t out[34];
        memset(out, 0x5a, sizeof(out));
        struct lqt_outcome r = run(&io, out + 1, n);
        assert(r.status == LQT_OK && r.cleanup == LQT_OK);
        assert(out[0] == 0x5a && out[n + 1] == 0x5a);
        for (size_t i = 0; i < n; ++i) assert(out[i + 1] == (uint8_t)(0xa3 + i));
        const uint8_t stream[9] = {0x81,0x70,0x82,1,0xa3,0x81,0x71,0x87,(uint8_t)n};
        assert(f.stream_len == 9 && !memcmp(f.stream, stream, 9));
        assert(f.tx == 3 && f.phase == 2 && f.resets == 2 && !f.active);
        assert(f.rc == n + 2 && f.wc == 4 && f.calls < 100);
    }
}
static void invalid(void)
{
    struct fake f; struct lqt_io io = setup(&f); uint8_t out[32];
    uint32_t words[4], old[4]; size_t count = 99;
    memset(out, 0x5a, sizeof(out)); memset(words, 0x5a, sizeof(words));
    memcpy(old, words, sizeof(old));
    assert(!lqc_pack(0x38,0,1,words,2,&count));
    assert(!lqc_pack(0x80,0,1,words,4,&count));
    assert(!lqc_pack(0,0,0,words,4,&count));
    assert(!lqc_pack(0,0,SIZE_MAX,words,4,&count));
    assert(!lqc_pack(0,0,1,NULL,4,&count));
    assert(!lqc_pack(0,0,1,words,4,NULL));
    assert(count == 99 && !memcmp(words,old,sizeof(old)));
    assert(lqc_pack(0x7f,0xff,32,words,3,&count));
    assert(count == 3 && words[0] == 0x0182fe81 && words[1] == 0xff &&
           words[2] == 0x2087ff81 && words[3] == old[3]);
    for (unsigned bit = 1; bit <= 128; bit <<= 1) {
        io.verified = LQT_ALL_VERIFIED ^ bit;
        assert(run(&io,out,1).status == LQT_PLATFORM_UNVERIFIED);
        assert(!f.calls && !f.times && !f.delays);
    }
    io.verified = LQT_ALL_VERIFIED;
    assert(run(&io,out,0).status == LQT_INVALID);
    assert(run(&io,out,33).status == LQT_INVALID);
    assert(run(&io,NULL,1).status == LQT_INVALID);
    assert(lqc_read_register(&io,0x38,0,out,1,2,10000).status == LQT_INVALID);
    assert(lqc_read_register(&io,0x80,0,out,32,1,10000).status == LQT_INVALID);
    assert(run(NULL,out,1).status == LQT_INVALID);
    for (unsigned k = 0; k < 8; ++k) {
        struct lqt_io bad = io;
        switch(k) { case 0: bad.read32=NULL; break; case 1: bad.write32=NULL; break;
        case 2: bad.time_us=NULL; break; case 3: bad.delay_us=NULL; break;
        case 4: bad.timeout_us=0; break; case 5: bad.timeout_us=1000001; break;
        case 6: bad.max_polls=0; break; case 7: bad.max_polls=257; break; }
        assert(run(&bad,out,1).status == LQT_INVALID);
    }
    assert(!f.calls && !f.times && !f.delays); unchanged(out,32);
    assert(lqc_read_register(&io,0x38,0,out,32,1,0).status == LQT_TRANSFER_TIMEOUT);
    assert(!f.calls && !f.delays);
}
static void faults(void)
{
    /* Sweep every successful-path MMIO call for every length, including final
     * cleanup state/idle calls. This catches partial RX and cleanup publication. */
    size_t swept = 0;
    for (size_t n = 1; n <= 32; ++n) {
        struct fake base; struct lqt_io io = setup(&base); uint8_t out[32];
        assert(run(&io,out,n).status == LQT_OK);
        for (size_t fail = 1; fail <= base.calls; ++fail) {
            struct fake f; io=setup(&f); f.fail_at=fail; memset(out,0x5a,32);
            struct lqt_outcome r=run(&io,out,n);
            assert(r.status != LQT_OK && f.calls < 150); unchanged(out,32); ++swept;
        }
    }
    for (unsigned k=0;k<14;++k) {
        struct fake f; struct lqt_io io=setup(&f); uint8_t out[32];
        memset(out,0x5a,32);
        switch(k) {
        case 0: f.bus_error=LQT_NACK; break;
        case 1: f.bus_error=0x10; break; case 2: f.core_error=4; break;
        case 3: f.no_done=true; break; case 4: f.invalid=true; break;
        case 5: f.no_master=true; break; case 6: f.stuck=true; break;
        case 7: f.freeze=true; f.no_done=true; break;
        case 8: f.reject_state=2; break; case 9: f.reject_state=4; break;
        case 10: f.cleanup_bad=true; break;
        case 11: f.no_done=true; f.reset_bad=true; break;
        case 12: f.no_done=true; f.backward=true; break;
        case 13: f.late=true; break;
        }
        struct lqt_outcome r=run(&io,out,32);
        assert(r.status!=LQT_OK && f.calls<180 && f.delays<32); unchanged(out,32);
        if(k==0) assert(r.status==LQT_NACK_ERROR && r.cleanup==LQT_OK);
        if(k==3 || k==7) assert(r.status==LQT_TRANSFER_TIMEOUT);
        if(k==10 || k==13) assert(r.status==LQT_CLEANUP_FAILED);
        if(k==11) assert(r.status==LQT_TRANSFER_TIMEOUT && r.cleanup==LQT_MMIO_ERROR);
    }
    for (unsigned k=0;k<3;++k) {
        struct fake f; struct lqt_io io=setup(&f); uint8_t out[32];
        memset(out,0x5a,32); f.mode=k==0 ? 0 : k==1 ? 3 : 3u<<5;
        assert(run(&io,out,32).status==LQT_FIFO_TOO_SMALL && !f.tx); unchanged(out,32);
    }
    printf("MMIO failure sweep: %zu cases (all lengths 1..32)\n",swept);
}
struct adapter_test { struct lqc_touch_adapter adapter; struct fake *f; unsigned claims,releases; };
static uint64_t anow(void *p) { return tm(((struct adapter_test *)p)->f); }
static enum lithium_touch_i2c_status claim(void *p,uint32_t base,uint64_t deadline)
{
    struct adapter_test *a=p; assert(base==LITHIUM_TOUCH_I2C_QUP_BASE && anow(p)<deadline);
    ++a->claims;
    if(a->adapter.poisoned) return LITHIUM_TOUCH_I2C_QUARANTINED;
    a->adapter.claimed=true; return LITHIUM_TOUCH_I2C_OK;
}
static void release(void *p)
{
    struct adapter_test *a=p; ++a->releases;
    if(lqc_touch_release_allowed(&a->adapter)) a->adapter.claimed=false;
}
static enum lithium_touch_i2c_handoff_state checked_release(void *p)
{
    struct adapter_test *a=p; ++a->releases;
    if(a->adapter.poisoned || !a->adapter.claimed)
        return LITHIUM_TOUCH_I2C_HANDOFF_UNCERTAIN;
    a->adapter.claimed=false;
    return LITHIUM_TOUCH_I2C_HANDOFF_RELEASED;
}
static void quarantine(void *p, enum lithium_touch_i2c_quarantine_reason reason)
{
    struct adapter_test *a=p; (void)reason; a->adapter.poisoned=true;
}
static struct lithium_touch_i2c_completion abridge(void *p,uint8_t addr,uint8_t reg,
                                                  uint8_t *out,size_t n,uint64_t d)
{
    struct adapter_test *a=p;
    if(a->f->fail_second && a->f->transactions==1) a->f->no_done=true;
    return lqc_touch_write_read(&a->adapter,addr,reg,out,n,d);
}
static void adapter(void)
{
    const struct lithium_touch_i2c_ops ops={anow,claim,abridge,release};
    const struct lithium_touch_i2c_safety_ops safety={checked_release,quarantine};
    for(unsigned fail=0;fail<2;++fail) {
        struct fake f; struct lqt_io io=setup(&f); f.fail_second=fail!=0; f.rearm=true;
        struct adapter_test a={.adapter={.io=&io},.f=&f};
        struct lithium_touch_i2c_report report; memset(&report,0x5a,sizeof(report));
        assert(lqc_touch_write_read(&a.adapter,0x38,0,report.bytes,32,10000).status==
               LITHIUM_TOUCH_I2C_NOT_OWNED && !f.calls && !f.times);
        enum lithium_touch_i2c_status s=lithium_touch_i2c_read_report_with_safety(
            &ops,&safety,&a,10000,&report);
        assert(a.claims==1 && a.releases==(fail ? 0u : 1u) && f.sw_resets==0);
        if(!fail) {
            assert(s==LITHIUM_TOUCH_I2C_OK && f.transactions==2 && f.reads[0]==32 && f.reads[1]==31);
            assert(!a.adapter.claimed && !a.adapter.poisoned && !f.state && f.resets==2);
            for(size_t i=0;i<63;++i) assert(report.bytes[i]==i);
        } else {
            assert(s!=LITHIUM_TOUCH_I2C_OK && a.adapter.claimed && a.adapter.poisoned);
            unchanged(report.bytes,63);
            const size_t calls=f.calls, times=f.times, transfers=f.transactions;
            assert(lithium_touch_i2c_read_report_with_safety(
                &ops,&safety,&a,10000,&report)==LITHIUM_TOUCH_I2C_QUARANTINED);
            assert(a.claims==2 && a.releases==0 && f.calls==calls && f.times>times &&
                f.transactions==transfers && a.adapter.claimed && a.adapter.poisoned);
        }
    }
    struct fake f; struct lqt_io io=setup(&f); uint8_t out[32]; memset(out,0x5a,32);
    struct lqc_touch_adapter a={.io=&io,.claimed=true}; f.bus_error=LQT_NACK; f.stuck=true;
    assert(lqc_touch_write_read(&a,0x38,0,out,32,10000).status==LITHIUM_TOUCH_I2C_IO_ERROR);
    assert(a.poisoned && !lqc_touch_release_allowed(&a));
    size_t calls=f.calls, times=f.times;
    assert(lqc_touch_write_read(&a,0x38,0,out,32,10000).status==LITHIUM_TOUCH_I2C_IO_ERROR);
    assert(f.calls==calls && f.times==times);
    unchanged(out,32);
}
static void touch_adapter_helpers(void)
{
    struct lqc_touch_adapter a={.claimed=true};
    assert(lqc_touch_release_allowed(&a));
    assert(lqc_touch_checked_release(&a)==LITHIUM_TOUCH_I2C_HANDOFF_RELEASED);
    assert(!a.claimed && !a.poisoned && !lqc_touch_release_allowed(&a));

    a.claimed=true;
    lqc_touch_quarantine(&a,LITHIUM_TOUCH_I2C_QUARANTINE_TRANSFER_UNCERTAIN);
    assert(a.claimed && a.poisoned && !lqc_touch_release_allowed(&a));
    assert(lqc_touch_checked_release(&a)==LITHIUM_TOUCH_I2C_HANDOFF_UNCERTAIN);
    lqc_touch_quarantine(&a,LITHIUM_TOUCH_I2C_QUARANTINE_RELEASE_UNCERTAIN);
    assert(a.claimed && a.poisoned);
    lqc_touch_quarantine(NULL,LITHIUM_TOUCH_I2C_QUARANTINE_TRANSFER_UNCERTAIN);
}
static void cleanup_edges(void)
{
    uint8_t out[32];
    struct fake f; struct lqt_io io=setup(&f);
    f.stale_read_count=true; f.rc=UINT32_C(0x12345678);
    assert(run(&io,out,32).status==LQT_OK && f.rc==34);
    for(unsigned k=0;k<4;++k) {
        io=setup(&f); memset(out,0x5a,32);
        struct lqc_touch_adapter a={.io=&io,.claimed=true};
        if(k==0) { f.no_done=true; io.timeout_us=100; }
        if(k==1) f.reject_final_reset=true;
        if(k==2) { f.bus_error=LQT_NACK; f.entry_error=true; }
        if(k==3) f.late=true;
        uint64_t deadline=k==0 ? 2 : 10000;
        struct lithium_touch_i2c_completion c=lqc_touch_write_read(&a,0x38,0,out,32,deadline);
        assert(c.status==(k==2 ? LITHIUM_TOUCH_I2C_IO_ERROR :
            LITHIUM_TOUCH_I2C_CLEANUP_UNCERTAIN) && !c.written && !c.read);
        assert(a.poisoned && a.claimed && !lqc_touch_release_allowed(&a));
        if(k==2) assert(a.last.cleanup==LQT_OK && a.last.status==LQT_CONTROLLER_ERROR && !f.tx);
        else assert(a.last.cleanup!=LQT_OK);
        assert(!f.sw_resets); unchanged(out,32);
        if(k==0) assert(f.now==deadline && f.resets==0); /* no post-deadline reset */
        size_t calls=f.calls,times=f.times,delays=f.delays;
        c=lqc_touch_write_read(&a,0x38,0,out,32,UINT64_MAX);
        assert(c.status==LITHIUM_TOUCH_I2C_IO_ERROR && f.calls==calls &&
               f.times==times && f.delays==delays);
    }
    {
        /* A deadline that expires before the first MMIO write remains a timeout,
         * while the adapter still latches the controller out of service. */
        io=setup(&f); memset(out,0x5a,32);
        struct lqc_touch_adapter a={.io=&io,.claimed=true};
        const struct lithium_touch_i2c_completion c=
            lqc_touch_write_read(&a,0x38,0,out,32,0);
        assert(c.status==LITHIUM_TOUCH_I2C_TIMEOUT && !c.written && !c.read);
        assert(a.poisoned && a.claimed && !f.calls && !f.sw_resets);
        unchanged(out,32);
    }
    for(unsigned k=0;k<2;++k) {
        io=setup(&f); f.bus_error=k ? 0x10 : LQT_NACK; f.phase2_nack=true;
        memset(out,0x5a,32);
        struct lqt_outcome r=run(&io,out,32);
        assert(r.status==(k ? LQT_CONTROLLER_ERROR : LQT_NACK_ERROR) && r.cleanup==LQT_OK);
        unchanged(out,32);
    }
    /* W1C and reset-on-error callbacks also get individual failure injection. */
    size_t swept=0;
    for(unsigned k=0;k<3;++k) {
        struct fake base; io=setup(&base);
        if(k==0) base.bus_error=LQT_NACK;
        if(k==1) base.core_error=4;
        if(k==2) base.no_done=true;
        assert(run(&io,out,32).status!=LQT_OK);
        for(size_t fail=1;fail<=base.calls;++fail) {
            io=setup(&f); f.fail_at=fail; memset(out,0x5a,32);
            if(k==0) f.bus_error=LQT_NACK;
            if(k==1) f.core_error=4;
            if(k==2) f.no_done=true;
            assert(run(&io,out,32).status!=LQT_OK && f.calls<180);
            unchanged(out,32); ++swept;
        }
    }
    /* Exact minimum RX capacity and one byte over; first phase ignores stale RX. */
    for(unsigned over=0;over<2;++over) {
        io=setup(&f); f.mode=0; memset(out,0x5a,32);
        assert(run(&io,out,6+over).status==(over ? LQT_FIFO_TOO_SMALL : LQT_OK));
        if(over) unchanged(out,32);
    }
    {
        /* The enclosing touch layer quarantines a timed-out operation whose
         * cleanup did not complete; it must not report an ordinary I/O error. */
        io=setup(&f); f.no_done=true;
        struct adapter_test a={.adapter={.io=&io},.f=&f};
        const struct lithium_touch_i2c_ops ops={anow,claim,abridge,release};
        const struct lithium_touch_i2c_safety_ops safety={checked_release,quarantine};
        struct lithium_touch_i2c_report report; memset(&report,0x5a,sizeof(report));
        assert(lithium_touch_i2c_read_report_with_safety(&ops,&safety,&a,2,&report)==
               LITHIUM_TOUCH_I2C_CLEANUP_UNCERTAIN);
        assert(a.releases==0 && a.adapter.claimed && a.adapter.poisoned &&
               f.resets==0 && !f.sw_resets);
        assert(a.adapter.last.cleanup==LQT_CLEANUP_FAILED); unchanged(report.bytes,63);
    }
    {
        /* An entry NACK is observed read-only: no reset, W1C, or retry. */
        io=setup(&f); f.bus_error=LQT_NACK; f.entry_error=true;
        struct adapter_test a={.adapter={.io=&io},.f=&f};
        const struct lithium_touch_i2c_ops ops={anow,claim,abridge,release};
        const struct lithium_touch_i2c_safety_ops safety={checked_release,quarantine};
        struct lithium_touch_i2c_report report; memset(&report,0x5a,sizeof(report));
        assert(lithium_touch_i2c_read_report_with_safety(&ops,&safety,&a,10000,&report)==
               LITHIUM_TOUCH_I2C_IO_ERROR);
        assert(a.claims==1 && a.releases==0 && a.adapter.poisoned && a.adapter.claimed);
        assert(!f.transactions && !f.sw_resets && !f.tx); unchanged(report.bytes,63);
    }
    printf("Error-path MMIO sweep: %zu cases; deadline poison/stale RX count PASS\n",swept);
}

static void no_software_reset(void)
{
    struct fake f;
    struct lqt_io io = setup(&f);
    uint8_t id = 0x5a;
    struct lqt_outcome result = lqc_probe_chip_id(&io, &id, 10000);
    const uint8_t expected[] = {0x81,0x70,0x82,1,0xa3,0x81,0x71,0x87,1};
    assert(result.status == LQT_OK && result.cleanup == LQT_OK && id == 0xa3);
    assert(f.resets == 1 && !f.sw_resets && f.state == 0 && !f.active && f.transactions == 1);
    assert(f.stream_len == sizeof(expected) && !memcmp(f.stream, expected, sizeof(expected)));
    const size_t successful_calls = f.calls;
    for (size_t i = 1; i <= successful_calls; ++i) {
        io = setup(&f); f.fail_at = i; id = 0x5a;
        result = lqc_probe_chip_id(&io, &id, 10000);
        assert(result.status != LQT_OK && id == 0x5a && !f.sw_resets);
        io = setup(&f); id = 0x5a;
        f.advance_on_call = i; f.hard_deadline = 10000; f.assert_deadline = true;
        result = lqc_probe_chip_id(&io, &id, f.hard_deadline);
        assert(result.status != LQT_OK && id == 0x5a && !f.sw_resets && f.calls == i);
    }
    for (unsigned i = 0; i < 5; ++i) {
        io = setup(&f); id = 0x5a;
        switch (i) {
        case 0: f.stuck = true; break;
        case 1: f.state = 1; break;
        case 2: f.flags = 1u << 4; break;
        case 3: f.bus_error = LQT_NACK; break;
        case 4: f.no_done = f.freeze = true; break;
        }
        result = lqc_probe_chip_id(&io, &id, 10000);
        assert(result.status != LQT_OK && !f.sw_resets && id == 0x5a && f.calls < 100);
        if (i < 3) assert(!f.transitions && !f.tx && !f.master && result.cleanup == LQT_OK);
        if (!i) assert(result.status == LQT_BUS_STUCK);
        if (i >= 3) assert(f.tx == 2 && f.phase == 1 && result.cleanup == LQT_CLEANUP_FAILED);
        if (i == 3) assert(result.status == LQT_NACK_ERROR);
        if (i == 4) assert(result.status == LQT_TRANSFER_TIMEOUT);
    }
    io = setup(&f); f.rearm = true;
    uint8_t report[63] = {0};
    for (size_t offset = 0; offset < sizeof(report); offset += 32) {
        size_t len = offset ? 31 : 32;
        result = lqc_read_register_with_policy(&io, 0x38, (uint8_t)offset,
            report + offset, len, len, 10000, LQC_POLL_REARM);
        assert(result.status == LQT_OK && result.cleanup == LQT_OK && !f.state && !f.active);
    }
    assert(f.transactions == 2 && f.resets == 2); /* terminal state, not SW_RESET */
    for (size_t i = 0; i < sizeof(report); ++i) assert(report[i] == i);
    for (uint32_t flags = LQT_OUT_DONE; flags <= LQT_IN_DONE; flags <<= 1) {
        io = setup(&f); f.flags = flags; id = 0x5a;
        result = lqc_probe_chip_id(&io, &id, 10000);
        assert(result.status == LQT_CONTROLLER_ERROR && result.cleanup == LQT_OK &&
            id == 0x5a && !f.tx && !f.resets && !f.sw_resets);
    }
    printf("No-reset probe: %zu MMIO and %zu hard-deadline injection points; clean entry, quarantine and split poll PASS\n",
        successful_calls, successful_calls);
}

static void r21_admission(void)
{
    const struct {
        const char *args;
        enum lithium_r21_touch_request expected;
    } cases[] = {
        {"bootcpu=1 cpus=1 fbwidth=1080 fbheight=2040", LITHIUM_R21_TOUCH_ABSENT},
        {"touch_stage_extra=1 x_touch_stage=1 touch=1", LITHIUM_R21_TOUCH_ABSENT},
        {"\ttouch_stage=0\r\n", LITHIUM_R21_TOUCH_STATIC},
        {"touch_stage=1", LITHIUM_R21_TOUCH_ID},
        {"touch_stage=2", LITHIUM_R21_TOUCH_FRAMES},
        {"touch_stage=3", LITHIUM_R21_TOUCH_HID},
        {"touch_stage=0 touch_stage=0", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage=0 touch_stage=1", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage=4", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage=-1", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage=00", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage=1touch", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage", LITHIUM_R21_TOUCH_INVALID},
        {"touch_stage=", LITHIUM_R21_TOUCH_INVALID},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        assert(lithium_r21_touch_request_parse(cases[i].args, strlen(cases[i].args)) ==
            cases[i].expected);
    assert(lithium_r21_touch_request_parse(NULL, 0) == LITHIUM_R21_TOUCH_ABSENT);
    assert(lithium_r21_touch_request_parse(NULL, 1) == LITHIUM_R21_TOUCH_INVALID);
    assert(lithium_r21_touch_request_parse("touch_stage=1", 4096) == LITHIUM_R21_TOUCH_INVALID);
    static const char embedded_nul[] = "touch_stage=1\0 touch_stage=2";
    assert(lithium_r21_touch_request_parse(embedded_nul, sizeof(embedded_nul) - 1) ==
        LITHIUM_R21_TOUCH_INVALID);

    struct fake f;
    struct lqt_io io = setup(&f);
    struct lithium_r21_probe once = {0};
    uint8_t id = 0x5a;
    assert(lithium_r21_read_id_once(&once, &io, 0x100, 0, 10000, &id) ==
        LQT_PLATFORM_UNVERIFIED);
    assert(lithium_r21_read_id_once(&once, &io, 1, 2, 10000, &id) ==
        LQT_PLATFORM_UNVERIFIED);
    for (uint32_t bit = 1; bit <= LQT_V_CALLBACKS; bit <<= 1) {
        io.verified = LQT_ALL_VERIFIED & ~bit;
        assert(lithium_r21_read_id_once(&once, &io, 1, 0, 10000, &id) ==
            LQT_PLATFORM_UNVERIFIED);
    }
    assert(!once.attempted && !f.calls && !f.times && !f.delays && id == 0x5a);

    io.verified = LQT_ALL_VERIFIED;
    assert(lithium_r21_read_id_once(&once, &io, 1, 0, 10000, &id) == LQT_OK);
    assert(once.attempted && once.parked_or_quarantined && f.transactions == 1 &&
        f.resets == 1 && !f.sw_resets);
    const size_t after_success = f.calls;
    assert(lithium_r21_read_id_once(&once, &io, 1, 0, 10000, &id) == LQT_INVALID);
    assert(f.calls == after_success);

    for (unsigned i = 0; i < 3; ++i) {
        io = setup(&f);
        once = (struct lithium_r21_probe){0};
        id = 0x5a;
        if (i == 0) f.bus_error = LQT_NACK;
        if (i == 1) f.stuck = true;
        if (i == 2) f.reject_final_reset = true;
        assert(lithium_r21_read_id_once(&once, &io, 1, 0, 10000, &id) != LQT_OK);
        assert(once.attempted && once.parked_or_quarantined == (i != 1));
        assert(!f.sw_resets && id == 0x5a);
        if (i == 2) assert(f.transactions == 1 && !f.active &&
            once.last.status == LQT_CLEANUP_FAILED && once.last.cleanup == LQT_STATE_TIMEOUT);
        const size_t failed_calls = f.calls;
        assert(lithium_r21_read_id_once(&once, &io, 1, 0, 10000, &id) == LQT_INVALID);
        assert(f.calls == failed_calls);
    }
    puts("R21 admission: absent/static/stages, malformed args, CPU/8 resource gates, no reset/retry PASS");
}

int main(void)
{
    lengths(); invalid(); faults(); adapter(); touch_adapter_helpers(); cleanup_edges(); no_software_reset(); r21_admission();
    puts("PASS: combined FIFO lengths/tags/gates/capacity/faults/cleanup/32+31 adapter");
    return 0;
}
