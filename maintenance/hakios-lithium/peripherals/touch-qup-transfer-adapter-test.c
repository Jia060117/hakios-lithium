/* R32 actual adapter sidecar. Inert RAM-only physical callback fixture derived
 * from R31's adapter fixture. No phone, device, QEMU or repository operations.
 * Numeric policies and the 22-write oracle are independent of production.
 * R32_FIXTURE_ONLY bootstraps unchanged R31 plus the new FIFO model while
 * the separately owned transfer adapter is being implemented. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef R32_FIXTURE_ONLY
#define R32_FIXTURE_ONLY 0
#endif
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define LTS1_AHB_VOTE UINT32_C(0x00352004)
#define LTS1_AHB_HALT UINT32_C(0x00325004)
#define LTS1_BLSP_BCR UINT32_C(0x00325000)
#define LTS1_QUP_BCR UINT32_C(0x00330000)
#define LTS1_QUP_CBCR UINT32_C(0x00330008)
#define LTS1_QUP_CMD UINT32_C(0x00330020)
#define LTS1_QUP_CFG UINT32_C(0x00330024)
#define LTS1_PIN87 UINT32_C(0x01067000)
#define LTS1_PIN88 UINT32_C(0x01068000)
#define LTS1_QUP_STATE UINT32_C(0x075ba004)
struct lts1_window { uint32_t physical; vm_offset_t mapped; };
static struct {
    struct lts1_window windows[8];
    unsigned mappings;
    bool started, poisoned, clocks_ready, pins_ready;
} lts1;
static uint64_t lts1_now(void);
static int ml_at_interrupt_context(void);
static int ml_get_interrupts_enabled(void);
static int ml_get_max_cpu_number(void);
static uint64_t mock_affinity(void);
static vm_offset_t mock_map(vm_offset_t, vm_size_t);
static uint32_t mock_read(vm_offset_t);
static void mock_write(vm_offset_t, uint32_t);
static void mock_barrier(void);
static void mock_delay(uint32_t);
static int mock_printf(const char *, ...) __attribute__((format(printf, 1, 2)));
#define LITHIUM_PERIPHERAL_AFFINITY() mock_affinity()
#define LITHIUM_TOUCH_ID_MAP(p, s) mock_map((p), (s))
#define LITHIUM_TOUCH_ID_READ(a) mock_read(a)
#define LITHIUM_TOUCH_ID_WRITE(a, v) mock_write((a), (v))
#define LITHIUM_TOUCH_ID_DELAY(us) mock_delay(us)
#define LITHIUM_TOUCH_STAGE1_BARRIER() mock_barrier()
#define printf mock_printf
#include "../../../ports/hakios-lithium/peripherals/touch-id.inc"
#define LTSN_EXPERIMENTAL 1
#define LTSP_EXPERIMENTAL 1
#if !R32_FIXTURE_ONLY
#define LTST_EXPERIMENTAL 1
#include "../../../ports/hakios-lithium/peripherals/touch-qup-transfer-core.h"
#undef LTST_EXPERIMENTAL
static struct ltst_result captured;
static void observed_transfer_into(struct ltsp_io *, struct ltst_result *);
#else
#include "../../../ports/hakios-lithium/peripherals/touch-qup-phase-core.h"
static struct ltsp_result captured;
#endif
#undef LTSP_EXPERIMENTAL
#undef LTSN_EXPERIMENTAL
/* Phase FUNCTIONS are intentionally included before the new adapter. */
#include "../../../ports/hakios-lithium/peripherals/touch-qup-phase.inc"
#if !R32_FIXTURE_ONLY
#define ltst_read_id_into(io, output) observed_transfer_into(io, output)
#include "../../../ports/hakios-lithium/peripherals/touch-qup-transfer.inc"
#undef ltst_read_id_into
#endif
#undef printf

static const uint32_t pages[9] = {
    0x00325000,0x00330000,0x00352000,0x01067000,0x01068000,
    0x075ba000,0x01079000,0x0108d000,0x09bc0000
};
static const struct { uint32_t physical, good, bad_bit; } board[] = {
    {0x00325000,0,1},{0x00330000,0,1},{0x00330020,0,1},
    {0x00352004,0x8000,0x8000},{0x00325004,0,0x80000000},
    {0x00330008,1,1},{0x01067000,0x0d,4},
    {0x01068000,0x0d,4},{0x00330024,0,2}
};
static const uint32_t snapshot_offsets[17] = {
    0x030,0x004,0x000,0x008,0x018,0x01c,0x020,0x028,0x10c,
    0x214,0x100,0x200,0x208,0x150,0x408,0x400,0x404
};
static const uint32_t expected_writes[22][2] = {
    {0x00c,1},{0x004,0},{0x000,0x207},{0x020,0x3c},{0x028,0x300},
    {0x408,1},{0x020,0},{0x028,0x300},{0x200,0},{0x100,0},
    {0x208,3},{0x150,9},{0x000,0x207},{0x008,0xc0a5},{0x408,1},
    {0x004,1},{0x400,0x003e007c},{0x004,3},
    {0x110,0x01827081},{0x110,0x877181a3},{0x110,1},{0x004,1}
};
static const uint32_t sample_offsets[7]={0x030,0x01c,0x404,0x018,0x214,0x10c,0x004};
static struct {
    uint32_t words[9][1024];
    struct { char kind; uint32_t physical,value; } trace[50000];
    unsigned events,reads,writes,qup_reads,maps,barriers,delays,times;
    unsigned external_calls,after_poison,post_core_external;
    struct { char kind; unsigned count,writes,reads; } external[24000];
    unsigned gic_reads[3],sw_resets,run_count,pause_count,tx_words,rx_reads;
    unsigned counts[256],fault_at,fault_mode,reenter_at,trip_write,trip_register;
    char fault_kind,reenter_kind;
    bool fired,reentered,map_fail,gic_mapped,policy_only,core_returned;
    bool observed_pair,hold_state,corrupt_readback,rx_left;
    unsigned transition_wait,wait_left,complete_wait,complete_left;
    unsigned stop_wait,stop_left,read_step_us;
    uint32_t completion_status,completion_op,rx_word;
    uint32_t mutate_physical,mutate_value,mutate_after_writes,mutate_at_read;
    uint64_t now,affinity;
    int irq_context,irq_enabled,max_cpu;
    char log[131072];
    size_t log_size;
#if !R32_FIXTURE_ONLY
    unsigned callback_counts[4],callback_at,callback_mode,callback_after_false;
    char callback_kind;
    bool callback_failed;
    struct {
        uint32_t offset,value;
        unsigned writes,physical_read;
        uint64_t before_us,accepted_us;
        bool returned;
    } core_reads[9000];
    unsigned core_read_count;
#endif
} mock;
static unsigned scenarios,failures,assertions,evidence_checks;
static const char *case_name="initial";
static unsigned capture_calls;
static struct ltsp_io bound_io,original_io;
#define CHECK(expr) do { ++assertions; if (!(expr)) { \
    if (failures<32) fprintf(stderr,"FAIL %s scenario=%u line=%d: %s\n", \
        case_name,scenarios,__LINE__,#expr); ++failures; \
} } while (0)

static void probe(void)
{
#if R32_FIXTURE_ONLY
    lithium_touch_qup_phase_probe();
#else
    lithium_touch_qup_transfer_probe();
#endif
}
static uint32_t *word(uint32_t physical)
{
    assert(!(physical&3u));
    for (unsigned i=0;i<9;++i)
        if ((physical&~UINT32_C(0xfff))==pages[i])
            return &mock.words[i][(physical&0xfffu)/4];
    assert(!"physical address escaped RAM fixture"); return NULL;
}
static uint32_t decode(vm_offset_t address)
{
    for (unsigned i=0;i<9;++i) {
        uintptr_t base=(uintptr_t)&mock.words[i][0];
        if (address>=base && address-base<4096) {
            assert((address-base)%4==0 && (i!=8 || mock.gic_mapped));
            return pages[i]+(uint32_t)(address-base);
        }
    }
    assert(!"access outside published RAM windows"); return 0;
}
static bool read_allowed(uint32_t off)
{
    if (off==0x218) return true;
    for (unsigned i=0;i<17;++i) if (off==snapshot_offsets[i]) return true;
    return false;
}
static void event(char kind,uint32_t physical,uint32_t value)
{
    assert(mock.events<sizeof(mock.trace)/sizeof(mock.trace[0]));
    mock.trace[mock.events].kind=kind;
    mock.trace[mock.events].physical=physical;
    mock.trace[mock.events++].value=value;
}
static void hook(char kind)
{
    unsigned n=++mock.counts[(unsigned char)kind];
    if (kind!='L') {
        assert(mock.external_calls<sizeof(mock.external)/sizeof(mock.external[0]));
        mock.external[mock.external_calls].kind=kind;
        mock.external[mock.external_calls].count=n;
        mock.external[mock.external_calls].writes=mock.writes;
        mock.external[mock.external_calls].reads=mock.reads;
        ++mock.external_calls;
        if (ltid.poisoned) ++mock.after_poison;
        if (mock.core_returned) ++mock.post_core_external;
    }
    if (mock.reenter_kind==kind && n==mock.reenter_at && !mock.reentered) {
        mock.reentered=true; probe();
    }
    if (mock.fault_kind!=kind || mock.fault_at!=n) return;
    mock.fired=true;
    if (mock.fault_mode==1) mock.now+=100000;
    else if (mock.fault_mode==2) mock.now=999;
    else if (mock.fault_mode==3) mock.now+=20000;
}
static uint64_t lts1_now(void) { hook('T'); ++mock.times; return mock.now; }
static int ml_at_interrupt_context(void) { hook('C'); return mock.irq_context; }
static int ml_get_interrupts_enabled(void) { hook('E'); return mock.irq_enabled; }
static int ml_get_max_cpu_number(void) { hook('N'); return mock.max_cpu; }
static uint64_t mock_affinity(void) { hook('A'); return mock.affinity; }
static vm_offset_t mock_map(vm_offset_t p,vm_size_t size)
{
    assert(p==0x09bc0000 && size==4096 && !mock.gic_mapped);
    event('M',(uint32_t)p,(uint32_t)size); ++mock.maps; hook('M');
    if (mock.map_fail) return 0;
    mock.gic_mapped=true; return (vm_offset_t)&mock.words[8][0];
}
static uint32_t mock_read(vm_offset_t address)
{
    uint32_t p=decode(address),off=p&0xfffu,page=p&~UINT32_C(0xfff);
    uint32_t value=*word(p);
    if (page==0x09bc0000) {
        assert(off==0x110 || off==0x210 || off==0x310);
        unsigned reg=off==0x110?0u:off==0x210?1u:2u;
        ++mock.gic_reads[reg];
        if (mock.trip_register==reg+1 && mock.qup_reads && mock.writes>=mock.trip_write)
            value|=0x400;
    } else if (page==0x075ba000) {
        assert(read_allowed(off)); ++mock.qup_reads;
        if (off==0x004 && mock.wait_left) { --mock.wait_left; value&=~4u; }
        if (mock.run_count==2) {
            if (off==0x018 && !mock.rx_reads) {
                if (mock.complete_left) { --mock.complete_left; value=0; }
                else value=mock.completion_op;
            }
            if (off==0x404) {
                if (mock.rx_reads && mock.stop_left) { --mock.stop_left; value=0x0c000100; }
                else value=mock.completion_status;
            }
            if (off==0x214) value=mock.rx_left?3u:0u;
            if (off==0x10c) value=0;
        }
        if (off==0x218) {
            assert(mock.run_count==2 && mock.tx_words==3 && mock.rx_left);
            assert(!mock.rx_reads); ++mock.rx_reads; mock.rx_left=false;
            value=mock.rx_word; *word(0x075ba018)=0x800;
        }
        if (mock.mutate_physical==p && mock.writes>=mock.mutate_after_writes &&
            (!mock.mutate_at_read || mock.reads+1==mock.mutate_at_read)) value=mock.mutate_value;
    } else {
        bool allowed=false;
        for (unsigned i=0;i<9;++i) if (p==board[i].physical) allowed=true;
        assert(allowed);
    }
    mock.now+=mock.read_step_us;
    event('R',p,value); ++mock.reads; hook('R'); return value;
}
static void mock_write(vm_offset_t address,uint32_t value)
{
    uint32_t p=decode(address),off=p&0xfffu;
    assert((p&~UINT32_C(0xfff))==0x075ba000);
    if (!mock.policy_only) {
        assert(mock.writes<22 && off==expected_writes[mock.writes][0] && value==expected_writes[mock.writes][1]);
        assert(mock.events>=3);
        assert(mock.trace[mock.events-3].kind=='R' && mock.trace[mock.events-3].physical==0x09bc0110);
        assert(mock.trace[mock.events-2].kind=='R' && mock.trace[mock.events-2].physical==0x09bc0310);
        assert(mock.trace[mock.events-1].kind=='R' && mock.trace[mock.events-1].physical==0x09bc0210);
    }
    if (off==0x00c) {
        assert(!mock.sw_resets); ++mock.sw_resets;
        *word(0x075ba018)=0xc0; mock.wait_left=mock.transition_wait;
    }
    if (off==0x004) {
        if (value==1) {
            ++mock.run_count;
            *word(0x075ba018)=0;
            if (mock.observed_pair) *word(0x075ba404)=0x0c110000;
            if (mock.run_count==2) {
                assert(mock.tx_words==3); mock.rx_left=true;
                mock.complete_left=mock.complete_wait; mock.stop_left=mock.stop_wait;
                *word(0x075ba404)=mock.completion_status;
            }
        }
        if (value==3) ++mock.pause_count;
        mock.wait_left=mock.transition_wait;
    }
    if (off==0x110) {
        assert(mock.run_count==1 && mock.pause_count==1 && mock.tx_words<3);
        ++mock.tx_words;
    }
    if (off==0x400) assert((*word(0x075ba004)&7u)==5u);
    if (!(mock.hold_state && off==0x004)) *word(p)=off==0x004?(0x11cu|value):value;
    if (mock.corrupt_readback && off==0x000) *word(p)^=1u;
    event('W',p,value); ++mock.writes; hook('W');
}
static void mock_barrier(void) { ++mock.barriers; hook('B'); }
static void mock_delay(uint32_t us)
{
    assert(us && us<=10); mock.now+=us; ++mock.delays; hook('D');
}
static int mock_printf(const char *fmt,...)
{
    va_list ap; va_start(ap,fmt);
    size_t left=sizeof(mock.log)-mock.log_size;
    int n=vsnprintf(mock.log+mock.log_size,left,fmt,ap); va_end(ap);
    assert(n>=0 && (size_t)n<left); mock.log_size+=(size_t)n; hook('L'); return n;
}
#if !R32_FIXTURE_ONLY
static bool callback_hit(unsigned index,char kind)
{
    if (mock.callback_failed) ++mock.callback_after_false;
    unsigned n=++mock.callback_counts[index];
    return mock.callback_kind==kind && mock.callback_at==n;
}
static bool proxy_read(void *cookie,uint32_t off,uint32_t *out)
{
    bool hit=callback_hit(0,'r'); unsigned index=mock.core_read_count++;
    assert(index<sizeof(mock.core_reads)/sizeof(mock.core_reads[0]));
    mock.core_reads[index].offset=off; mock.core_reads[index].writes=mock.writes;
    mock.core_reads[index].before_us=mock.now;
    if (hit && mock.callback_mode==1) { mock.callback_failed=true; return false; }
    bool ok=original_io.read32(cookie,off,out);
    if (hit && mock.callback_mode==2) { mock.callback_failed=true; return false; }
    mock.core_reads[index].returned=ok; mock.core_reads[index].physical_read=mock.reads;
    if (ok) { mock.core_reads[index].value=*out; mock.core_reads[index].accepted_us=mock.now; }
    if (!ok) mock.callback_failed=true; return ok;
}
static bool proxy_write(void *cookie,uint32_t off,uint32_t value)
{
    bool hit=callback_hit(1,'w');
    if (hit && mock.callback_mode==1) { mock.callback_failed=true; return false; }
    bool ok=original_io.write32(cookie,off,value);
    if (hit && mock.callback_mode==2) { mock.callback_failed=true; return false; }
    if (!ok) mock.callback_failed=true; return ok;
}
static uint64_t proxy_now(void *cookie)
{
    bool hit=callback_hit(2,'t'); uint64_t t=original_io.now_us(cookie);
    if (hit) { mock.callback_failed=true; return mock.callback_mode==1?t+300000:999; }
    return t;
}
static void proxy_delay(void *cookie,uint32_t us)
{
    bool hit=callback_hit(3,'d'); original_io.delay_us(cookie,us);
    if (hit) mock.now=mock.callback_mode==1?mock.now+100000:999;
}
static void observed_transfer_into(struct ltsp_io *io, struct ltst_result *output)
{
    ++capture_calls; bound_io=original_io=*io;
    io->read32=proxy_read; io->write32=proxy_write;
    io->now_us=proxy_now; io->delay_us=proxy_delay;
    ltst_read_id_into(io, output); captured=*output; mock.core_returned=true;
}
#endif
static void reset_fixture(void)
{
    memset(&mock,0,sizeof(mock)); memset(&lts1,0,sizeof(lts1));
    memset(&ltid,0,sizeof(ltid)); memset(&captured,0,sizeof(captured));
    memset(&bound_io,0,sizeof(bound_io)); memset(&original_io,0,sizeof(original_io));
    ltspp_active=ltspp_reset_written=false; ltspp_write_count=0;
    memset(ltspp_write_trace,0,sizeof(ltspp_write_trace)); capture_calls=0;
#if !R32_FIXTURE_ONLY
    ltstp_active=false; ltstp_write_count=ltstp_fifo_reads=0;
    memset(ltstp_write_trace,0,sizeof(ltstp_write_trace));
#endif
    ++scenarios; mock.now=1000; mock.affinity=1; mock.irq_enabled=1;
    mock.observed_pair=true; mock.rx_word=0x00abdead;
    mock.completion_status=0x0c110000; mock.completion_op=0x820;
    lts1.started=lts1.clocks_ready=lts1.pins_ready=true; lts1.mappings=8;
    for (unsigned i=0;i<8;++i) {
        lts1.windows[i].physical=pages[i];
        lts1.windows[i].mapped=(vm_offset_t)&mock.words[i][0];
    }
    for (unsigned i=0;i<9;++i) *word(board[i].physical)=board[i].good;
    *word(0x075ba008)=0xa5; *word(0x075ba018)=0xc0;
    *word(0x075ba020)=0x3c; *word(0x075ba004)=0x11c;
    *word(0x075ba030)=0x20060000; *word(0x075ba404)=0x0c000000;
}
static unsigned occurrences(const char *s,const char *needle)
{
    unsigned n=0; while ((s=strstr(s,needle))) { ++n; ++s; } return n;
}

#if R32_FIXTURE_ONLY
static void test_upfront_fixture(void)
{
    case_name="upfront-unchanged-R31-and-independent-TX-RX-model";
    reset_fixture(); probe();
    CHECK(mock.writes==18 && mock.qup_reads==411 && mock.reads==477);
    CHECK(mock.sw_resets==1 && mock.run_count==1 && mock.pause_count==1);
    CHECK(*word(0x075ba004)==0x11f && !mock.rx_reads && !mock.tx_words);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH phase field ")==272);
    mock.policy_only=true;
    for (unsigned i=18;i<22;++i)
        mock_write((vm_offset_t)word(0x075ba000+expected_writes[i][0]),expected_writes[i][1]);
    CHECK(mock.writes==22 && mock.tx_words==3 && mock.run_count==2);
    CHECK(mock_read((vm_offset_t)word(0x075ba018))==0x820);
    CHECK(mock_read((vm_offset_t)word(0x075ba214))==3);
    uint32_t raw=mock_read((vm_offset_t)word(0x075ba218));
    CHECK(raw==0x00abdead && ((raw>>16)&0xffu)==0xab && mock.rx_reads==1);
    CHECK(mock_read((vm_offset_t)word(0x075ba214))==0);
    CHECK(mock_read((vm_offset_t)word(0x075ba018))==0x800);
    CHECK(*word(0x075ba004)==0x11d);
    for (unsigned i=0;i<7;++i) CHECK(read_allowed(sample_offsets[i]));
    CHECK(read_allowed(0x218) && !read_allowed(0x110) && !read_allowed(0x219));
    printf("upfront fixture: unchanged R31 411 QUP reads / 18 writes; numeric TX3+RUN and 2-overhead+ID RX model passed\n");
}
#else
static const char *const reg_names[17]={
    "VERSION","STATE","CONFIG","IO_MODES","OPERATIONAL","ERROR_FLAGS",
    "ERROR_FLAGS_EN","OPERATIONAL_MASK","OUT_FIFO_COUNT","IN_FIFO_COUNT",
    "MX_OUTPUT","MX_INPUT","MX_READ","MX_WRITE",
    "MASTER_CONFIG","CLK_CTL","I2C_STATUS"
};
static void expect_next(const char **cursor,const char *prefix,const char *expected)
{
    const char *p=strstr(*cursor,prefix);
    CHECK(p && !strncmp(p,expected,strlen(expected)));
    *cursor=p?p+strlen(expected):mock.log+mock.log_size;
}
static void check_sample_log(const char *stage,const struct ltst_sample *s)
{
    static const char *const names[7]={"VERSION","ERROR","STATUS","OPERATIONAL","STATE","IN_COUNT","OUT_COUNT"};
    uint32_t values[7]={s->version,s->error,s->status,s->operational,s->state,s->in_count,s->out_count};
    char expected[400],prefix[150];
    CHECK(!(s->valid_mask&~0x7fu));
    CHECK(s->complete==(s->valid_mask==0x7fu));
    if (!s->attempted) CHECK(!s->valid_mask && !s->started_us && !s->completed_us);
    if (!s->valid_mask) CHECK(!s->completed_us);
    else CHECK(s->completed_us>=s->started_us);
    snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer sample stage=%s attempted=%u complete=%u valid=0x%02x poll=%u\n",
        stage,s->attempted,s->complete,s->valid_mask,s->poll_index);
    CHECK(strstr(mock.log,expected));
    snprintf(prefix,sizeof(prefix),"LITHIUM TOUCH transfer field stage=%s ",stage);
    const char *cursor=mock.log;
    for (unsigned i=0;i<7;++i) {
        if (s->valid_mask&(1u<<i))
            snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=%s index=%u reg=%s valid=1 value=0x%08x\n",stage,i,names[i],values[i]);
        else {
            CHECK(!values[i]);
            snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=%s index=%u reg=%s valid=0 value=unread\n",stage,i,names[i]);
        }
        expect_next(&cursor,prefix,expected);
    }
    snprintf(prefix,sizeof(prefix),"sample_time stage=%s start_absolute_us=",stage);
    if (s->attempted) {
        snprintf(expected,sizeof(expected),"%s%llu\n",prefix,(unsigned long long)s->started_us);
        CHECK(strstr(mock.log,expected));
    } else CHECK(!strstr(mock.log,prefix));
    snprintf(prefix,sizeof(prefix),"sample_time stage=%s accepted_absolute_us=",stage);
    if (s->valid_mask) {
        snprintf(expected,sizeof(expected),"%s%llu\n",prefix,(unsigned long long)s->completed_us);
        CHECK(strstr(mock.log,expected));
    } else CHECK(!strstr(mock.log,prefix));
}
static void check_evidence(void)
{
    if (!capture_calls) return;
    ++evidence_checks; CHECK(capture_calls==1);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH transfer field stage=init ")==272);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH transfer field stage=guard ")==17);
    char expected[600]; const char *cursor=mock.log;
    snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer result status=%s reason=%s phase_ok=%u guard_ok=%u attempt=%u complete=%u quarantined=%u poisoned=%u\n",
        captured.status,captured.reason,captured.phase_accepted,captured.fresh_guard_accepted,captured.attempt_issued,
        captured.transfer_complete,captured.quarantined,captured.poisoned);
    CHECK(strstr(mock.log,expected));
    snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer progress run_issued=%u run_accepted=%u run_observed=%u input_done=%u stop=%u\n",
        captured.run_issued,captured.run_write_accepted,captured.run_observed,captured.input_done_observed,captured.stop_observed);
    CHECK(strstr(mock.log,expected));
    snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer counters reads=%u writes=%u polls=%u rx_reads=%u elapsed_us=%llu transaction_elapsed_us=%llu\n",
        captured.reads,captured.writes,captured.polls,captured.rx_reads,(unsigned long long)captured.elapsed_us,(unsigned long long)captured.transaction_elapsed_us);
    CHECK(strstr(mock.log,expected));
    static const char *const poll_names[5]={"VERSION","ERROR","STATUS","OPERATIONAL","STATE"};
    for (unsigned slot=0;slot<4;++slot) {
        const struct ltsp_poll_sample *p=&captured.phase.poll_samples[slot];
        const uint32_t raw[5]={p->version,p->error,p->status,p->operational,p->state};
        snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer init_poll slot=%u attempted=%u complete=%u valid=0x%02x from=%u to=%u index=%u\n",
            slot,p->attempted,p->complete,p->valid_mask,p->from,p->target,p->poll_index);
        CHECK(strstr(mock.log,expected));
        for (unsigned i=0;i<5;++i) {
            if (p->valid_mask&(1u<<i))
                snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=init_poll slot=%u index=%u reg=%s valid=1 value=0x%08x\n",slot,i,poll_names[i],raw[i]);
            else
                snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=init_poll slot=%u index=%u reg=%s valid=0 value=unread\n",slot,i,poll_names[i]);
            expect_next(&cursor,"LITHIUM TOUCH transfer field stage=init_poll ",expected);
        }
    }
    for (unsigned phase=0;phase<8;++phase) for (unsigned pass=0;pass<2;++pass)
        for (unsigned i=0;i<17;++i) {
            const struct ltsn_result *s=&captured.phase.snapshots[phase];
            if (s->valid_mask[pass]&(1u<<i))
                snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=init phase=%u pass=%u index=%u reg=%s valid=1 value=0x%08x\n",phase,pass,i,reg_names[i],s->values[pass][i]);
            else {
                CHECK(!s->values[pass][i]);
                snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=init phase=%u pass=%u index=%u reg=%s valid=0 value=unread\n",phase,pass,i,reg_names[i]);
            }
            expect_next(&cursor,"LITHIUM TOUCH transfer field stage=init ",expected);
        }
    for (unsigned i=0;i<17;++i) {
        if (captured.guard_valid_mask&(1u<<i))
            snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=guard index=%u reg=%s valid=1 value=0x%08x\n",i,reg_names[i],captured.guard_values[i]);
        else {
            CHECK(!captured.guard_values[i]);
            snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer field stage=guard index=%u reg=%s valid=0 value=unread\n",i,reg_names[i]);
        }
        expect_next(&cursor,"LITHIUM TOUCH transfer field stage=guard ",expected);
    }
    check_sample_log("pre_load0",&captured.pre_load[0]);
    check_sample_log("pre_load1",&captured.pre_load[1]);
    check_sample_log("pre_load2",&captured.pre_load[2]);
    check_sample_log("pre_run",&captured.pre_run);
    check_sample_log("completion",&captured.last_completion);
    check_sample_log("post_rx",&captured.post_rx);
    check_sample_log("stop",&captured.last_stop);
    check_sample_log("first_failure",&captured.first_failed_sample);
    CHECK(ltstp_write_count==mock.writes);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH transfer write ")==mock.writes);
    cursor=mock.log;
    for (unsigned i=0;i<mock.writes;++i) {
        CHECK(ltstp_write_trace[i].offset==expected_writes[i][0] && ltstp_write_trace[i].value==expected_writes[i][1]);
        snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer write index=%u off=0x%03x value=0x%08x returned_on_time=%u\n",
            i,expected_writes[i][0],expected_writes[i][1],ltstp_write_trace[i].returned_on_time);
        expect_next(&cursor,"LITHIUM TOUCH transfer write ",expected);
    }
    if (captured.rx_word_valid)
        snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer rx valid=1 word=0x%08x id_byte=0x%02x id_committed=%u nontrivial=%u identity_match=unverified\n",
            captured.raw_rx_word,captured.raw_id,captured.id_valid,captured.id_nontrivial);
    else snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer rx valid=0 word=unread id_byte=unread id_committed=0 identity_match=unverified\n");
    CHECK(strstr(mock.log,expected));
    if (captured.first_failed_offset_valid)
        snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer failure valid=1 access=%s off=0x%03x stage=%s\n",
            captured.first_failed_access,captured.first_failed_offset,captured.first_failed_stage);
    else snprintf(expected,sizeof(expected),"LITHIUM TOUCH transfer failure valid=0 access=unread off=unread\n");
    CHECK(strstr(mock.log,expected));
    CHECK(strstr(mock.log,"terminal_gic=unread quarantine=1"));
    CHECK(strstr(mock.log,"LITHIUM TOUCH transfer limits ack_waveform=unverified stop_waveform=unverified irq_attached=0 hid_events=0 recovery_reset=0 retry=0"));
    /* Native kernel printf has a bounded payload. Catch accidental long logs
     * using the actual expanded output, not the source format length. */
    for (const char *p=mock.log;*p;) {
        const char *end=strchr(p,'\n');
        CHECK(end && (size_t)(end-p)<=230);
        if (!end) break;
        p=end+1;
    }
}
static void check_trace(void)
{
    unsigned writes=0,rx=0;
    for (unsigned i=0;i<mock.events;++i) {
        uint32_t p=mock.trace[i].physical,off=p&0xfffu;
        if (mock.trace[i].kind=='W') {
            CHECK(writes<22);
            if (writes<22) CHECK(off==expected_writes[writes][0] && mock.trace[i].value==expected_writes[writes][1]);
            CHECK((p&~UINT32_C(0xfff))==0x075ba000); ++writes;
        }
        if (mock.trace[i].kind=='R' && (p&~UINT32_C(0xfff))==0x075ba000) {
            CHECK(read_allowed(off)); if (off==0x218) ++rx;
        }
    }
    CHECK(writes==mock.writes && writes<=22 && rx==mock.rx_reads && rx<=1);
    CHECK(mock.tx_words<=3 && mock.sw_resets<=1 && mock.run_count<=2 && mock.pause_count<=1);
    CHECK(ltid.reads==mock.reads && ltid.writes==mock.writes);
    CHECK(mock.reads+mock.writes<=4096);
    if (capture_calls) {
        CHECK(captured.id_valid==captured.transfer_complete);
        CHECK(!captured.id_nontrivial || captured.id_valid);
        CHECK(captured.quarantined && captured.no_reuse);
        if (captured.rx_word_valid) {
            CHECK(captured.raw_rx_completed_us>=captured.raw_rx_started_us);
            CHECK(captured.raw_rx_started_us>=captured.started_us);
        } else CHECK(!captured.raw_rx_started_us && !captured.raw_rx_completed_us);
    }
    CHECK(!mock.after_poison && !mock.post_core_external && !mock.callback_after_false);
}
static void seal(void)
{
    unsigned calls=mock.external_calls,events=mock.events;
    size_t logged=mock.log_size; const char *reason=ltid.reason;
    probe();
    if (ltid.started) { lithium_touch_id_probe(); lithium_touch_qup_phase_probe(); }
    CHECK(mock.external_calls==calls && mock.events==events && mock.log_size==logged);
    if (capture_calls) {
        uint32_t out=0xfeedface;
        CHECK(!bound_io.read32(bound_io.cookie,0x030,&out) && out==0xfeedface);
        CHECK(!bound_io.read32(bound_io.cookie,0x218,&out) && out==0xfeedface);
        for (unsigned i=0;i<22;++i)
            CHECK(!bound_io.write32(bound_io.cookie,expected_writes[i][0],expected_writes[i][1]));
        (void)bound_io.now_us(bound_io.cookie); bound_io.delay_us(bound_io.cookie,10);
    }
    CHECK(mock.external_calls==calls && mock.events==events);
    CHECK(ltid.reason==reason && !strstr(mock.log,"valid=0 value=0x"));
    check_trace(); check_evidence();
}
static void successful(void)
{
    CHECK(capture_calls==1 && captured.phase_accepted && captured.fresh_guard_accepted);
    CHECK(captured.phase.writes==18);
    if (!mock.transition_wait) CHECK(captured.phase.reads==411 && captured.phase.polls==4);
    else CHECK(captured.phase.reads>411 && captured.phase.polls>4);
    CHECK(captured.transfer_complete && captured.stop_observed && captured.run_observed);
    CHECK(captured.input_done_observed && captured.id_valid && captured.rx_word_valid);
    CHECK(captured.raw_rx_word==mock.rx_word && captured.raw_id==((mock.rx_word>>16)&0xffu));
    CHECK(captured.id_nontrivial==(captured.raw_id!=0 && captured.raw_id!=0xff));
    CHECK(captured.quarantined && captured.no_reuse && !captured.poisoned);
    CHECK(mock.writes==22 && mock.tx_words==3 && mock.rx_reads==1);
    CHECK(mock.run_count==2 && mock.pause_count==1 && mock.sw_resets==1);
    CHECK(*word(0x075ba004)==0x11d);
    CHECK(captured.write_trace_count==4 && captured.transaction_writes==4);
    CHECK(captured.rx_reads==1 && captured.writes==22 && captured.reads==mock.qup_reads);
    for (unsigned i=0;i<4;++i) {
        const struct ltst_write_trace *t=&captured.write_trace[i];
        CHECK(t->attempted && t->callback_ok && t->accepted);
        CHECK(t->offset==expected_writes[i+18][0] && t->value==expected_writes[i+18][1]);
        CHECK(t->completed_us>=t->started_us);
    }
    for (unsigned i=0;i<3;++i) CHECK(mock.gic_reads[i]==23);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH transfer begin ")==1);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH transfer result ")==1);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH transfer end ")==1);
}
static void test_success(void)
{
    case_name="real-R31-plus-TX3-second-RUN-RX-STOP";
    const uint32_t ids[]={0,1,0x54,0xab,0xff};
    for (unsigned i=0;i<5;++i) for (unsigned pair=0;pair<2;++pair) {
        reset_fixture(); mock.observed_pair=pair!=0;
        mock.rx_word=(ids[i]<<16)|0xbeef;
        if (!pair) mock.completion_status=0x0c000000;
        probe(); successful(); seal();
    }
    const char kinds[]={'T','R','W','B','M','L','C','E','A','N'};
    for (unsigned i=0;i<sizeof(kinds);++i) {
        reset_fixture(); mock.reenter_kind=kinds[i]; mock.reenter_at=1;
        probe(); CHECK(mock.reentered); successful(); seal();
    }
    reset_fixture(); mock.complete_wait=2; mock.stop_wait=3;
    mock.reenter_kind='D'; mock.reenter_at=1; probe();
    CHECK(mock.reentered && mock.delays>=4); successful(); seal();
}
static void test_context_mapping(void)
{
    case_name="invalid-context-mappings-shared-latch";
    for (unsigned c=0;c<12;++c) {
        reset_fixture();
        switch(c) {
        case 0:lts1.started=false;break; case 1:lts1.poisoned=true;break;
        case 2:lts1.clocks_ready=false;break;case 3:lts1.pins_ready=false;break;
        case 4:mock.affinity=0;break;case 5:mock.affinity=0x100;break;
        case 6:mock.affinity=0x101;break;case 7:mock.max_cpu=1;break;
        case 8:mock.max_cpu=-1;break;case 9:mock.irq_context=1;break;
        case 10:mock.irq_enabled=0;break;default:ltid.poisoned=true;break;
        }
        probe(); CHECK(ltid.started==(c!=11) && ltid.poisoned && !mock.events && !capture_calls); seal();
    }
    reset_fixture(); ltid.started=true; probe();
    CHECK(!mock.external_calls && !mock.log_size); seal();
    for (unsigned i=0;i<8;++i) {
        reset_fixture(); lts1.windows[i].mapped=0; probe();
        CHECK(ltid.poisoned && !mock.events && !capture_calls); seal();
    }
    for (unsigned i=0;i<6;++i) {
        reset_fixture(); lts1.windows[i].physical=0; probe();
        CHECK(ltid.poisoned && !mock.maps && !mock.writes); seal();
    }
    const unsigned counts[]={0,9,UINT32_MAX};
    for (unsigned i=0;i<3;++i) {
        reset_fixture(); lts1.mappings=counts[i]; probe();
        CHECK(ltid.poisoned && !mock.events && !capture_calls); seal();
    }
    for (unsigned i=0;i<9;++i) {
        reset_fixture(); *word(board[i].physical)^=board[i].bad_bit; probe();
        CHECK(ltid.poisoned && mock.reads==i+1 && !mock.maps && !mock.writes); seal();
    }
    reset_fixture(); mock.map_fail=true; probe();
    CHECK(ltid.poisoned && mock.maps==1 && mock.reads==9 && !capture_calls); seal();
}
static void test_gic(void)
{
    case_name="GIC-138-entry-and-before-all-22-writes";
    const uint32_t regs[]={0x09bc0110,0x09bc0210,0x09bc0310};
    for (unsigned r=0;r<3;++r) {
        reset_fixture(); *word(regs[r])=0x400; probe();
        CHECK(ltid.poisoned && !mock.qup_reads && !mock.writes && !capture_calls); seal();
        for (unsigned before=0;before<22;++before) {
            reset_fixture(); mock.trip_register=r+1; mock.trip_write=before; probe();
            CHECK(ltid.poisoned && mock.writes==before);
            CHECK(captured.first_failed_offset_valid && !strcmp(captured.first_failed_access,"write"));
            CHECK(captured.first_failed_offset==expected_writes[before][0]); seal();
        }
    }
    reset_fixture(); *word(regs[0])=*word(regs[1])=*word(regs[2])=0x800;
    probe(); successful(); seal();
}
static void test_physical_faults(void)
{
    case_name="physical-callback-late-clock-rollback-boundaries";
    reset_fixture(); probe(); successful();
    unsigned total=mock.external_calls;
    static struct { char kind; unsigned count,writes,reads; } baseline[24000];
    memcpy(baseline,mock.external,total*sizeof(baseline[0]));
    unsigned selected=0;
    for (unsigned i=0;i<total;++i) {
        char k=baseline[i].kind;
        bool test=k=='R' || k=='W' || k=='M' ||
            ((k=='T' || k=='B') && baseline[i].writes>=18) ||
            ((k=='T' || k=='B') && baseline[i].count<=8);
        if (k=='T' && baseline[i].count==1) test=false; /* origin has no prior sample */
        if (!test) continue;
        ++selected;
        for (unsigned mode=1;mode<=2;++mode) {
            reset_fixture(); mock.fault_kind=k; mock.fault_at=baseline[i].count;
            mock.fault_mode=mode; probe();
            CHECK(mock.fired && !captured.transfer_complete);
            CHECK(ltid.poisoned || captured.poisoned); seal();
        }
    }
    printf("physical faults: %u independently replayed callback positions x late/rollback\n",selected);
}
static void test_callback_failures(void)
{
    case_name="core-bound-callback-false-before-and-after-physical-IO";
    reset_fixture(); probe(); successful();
    unsigned reads=mock.callback_counts[0],writes=mock.callback_counts[1];
    for (unsigned n=1;n<=reads;++n) for (unsigned mode=1;mode<=2;++mode) {
        reset_fixture(); mock.callback_kind='r'; mock.callback_at=n; mock.callback_mode=mode;
        probe(); CHECK(mock.callback_failed && captured.poisoned && !captured.transfer_complete);
        CHECK(mock.callback_counts[0]==n && !mock.callback_after_false); seal();
    }
    for (unsigned n=1;n<=writes;++n) for (unsigned mode=1;mode<=2;++mode) {
        reset_fixture(); mock.callback_kind='w'; mock.callback_at=n; mock.callback_mode=mode;
        probe(); CHECK(mock.callback_failed && captured.poisoned && !captured.transfer_complete);
        CHECK(mock.writes==n-(mode==1?1u:0u));
        CHECK(mock.callback_counts[1]==n && !mock.callback_after_false); seal();
    }
    for (unsigned mode=1;mode<=2;++mode) {
        reset_fixture(); mock.callback_kind='t'; mock.callback_at=2; mock.callback_mode=mode;
        probe(); CHECK(captured.poisoned && !captured.transfer_complete); seal();
    }
}
static void test_delays(void)
{
    case_name="delay-callback-late-rollback-in-initializer-completion-STOP";
    for (unsigned stage=0;stage<3;++stage) {
        reset_fixture();
        if (!stage) mock.transition_wait=2;
        else if (stage==1) mock.complete_wait=2;
        else mock.stop_wait=3;
        probe(); successful(); unsigned delays=mock.delays;
        for (unsigned n=1;n<=delays;++n) for (unsigned mode=1;mode<=2;++mode) {
            reset_fixture();
            if (!stage) mock.transition_wait=2;
            else if (stage==1) mock.complete_wait=2;
            else mock.stop_wait=3;
            mock.fault_kind='D'; mock.fault_at=n; mock.fault_mode=mode;
            probe(); CHECK(mock.fired && !captured.transfer_complete);
            CHECK(ltid.poisoned || captured.poisoned); seal();
        }
    }
}
static void direct_fixture(unsigned index)
{
    reset_fixture(); ltid.start=ltid.last=mock.now;
    ltid.qup=(vm_offset_t)&mock.words[5][0]; ltid.gic=(vm_offset_t)&mock.words[8][0];
    ltid.started=ltstp_active=true; mock.gic_mapped=mock.policy_only=true;
    ltstp_write_count=index;
    if (index>=16) *word(0x075ba004)=0x11d;
    if (index>=18) {
        mock.run_count=mock.pause_count=1;
        mock.tx_words=index<=21?index-18:3;
        *word(0x075ba004)=0x11f;
    }
    if (index==22) { mock.run_count=2; mock.rx_left=true; *word(0x075ba004)=0x11d; }
}
static void test_numeric_policy(void)
{
    case_name="independent-ordered22-17register-plus-RX-numeric-policy";
    for (unsigned i=0;i<24;++i) for (uint32_t off=0;off<4096;++off) {
        bool want=i<22 && off==expected_writes[i][0];
        uint32_t value=i<22?expected_writes[i][1]:1;
        CHECK(ltstp_write_policy(i,off,value)==want);
    }
    for (unsigned i=0;i<22;++i) {
        for (unsigned j=0;j<22;++j) {
            bool want=expected_writes[j][0]==expected_writes[i][0] && expected_writes[j][1]==expected_writes[i][1];
            CHECK(ltstp_write_policy(i,expected_writes[j][0],expected_writes[j][1])==want);
        }
        for (unsigned bit=0;bit<32;++bit) {
            uint32_t off=expected_writes[i][0],val=expected_writes[i][1];
            CHECK(!ltstp_write_policy(i,off,val^(UINT32_C(1)<<bit)));
            CHECK(!ltstp_write_policy(i,off^(UINT32_C(1)<<bit),val));
            direct_fixture(i);
            CHECK(!ltstp_write(NULL,off,val^(UINT32_C(1)<<bit)) && ltid.poisoned && !mock.events);
            CHECK(!mock.external_calls && ltstp_write_count==i);
        }
        direct_fixture(i); CHECK(ltstp_write(NULL,expected_writes[i][0],expected_writes[i][1]));
        CHECK(mock.writes==1 && mock.reads==3 && ltstp_write_count==i+1);
        unsigned events=mock.events,calls=mock.external_calls;
        CHECK(!ltstp_write(NULL,expected_writes[i][0],expected_writes[i][1]));
        CHECK(ltid.poisoned && mock.events==events && mock.external_calls==calls);
    }
    for (uint32_t off=0;off<4096;++off) {
        direct_fixture(22); uint32_t out=0xfeedface;
        bool ok=ltstp_read(NULL,off,&out);
        CHECK(ok==read_allowed(off)); CHECK(mock.reads==(ok?1u:0u));
        if (!ok) CHECK(out==0xfeedface && !mock.events);
    }
    for (unsigned i=0;i<22;++i) {
        direct_fixture(i); uint32_t out=0xfeedface;
        CHECK(!ltstp_read(NULL,0x218,&out) && out==0xfeedface && ltid.poisoned && !mock.events);
    }
    direct_fixture(22); uint32_t out=0xfeedface;
    CHECK(ltstp_read(NULL,0x218,&out) && mock.rx_reads==1);
    unsigned events=mock.events,calls=mock.external_calls;
    CHECK(!ltstp_read(NULL,0x218,&out) && mock.events==events && mock.external_calls==calls);
    const uint32_t offsets[]={0x1000,0xfffffffc,UINT32_MAX};
    for (unsigned i=0;i<3;++i) {
        direct_fixture(0); out=0xfeedface;
        CHECK(!ltstp_read(NULL,offsets[i],&out) && out==0xfeedface && !mock.events);
        direct_fixture(0); CHECK(!ltstp_write(NULL,offsets[i],1) && !mock.events);
    }
    direct_fixture(0); CHECK(!ltstp_read(NULL,0x030,NULL) && !mock.events);
    direct_fixture(0); ltid.qup=0; CHECK(!ltstp_read(NULL,0x030,&out) && !mock.events);
    direct_fixture(0); ltid.gic=0; CHECK(!ltstp_write(NULL,0x00c,1) && !mock.events);
    direct_fixture(0); ltstp_delay(NULL,11); CHECK(ltid.poisoned && !mock.external_calls);
    direct_fixture(0); ltid.poisoned=true; out=0xfeedface;
    CHECK(!ltstp_read(NULL,0x030,&out) && out==0xfeedface);
    CHECK(!ltstp_write(NULL,0x00c,1)); ltstp_delay(NULL,1);
    CHECK(ltstp_now(NULL)==UINT64_MAX && !mock.external_calls);
}
static void test_outer_limits(void)
{
    case_name="same-outer-100ms-4096-including-GIC-limits";
    uint32_t out=0xfeedface;
    direct_fixture(0); mock.now=100999; CHECK(ltstp_read(NULL,0x030,&out));
    direct_fixture(0); mock.now=101000; out=0xfeedface;
    CHECK(!ltstp_read(NULL,0x030,&out) && out==0xfeedface && !mock.events);
    CHECK(!strcmp(ltid.reason,"adapter_deadline"));
    direct_fixture(0); mock.now=100999; mock.read_step_us=1; out=0xfeedface;
    CHECK(!ltstp_read(NULL,0x030,&out) && out==0xfeedface && mock.reads==1);
    CHECK(!strcmp(ltid.reason,"adapter_deadline"));
    for (unsigned n=4094;n<=4096;++n) {
        direct_fixture(0); ltid.reads=n; out=0xfeedface;
        bool ok=ltstp_read(NULL,0x030,&out);
        CHECK(ok==(n==4094)); CHECK(mock.reads==(n<4096?1u:0u));
        if (!ok) CHECK(out==0xfeedface && !strcmp(ltid.reason,"adapter_access_cap"));
    }
    for (unsigned n=4091;n<=4096;++n) {
        direct_fixture(0); ltid.reads=n;
        bool ok=ltstp_write(NULL,0x00c,1);
        CHECK(ok==(n==4091));
        CHECK(mock.writes==(n<=4092?1u:0u));
        CHECK(mock.reads+mock.writes<=4096-n);
        if (n>=4092) CHECK(ltid.poisoned && !strcmp(ltid.reason,"adapter_access_cap"));
    }
    direct_fixture(22); mock.fault_kind='R'; mock.fault_at=1; mock.fault_mode=1;
    out=0xfeedface; CHECK(!ltstp_read(NULL,0x218,&out) && out==0xfeedface);
    CHECK(mock.rx_reads==1 && ltstp_fifo_reads==1 && ltid.poisoned);
    unsigned calls=mock.external_calls;
    CHECK(!ltstp_read(NULL,0x218,&out) && mock.external_calls==calls && mock.rx_reads==1);
    for (unsigned line=1;line<=2;++line) for (unsigned mode=1;mode<=2;++mode) {
        reset_fixture(); mock.fault_kind='L'; mock.fault_at=line; mock.fault_mode=mode;
        probe(); CHECK(mock.fired && ltid.poisoned && mock.reads==9);
        CHECK(!mock.maps && !mock.writes && !capture_calls); seal();
    }
}
static void check_read_layout(unsigned *first_transfer,unsigned starts[7])
{
    /* Independent successful callback order: 411 unchanged R31 reads,
     * fresh17 STATE last, four seven-field mutation guards, completion7,
     * RX once, post-RX7, STOP7. */
    CHECK(mock.core_read_count==478 && captured.reads==478 && mock.reads==556);
    CHECK(captured.transaction_reads==67 && captured.accesses==500);
    unsigned n=411;
    for (unsigned j=0;j<17;++j) {
        unsigned i=j==16?1u:j>=1?j+1:j;
        CHECK(mock.core_reads[n].offset==snapshot_offsets[i] && mock.core_reads[n].writes==18); ++n;
    }
    *first_transfer=411;
    for (unsigned sample=0;sample<5;++sample) {
        starts[sample]=n;
        unsigned writes=sample<3?sample+18:sample==3?21u:22u;
        for (unsigned j=0;j<7;++j) {
            CHECK(mock.core_reads[n].offset==sample_offsets[j] && mock.core_reads[n].writes==writes); ++n;
        }
    }
    CHECK(mock.core_reads[n].offset==0x218 && mock.core_reads[n].writes==22); ++n;
    for (unsigned sample=5;sample<7;++sample) {
        starts[sample]=n;
        for (unsigned j=0;j<7;++j) {
            CHECK(mock.core_reads[n].offset==sample_offsets[j] && mock.core_reads[n].writes==22); ++n;
        }
    }
    CHECK(n==478);
}
static void test_guard_and_validity(void)
{
    case_name="fresh17-eachFIFO-secondRUN-RX-STOP-guards-prefix-validity";
    reset_fixture(); probe(); successful();
    unsigned first=0,starts[7]; check_read_layout(&first,starts);
    unsigned physical[478]; uint32_t raw[478];
    for (unsigned i=0;i<478;++i) {
        physical[i]=mock.core_reads[i].physical_read; raw[i]=mock.core_reads[i].value;
    }
    for (unsigned j=0;j<17;++j) {
        unsigned index=first+j;
        uint32_t off=j==16?0x004:snapshot_offsets[j>=1?j+1:j];
        reset_fixture(); mock.mutate_physical=0x075ba000+off;
        mock.mutate_value=raw[index]^1u; mock.mutate_at_read=physical[index];
        probe(); CHECK(captured.phase_accepted && !captured.fresh_guard_accepted);
        CHECK(mock.writes==18 && !mock.tx_words && !mock.rx_reads && !captured.transfer_complete); seal();
    }
    const uint32_t prefix[7]={0,1,3,7,15,47,111};
    for (unsigned sample=0;sample<7;++sample) for (unsigned j=0;j<7;++j) {
        unsigned index=starts[sample]+j;
        for (unsigned mode=1;mode<=2;++mode) {
            reset_fixture(); mock.callback_kind='r'; mock.callback_at=index+1; mock.callback_mode=mode;
            probe(); CHECK(captured.first_failed_sample_valid);
            const struct ltst_sample *s=&captured.first_failed_sample;
            CHECK(s->attempted && !s->complete && s->valid_mask==prefix[j]);
            CHECK(captured.first_failed_offset_valid && captured.first_failed_offset==sample_offsets[j]);
            CHECK(!strcmp(captured.first_failed_access,"read") && captured.poisoned);
            CHECK(!captured.id_valid && !captured.id_nontrivial && !captured.transfer_complete); seal();
        }
        if (j==5 || (sample==4 && j==4)) continue; /* raw OUT occupancy and completion IN are observations */
        reset_fixture(); mock.mutate_physical=0x075ba000+sample_offsets[j];
        mock.mutate_at_read=physical[index];
        if (j==0) mock.mutate_value=0;
        else if (j==1 || j==4) mock.mutate_value=1;
        else if (j==2) mock.mutate_value=raw[index]|8u;
        else if (j==3) mock.mutate_value=raw[index]|0x1000u;
        else mock.mutate_value=0x11c;
        probe(); CHECK(!captured.transfer_complete && !captured.id_valid);
        CHECK(captured.first_failed_sample_valid && captured.first_failed_sample.valid_mask==0x7f);
        unsigned want=sample<3?sample+18:sample==3?21u:22u;
        CHECK(mock.writes==want && mock.rx_reads==(sample>=5?1u:0u)); seal();
    }
    const uint32_t status[]={0x0c110008,0x0c110004,0x0c110002};
    for (unsigned i=0;i<3;++i) {
        reset_fixture(); mock.completion_status=status[i]; probe();
        CHECK(!captured.transfer_complete && !mock.rx_reads && mock.writes==22);
        CHECK(captured.first_failed_sample_valid); seal();
    }
    reset_fixture(); mock.completion_op=0x800; probe();
    CHECK(!captured.transfer_complete && !mock.rx_reads && !strcmp(captured.reason,"input_done_without_rx")); seal();
    reset_fixture(); mock.complete_wait=300; probe();
    CHECK(captured.poisoned && !captured.transfer_complete && !mock.rx_reads && captured.polls==256);
    CHECK(captured.completion_polls==256); seal();
    reset_fixture(); mock.stop_wait=300; probe();
    CHECK(captured.poisoned && captured.rx_word_valid && !captured.id_valid && !captured.id_nontrivial);
    CHECK(!captured.transfer_complete && captured.polls==256 && captured.completion_polls==1);
    CHECK(captured.stop_polls==255 && mock.rx_reads==1); seal();
}
static void test_transaction_deadline(void)
{
    case_name="returning-20ms-new-transfer-faults-retain-quarantine";
    reset_fixture(); probe(); successful();
    unsigned physical[478];
    for (unsigned i=0;i<478;++i) physical[i]=mock.core_reads[i].physical_read;
    for (unsigned i=411;i<478;++i) {
        reset_fixture(); mock.fault_kind='R'; mock.fault_at=physical[i]; mock.fault_mode=3;
        probe(); CHECK(mock.fired && captured.poisoned && !captured.transfer_complete);
        CHECK(!captured.id_valid && !captured.id_nontrivial);
        CHECK(!strcmp(captured.reason,"transaction_deadline"));
        CHECK(mock.writes<=22 && mock.rx_reads<=1); seal();
    }
    for (unsigned n=19;n<=22;++n) {
        reset_fixture(); mock.fault_kind='W'; mock.fault_at=n; mock.fault_mode=3;
        probe(); CHECK(mock.fired && captured.poisoned && !captured.transfer_complete);
        CHECK(mock.writes==n && captured.write_trace_count==n-18);
        CHECK(captured.write_trace[n-19].callback_ok && !captured.write_trace[n-19].accepted);
        CHECK(!captured.id_valid && !mock.rx_reads); seal();
    }
    for (unsigned stage=0;stage<2;++stage) {
        reset_fixture(); if (!stage) mock.complete_wait=2; else mock.stop_wait=3;
        mock.fault_kind='D'; mock.fault_at=1; mock.fault_mode=3;
        probe(); CHECK(mock.fired && captured.poisoned && !captured.transfer_complete);
        CHECK(!captured.id_valid && !captured.id_nontrivial); seal();
    }
    reset_fixture(); mock.read_step_us=200; probe();
    CHECK(captured.phase_accepted && captured.fresh_guard_accepted);
    CHECK(ltid.poisoned && !strcmp(ltid.reason,"adapter_deadline"));
    CHECK(!captured.transfer_complete && captured.transaction_elapsed_us<20000);
    CHECK(mock.reads==500 && mock.writes==18 && !mock.tx_words && !mock.rx_reads); seal();
}
static int sample_log(const char *name)
{
    reset_fixture();
    if (!strcmp(name,"sample-stop-failure")) mock.stop_wait=300;
    else if (!strcmp(name,"sample-rx-late")) {
        /* Numeric successful physical count: 9 board+3 entry GIC+
         * 411 R31+54 R31 prewrite GIC+17 fresh+21 preload+9 FIFO GIC+
         * 7 preRUN+3 RUN GIC+7 completion = 541 before RX. */
        mock.fault_kind='R'; mock.fault_at=542; mock.fault_mode=3;
    } else if (!strcmp(name,"sample-preload-partial")) {
        mock.callback_kind='r'; mock.callback_at=428+4; mock.callback_mode=1;
    } else if (strcmp(name,"sample-success")) return 2;
    probe();
    if (!strcmp(name,"sample-success")) successful();
    else if (!strcmp(name,"sample-stop-failure")) {
        CHECK(captured.rx_word_valid && !captured.id_valid && !captured.transfer_complete && captured.polls==256);
    } else if (!strcmp(name,"sample-rx-late")) {
        CHECK(mock.rx_reads==1 && !captured.rx_word_valid && !captured.id_valid && captured.poisoned);
        CHECK(captured.first_failed_offset_valid && captured.first_failed_offset==0x218);
    } else CHECK(captured.first_failed_sample_valid && captured.first_failed_sample.valid_mask==7);
    seal(); fputs(mock.log,stdout);
    printf("sample=%s complete=%u id_valid=%u raw_rx_valid=%u physical_reads=%u physical_writes=%u external_times=%u failures=%u\n",
        name,captured.transfer_complete,captured.id_valid,captured.rx_word_valid,mock.reads,mock.writes,mock.times,failures);
    return failures?1:0;
}
#endif
int main(int argc,char **argv)
{
    (void)argc; (void)argv;
    (void)lithium_touch_id_probe;
    (void)lithium_touch_qup_phase_probe;
#if R32_FIXTURE_ONLY
    test_upfront_fixture();
#else
    (void)sample_offsets;
    if (argc==2) return sample_log(argv[1]);
    test_success(); test_context_mapping(); test_gic();
    test_physical_faults(); test_callback_failures(); test_delays();
    test_numeric_policy(); test_outer_limits();
    test_guard_and_validity();
    test_transaction_deadline();
#endif
    printf("R32 actual adapter: %u scenarios, %u failed checks, %u assertions; evidence_runs=%u mode=%s\n",
        scenarios,failures,assertions,evidence_checks,R32_FIXTURE_ONLY?"upfront-fixture":"actual-production");
    return failures?1:0;
}
