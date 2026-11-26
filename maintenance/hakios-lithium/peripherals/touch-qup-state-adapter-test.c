/* R30 actual adapter/core host tests. Only inert RAM, context and clock mocks.
 * No device APIs, real MMIO, FIFO data, ID traffic or hardware claims. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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
/* Opt in BEFORE the independent header's first include. Include guards make
 * a later opt-in ineffective; never include a default-off snapshot first. */
#define LTSN_EXPERIMENTAL 1
#define LTSQ_EXPERIMENTAL 1
#include "../../../ports/hakios-lithium/peripherals/touch-qup-state-core.h"
#undef LTSQ_EXPERIMENTAL
#undef LTSN_EXPERIMENTAL
static struct ltsq_result captured;
static struct ltsq_io bound_io, original_io;
static unsigned capture_calls;
static struct ltsq_result observed_initialize(struct ltsq_io *io);
#define ltsq_observe_states(io) observed_initialize(io)
#include "../../../ports/hakios-lithium/peripherals/touch-qup-state.inc"
#undef ltsq_observe_states
#undef printf

static const uint32_t pages[9] = {
    0x00325000, 0x00330000, 0x00352000, 0x01067000, 0x01068000,
    0x075ba000, 0x01079000, 0x0108d000, 0x09bc0000
};
static const struct { uint32_t physical, good, bad_bit; } board[] = {
    {0x00325000, 0, 1}, {0x00330000, 0, 1}, {0x00330020, 0, 1},
    {0x00352004, 0x8000, 0x8000}, {0x00325004, 0, 0x80000000},
    {0x00330008, 1, 1}, {0x01067000, 0x0d, 4},
    {0x01068000, 0x0d, 4}, {0x00330024, 0, 2}
};
/* Independent numeric oracle: no production policy helper/table is used. */
static const uint32_t snapshot_offsets[17] = {
    0x030,0x004,0x000,0x008,0x018,0x01c,0x020,0x028,0x10c,
    0x214,0x100,0x200,0x208,0x150,0x408,0x400,0x404
};
static const char *const phases[8] = {
    "pre","post_reset","base","setup","run","clock","pause","terminal"
};
static const uint32_t expected_writes[18][2] = {
    {0x00c,1},{0x004,0},{0x000,0x207},{0x020,0x3c},{0x028,0x300},
    {0x408,1},{0x020,0},{0x028,0x300},{0x200,0},{0x100,0},
    {0x208,3},{0x150,9},{0x000,0x207},{0x008,0xc0a5},{0x408,1},
    {0x004,1},{0x400,0x003e007c},{0x004,3}
};
static struct {
    uint32_t words[9][1024];
    struct { char kind; uint32_t physical, value; } trace[18000];
    unsigned events, reads, writes, qup_reads, maps, barriers, delays, times;
    unsigned gic_reads[3], sw_resets, state_writes, run_count, pause_count;
    unsigned fault_at, fault_mode, after_fault, writes_after_fault;
    unsigned trip_write, trip_register, transition_wait, wait_left;
    unsigned callback_counts[4], callback_after_false, callback_at, callback_mode;
    unsigned post_core_times, post_core_events;
    char fault_kind, reenter_kind, callback_kind;
    bool fired, reentered, map_fail, gic_mapped, policy_only, core_returned;
    bool callback_failed, hold_state, corrupt_readback;
    uint32_t full_after_reset, mutate_physical, mutate_value, mutate_after_writes;
    uint64_t now, affinity;
    int irq_context, irq_enabled, max_cpu;
    char log[131072];
    size_t log_size;
} mock;
static unsigned scenarios, failures, assertions, evidence_checks;
static const char *case_name;
#define CHECK(expr) do { ++assertions; if (!(expr)) { \
    if (failures < 32) fprintf(stderr,"FAIL %s scenario=%u line=%d: %s\n", \
        case_name,scenarios,__LINE__,#expr); ++failures; \
} } while (0)

static uint32_t *word(uint32_t physical)
{
    assert(!(physical & 3u));
    for (unsigned i=0;i<9;++i)
        if ((physical & ~UINT32_C(0xfff)) == pages[i])
            return &mock.words[i][(physical & 0xfffu)/4];
    assert(!"physical address escaped fixture"); return NULL;
}
static uint32_t decode(vm_offset_t address)
{
    for (unsigned i=0;i<9;++i) {
        uintptr_t base=(uintptr_t)&mock.words[i][0];
        if (address>=base && address-base<4096) {
            assert((address-base)%4==0);
            assert(i!=8 || mock.gic_mapped);
            return pages[i]+(uint32_t)(address-base);
        }
    }
    assert(!"access outside published windows"); return 0;
}
static bool read_allowed(uint32_t off)
{
    for (unsigned i=0;i<17;++i) if (off==snapshot_offsets[i]) return true;
    return false;
}
static bool write_allowed(uint32_t off,uint32_t value)
{
    switch (off) {
    case 0x00c: return value==1;
    case 0x004: return value==0 || value==1 || value==3;
    case 0x000: return value==0x207;
    case 0x020: return value==0 || value==0x3c;
    case 0x028: return value==0x300;
    case 0x408: return value==1;
    case 0x100: case 0x200: return value==0;
    case 0x208: return value==3;
    case 0x150: return value==9;
    case 0x008: return value==0xc0a5;
    case 0x400: return value==0x003e007c;
    default: return false;
    }
}
static void event(char kind,uint32_t physical,uint32_t value)
{
    assert(mock.events<sizeof(mock.trace)/sizeof(mock.trace[0]));
    if (mock.core_returned) ++mock.post_core_events;
    if (mock.fired) { ++mock.after_fault; if (kind=='W') ++mock.writes_after_fault; }
    mock.trace[mock.events].kind=kind;
    mock.trace[mock.events].physical=physical;
    mock.trace[mock.events++].value=value;
}
static void hook(char kind,unsigned count)
{
    if (mock.reenter_kind==kind && !mock.reentered) {
        mock.reentered=true; lithium_touch_qup_state_probe();
    }
    if (mock.fault_kind!=kind || mock.fault_at!=count) return;
    mock.fired=true;
    if (mock.fault_mode==1) mock.now+=300000;
    else if (mock.fault_mode==2) mock.now=999;
    else mock.now+=20000;
}
static uint64_t lts1_now(void)
{
    if (mock.core_returned) ++mock.post_core_times;
    hook('T',++mock.times); return mock.now;
}
static int ml_at_interrupt_context(void) { return mock.irq_context; }
static int ml_get_interrupts_enabled(void) { return mock.irq_enabled; }
static int ml_get_max_cpu_number(void) { return mock.max_cpu; }
static uint64_t mock_affinity(void) { return mock.affinity; }
static vm_offset_t mock_map(vm_offset_t physical,vm_size_t size)
{
    assert(physical==0x09bc0000 && size==4096 && !mock.gic_mapped);
    event('M',(uint32_t)physical,(uint32_t)size); hook('M',++mock.maps);
    if (mock.map_fail) return 0;
    mock.gic_mapped=true; return (vm_offset_t)&mock.words[8][0];
}
static uint32_t mock_read(vm_offset_t address)
{
    uint32_t p=decode(address), off=p&0xfffu, page=p&~UINT32_C(0xfff);
    uint32_t value=*word(p);
    if (page==0x09bc0000) {
        assert(off==0x110 || off==0x210 || off==0x310);
        unsigned reg=off==0x110?0u:off==0x210?1u:2u;
        ++mock.gic_reads[reg];
        if (mock.trip_register==reg+1 && mock.qup_reads &&
            mock.writes>=mock.trip_write) value|=0x400;
    } else if (page==0x075ba000) {
        assert(read_allowed(off)); /* In particular NEVER 110 or 218. */
        ++mock.qup_reads;
        if (!mock.policy_only && off==0x004 && mock.wait_left) {
            --mock.wait_left; value&=~4u;
        }
        if (mock.mutate_physical==p && mock.writes>=mock.mutate_after_writes)
            value=mock.mutate_value;
    } else {
        bool allowed=false;
        for (unsigned i=0;i<9;++i) if (p==board[i].physical) allowed=true;
        assert(allowed);
    }
    event('R',p,value); hook('R',++mock.reads); return value;
}
static void mock_write(vm_offset_t address,uint32_t value)
{
    uint32_t p=decode(address), off=p&0xfffu;
    assert((p&~UINT32_C(0xfff))==0x075ba000 && write_allowed(off,value));
    assert(mock.events>=3);
    assert(mock.trace[mock.events-3].kind=='R' && mock.trace[mock.events-3].physical==0x09bc0110);
    assert(mock.trace[mock.events-2].kind=='R' && mock.trace[mock.events-2].physical==0x09bc0310);
    assert(mock.trace[mock.events-1].kind=='R' && mock.trace[mock.events-1].physical==0x09bc0210);
    if (!mock.policy_only) {
        assert(mock.writes<18 && off==expected_writes[mock.writes][0] && value==expected_writes[mock.writes][1]);
        if (off==0x00c) {
            assert(!mock.sw_resets); ++mock.sw_resets;
            *word(0x075ba018)=mock.full_after_reset;
            mock.wait_left=mock.transition_wait;
        }
        if (off==0x004) {
            ++mock.state_writes;
            if (value==1) ++mock.run_count;
            if (value==3) ++mock.pause_count;
            mock.wait_left=mock.transition_wait;
        }
        if (off==0x400) assert((*word(0x075ba004)&7u)==5u);
    }
    if (!(mock.hold_state && off==0x004))
        *word(p)=off==0x004?(0x11cu|value):value;
    if (mock.corrupt_readback && off==0x000) *word(p)^=1;
    event('W',p,value); hook('W',++mock.writes);
}
static void mock_barrier(void) { hook('B',++mock.barriers); }
static void mock_delay(uint32_t us)
{
    assert(us && us<=10); mock.now+=us; hook('D',++mock.delays);
}
static int mock_printf(const char *format,...)
{
    va_list ap; va_start(ap,format);
    size_t left=sizeof(mock.log)-mock.log_size;
    int n=vsnprintf(mock.log+mock.log_size,left,format,ap); va_end(ap);
    assert(n>=0 && (size_t)n<left); mock.log_size+=(size_t)n;
    hook('L',(unsigned)mock.log_size); return n;
}

static bool callback_hit(unsigned index,char kind)
{
    if (mock.callback_failed) ++mock.callback_after_false;
    unsigned n=++mock.callback_counts[index];
    return mock.callback_kind==kind && mock.callback_at==n;
}
static bool proxy_read(void *cookie,uint32_t off,uint32_t *out)
{
    bool hit=callback_hit(0,'r');
    if (hit && mock.callback_mode==1) { mock.callback_failed=true; return false; }
    bool ok=original_io.read32(cookie,off,out);
    if (hit && mock.callback_mode==2) { mock.callback_failed=true; return false; }
    if (!ok) mock.callback_failed=true;
    return ok;
}
static bool proxy_write(void *cookie,uint32_t off,uint32_t value)
{
    bool hit=callback_hit(1,'w');
    if (hit && mock.callback_mode==1) { mock.callback_failed=true; return false; }
    bool ok=original_io.write32(cookie,off,value);
    if (hit && mock.callback_mode==2) { mock.callback_failed=true; return false; }
    if (!ok) mock.callback_failed=true;
    return ok;
}
static uint64_t proxy_now(void *cookie)
{
    bool hit=callback_hit(2,'t');
    uint64_t t=original_io.now_us(cookie);
    if (hit) { mock.callback_failed=true; return mock.callback_mode==1?t+300000:999; }
    return t;
}
static void proxy_delay(void *cookie,uint32_t us)
{
    bool hit=callback_hit(3,'d'); original_io.delay_us(cookie,us);
    if (hit) {
        if (mock.callback_mode==1) mock.now+=300000; else mock.now=999;
        /* Void delay failure is detectable at its required post time sample. */
    }
}
static struct ltsq_result observed_initialize(struct ltsq_io *io)
{
    ++capture_calls; original_io=bound_io=*io;
    io->read32=proxy_read; io->write32=proxy_write;
    io->now_us=proxy_now; io->delay_us=proxy_delay;
    captured=ltsq_observe_states(io);
    mock.core_returned=true;
    return captured;
}
static void reset_fixture(void)
{
    memset(&mock,0,sizeof(mock)); memset(&lts1,0,sizeof(lts1));
    memset(&ltid,0,sizeof(ltid)); memset(&captured,0,sizeof(captured));
    memset(&bound_io,0,sizeof(bound_io)); memset(&original_io,0,sizeof(original_io));
    ltsqp_active=ltsqp_reset_written=false; ltsqp_write_count=0;
    memset(ltsqp_write_trace,0,sizeof(ltsqp_write_trace)); capture_calls=0;
    ++scenarios; mock.now=1000; mock.affinity=1; mock.irq_enabled=1;
    mock.full_after_reset=0xc0;
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
static void direct_fixture(void)
{
    reset_fixture(); ltid.start=ltid.last=mock.now;
    ltid.qup=(vm_offset_t)&mock.words[5][0]; ltid.gic=(vm_offset_t)&mock.words[8][0];
    mock.gic_mapped=mock.policy_only=true; ltid.started=ltsqp_active=true;
}
static unsigned occurrences(const char *haystack,const char *needle)
{
    unsigned n=0;
    while ((haystack=strstr(haystack,needle))) { ++n; ++haystack; }
    return n;
}
static void check_evidence(void)
{
    if (!capture_calls) return;
    ++evidence_checks; CHECK(capture_calls==1);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH state field ")==272);
    CHECK(occurrences(mock.log,"LITHIUM TOUCH state snapshot ")==8);
    const char *field_cursor=mock.log;
    for (unsigned phase=0;phase<8;++phase) {
        const struct ltsn_result *s=&captured.snapshots[phase];
        unsigned accepted=0;
        CHECK(captured.snapshot_valid[phase]==s->complete);
        for (unsigned pass=0;pass<2;++pass) for (unsigned i=0;i<17;++i) {
            char expected[320];
            if (s->valid_mask[pass]&(1u<<i)) {
                ++accepted; CHECK(captured.snapshot_attempted[phase]);
                snprintf(expected,sizeof(expected),
                    "LITHIUM TOUCH state field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=1 value=0x%08x at_us=%llu read_us=%llu\n",
                    phases[phase],pass,i,ltsn_registers[i].name,snapshot_offsets[i],
                    s->values[pass][i],(unsigned long long)s->at_us[pass][i],
                    (unsigned long long)s->read_elapsed_us[pass][i]);
            } else {
                CHECK(!s->values[pass][i] && !s->at_us[pass][i] && !s->read_elapsed_us[pass][i]);
                snprintf(expected,sizeof(expected),
                    "LITHIUM TOUCH state field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=0 value=unread\n",
                    phases[phase],pass,i,ltsn_registers[i].name,snapshot_offsets[i]);
            }
            field_cursor=strstr(field_cursor,"LITHIUM TOUCH state field ");
            CHECK(field_cursor && !strncmp(field_cursor,expected,strlen(expected)));
            if (field_cursor) field_cursor+=strlen(expected);
            else field_cursor=mock.log+mock.log_size;
        }
        CHECK(s->completed_reads==accepted);
        if (!captured.snapshot_attempted[phase]) CHECK(!s->complete && !s->attempts && !accepted);
    }
    CHECK(ltsqp_write_count==mock.writes);
    unsigned index=0;
    for (unsigned e=0;e<mock.events;++e) if (mock.trace[e].kind=='W') {
        CHECK(index<ltsqp_write_count);
        CHECK(ltsqp_write_trace[index].offset==(mock.trace[e].physical&0xfff));
        CHECK(ltsqp_write_trace[index].value==mock.trace[e].value); ++index;
    }
    CHECK(!mock.post_core_times && !mock.post_core_events);
    CHECK(!mock.callback_after_false);
    if (captured.first_failed_offset_valid) {
        char needle[120];
        snprintf(needle,sizeof(needle),"failure valid=1 access=%s off=0x%03x",
            captured.first_failed_access,captured.first_failed_offset);
        CHECK(strstr(mock.log,needle));
    } else CHECK(strstr(mock.log,"failure valid=0 access=unread off=unread"));
}
static void seal(void)
{
    unsigned events=mock.events,times=mock.times,delays=mock.delays;
    size_t logged=mock.log_size; const char *reason=ltid.reason;
    CHECK(!ltsqp_active); lithium_touch_qup_state_probe();
    CHECK(mock.events==events && mock.times==times && mock.log_size==logged);
    uint32_t output=0xfeedface;
    CHECK(!ltsqp_read(NULL,0x030,&output) && output==0xfeedface);
    CHECK(!ltsqp_write(NULL,0x00c,1)); (void)ltsqp_now(NULL); ltsqp_delay(NULL,1);
    if (capture_calls) {
        CHECK(!bound_io.read32(bound_io.cookie,0x030,&output));
        CHECK(!bound_io.write32(bound_io.cookie,0x00c,1));
        (void)bound_io.now_us(bound_io.cookie); bound_io.delay_us(bound_io.cookie,1);
    }
    CHECK(mock.events==events && mock.times==times && mock.delays==delays);
    CHECK(ltid.reason==reason); CHECK(!strstr(mock.log,"valid=0 value=0x"));
    CHECK(mock.sw_resets<=1); check_evidence();
}
static void test_context(void)
{
    case_name="Stage1/context/shared-once-latch";
    for (unsigned c=0;c<12;++c) {
        reset_fixture();
        switch (c) {
        case 0:lts1.started=false;break; case 1:lts1.poisoned=true;break;
        case 2:lts1.clocks_ready=false;break;case 3:lts1.pins_ready=false;break;
        case 4:mock.affinity=0;break;case 5:mock.affinity=0x100;break;
        case 6:mock.affinity=0x101;break;case 7:mock.max_cpu=1;break;
        case 8:mock.max_cpu=-1;break;case 9:mock.irq_context=1;break;
        case 10:mock.irq_enabled=0;break;default:ltid.poisoned=true;break;
        }
        lithium_touch_qup_state_probe();
        CHECK(ltid.started && ltid.poisoned && !mock.events && !mock.maps);
        CHECK(!mock.sw_resets && !capture_calls); seal();
    }
    reset_fixture();ltid.started=true;lithium_touch_qup_state_probe();
    CHECK(!mock.events && !mock.times && !mock.log_size);seal();
}
static void test_mapping(void)
{
    case_name="mapping-count-zero-missing-board-change-map-false";
    for (unsigned i=0;i<8;++i) {
        reset_fixture();lts1.windows[i].mapped=0;lithium_touch_qup_state_probe();
        CHECK(ltid.poisoned && !mock.events && !mock.maps);seal();
    }
    for (unsigned i=0;i<6;++i) {
        reset_fixture();lts1.windows[i].physical=0;lithium_touch_qup_state_probe();
        CHECK(ltid.poisoned && !mock.maps && !mock.qup_reads && !mock.writes);seal();
    }
    const unsigned bad_counts[]={0,9,UINT32_MAX};
    for (unsigned i=0;i<3;++i) {
        reset_fixture();lts1.mappings=bad_counts[i];lithium_touch_qup_state_probe();
        CHECK(ltid.poisoned && !mock.events && !mock.maps);seal();
    }
    for (unsigned i=0;i<9;++i) {
        reset_fixture();*word(board[i].physical)^=board[i].bad_bit;
        lithium_touch_qup_state_probe();
        CHECK(ltid.poisoned && mock.reads==i+1 && !mock.maps && !mock.qup_reads && !mock.writes);seal();
    }
    reset_fixture();mock.map_fail=true;lithium_touch_qup_state_probe();
    CHECK(ltid.poisoned && mock.maps==1 && mock.reads==9 && !mock.writes && !capture_calls);seal();
}
static void successful(void)
{
    CHECK(capture_calls==1 && !ltid.poisoned && mock.maps==1);
    CHECK(mock.sw_resets==1 && mock.writes==18 && mock.state_writes==3);
    CHECK(mock.run_count==1 && mock.pause_count==1);
    CHECK(captured.reset_issued && captured.base_configured && captured.transfer_configured);
    CHECK(captured.run_observed && captured.clock_programmed && captured.pause_observed);
    CHECK(!strcmp(captured.status,"observed"));
    CHECK(!strcmp(captured.reason,"prefifo_pause_observed_no_new_tags"));
    for (unsigned i=0;i<8;++i) CHECK(captured.snapshot_valid[i] && captured.snapshot_attempted[i]);
    CHECK(mock.gic_reads[0]==19 && mock.gic_reads[1]==19 && mock.gic_reads[2]==19);
    for (unsigned i=0;i<9;++i) CHECK(mock.trace[i].kind=='R' && mock.trace[i].physical==board[i].physical);
    CHECK(mock.trace[9].kind=='M' && mock.trace[9].physical==0x09bc0000);
    CHECK(*word(0x075ba208)==3 && *word(0x075ba150)==9);
    CHECK(!*word(0x075ba100) && !*word(0x075ba200));
    CHECK(!captured.first_failed_offset_valid);
}
static void test_success(void)
{
    case_name="real-adapter-core-eighteen-writes-eight-phases-reentry";
    const char kinds[]={0,'M','R','W','B','T','L','D'};
    for (unsigned i=0;i<sizeof(kinds);++i) {
        reset_fixture();mock.reenter_kind=kinds[i];
        if (kinds[i]=='D') mock.transition_wait=1;
        lithium_touch_qup_state_probe();successful();
        CHECK(!kinds[i] || mock.reentered);seal();
    }
    const uint32_t full[]={0,0x40,0x80,0xc0};
    for (unsigned i=0;i<4;++i) {
        reset_fixture();mock.full_after_reset=full[i];
        lithium_touch_qup_state_probe();successful();seal();
    }
    reset_fixture();*word(0x09bc0110)=*word(0x09bc0210)=*word(0x09bc0310)=0x800;
    lithium_touch_qup_state_probe();successful();seal();
}
static void test_gic(void)
{
    case_name="GIC-entry-and-every-write-enable-pending-active";
    const uint32_t regs[]={0x09bc0110,0x09bc0210,0x09bc0310};
    for (unsigned r=0;r<3;++r) {
        reset_fixture();*word(regs[r])=0x400;lithium_touch_qup_state_probe();
        CHECK(ltid.poisoned && mock.maps==1 && !mock.qup_reads && !mock.writes && !capture_calls);seal();
        for (unsigned before=0;before<18;++before) {
            reset_fixture();mock.trip_register=r+1;mock.trip_write=before;
            lithium_touch_qup_state_probe();
            CHECK(ltid.poisoned && mock.writes==before);
            CHECK(captured.first_failed_offset_valid && !strcmp(captured.first_failed_access,"write"));
            CHECK(captured.first_failed_offset==expected_writes[before][0]);seal();
        }
    }
}
static void test_policy(void)
{
    case_name="independent-numeric-policy-no-FIFO-or-extraneous-write";
    const uint32_t values[]={0,1,2,3,9,0x207,0x7207,0xc000,0xc0a5,0xc1ef,
        0x300,0x100,0x200,0x003e007c,0x01827081,0x877181a3,0x40,0x3c,UINT32_MAX};
    for (uint32_t off=0;off<4096;++off) {
        direct_fixture();uint32_t value=0xfeedface;
        bool ok=ltsqp_read(NULL,off,&value);
        CHECK(ok==read_allowed(off));CHECK(mock.reads==(ok?1u:0u));
        if (!ok) CHECK(value==0xfeedface && !mock.events);
        if (off&3u) {
            direct_fixture();CHECK(!ltsqp_write(NULL,off,1) && !mock.events);
            continue;
        }
        for (unsigned v=0;v<sizeof(values)/sizeof(values[0]);++v) {
            direct_fixture();bool allowed=write_allowed(off,values[v]);
            CHECK(ltsqp_write(NULL,off,values[v])==allowed);
            CHECK(mock.writes==(allowed?1u:0u));CHECK(mock.reads==(allowed?3u:0u));
        }
    }
    direct_fixture();CHECK(ltsqp_write(NULL,0x00c,1));unsigned events=mock.events;
    CHECK(!ltsqp_write(NULL,0x00c,1) && mock.events==events && mock.writes==1);
    for (unsigned i=0;i<18;++i) for (unsigned bit=0;bit<32;++bit) {
        direct_fixture();uint32_t off=expected_writes[i][0];
        uint32_t flipped=expected_writes[i][1]^(UINT32_C(1)<<bit);
        bool allowed=write_allowed(off,flipped);
        CHECK(ltsqp_write(NULL,off,flipped)==allowed);
        CHECK(mock.writes==(allowed?1u:0u) && mock.reads==(allowed?3u:0u));
    }
    direct_fixture();ltid.qup=0;CHECK(!ltsqp_write(NULL,0x00c,1) && !mock.events);
    direct_fixture();ltid.gic=0;CHECK(!ltsqp_write(NULL,0x00c,1) && !mock.events);
    direct_fixture();CHECK(!ltsqp_read(NULL,0x030,NULL) && !mock.events);
    direct_fixture();ltsqp_delay(NULL,11);CHECK(ltid.poisoned && !mock.delays);
    direct_fixture();ltsqp_write_count=64;
    CHECK(!ltsqp_write(NULL,0x00c,1) && !mock.events);
    CHECK(strstr(ltid.reason,"trace_cap"));
    const uint32_t outside[]={0x1000,0xfffffffc,UINT32_MAX};
    for (unsigned i=0;i<3;++i) {
        direct_fixture();uint32_t value=0xfeedface;
        CHECK(!ltsqp_read(NULL,outside[i],&value) && value==0xfeedface && !mock.events);
        direct_fixture();CHECK(!ltsqp_write(NULL,outside[i],1) && !mock.events);
    }
    direct_fixture();ltid.poisoned=true;
    uint32_t value=0xfeedface;
    CHECK(!ltsqp_read(NULL,0x030,&value) && value==0xfeedface);
    CHECK(!ltsqp_write(NULL,0x00c,1));ltsqp_delay(NULL,1);CHECK(!mock.events && !mock.delays);
    /* The unchanged OUTER helpers have independent, tighter caps. These are
     * deliberately not assertions that the portable core has the same caps. */
    direct_fixture();mock.now=100999;value=0xfeedface;
    CHECK(ltsqp_read(NULL,0x030,&value));
    direct_fixture();mock.now=101000;value=0xfeedface;
    CHECK(!ltsqp_read(NULL,0x030,&value) && value==0xfeedface && !mock.events);
    CHECK(!strcmp(ltid.reason,"adapter_deadline"));
    direct_fixture();ltid.reads=4094;value=0xfeedface;
    CHECK(ltsqp_read(NULL,0x030,&value) && mock.reads==1);
    direct_fixture();ltid.reads=4095;value=0xfeedface;
    CHECK(!ltsqp_read(NULL,0x030,&value) && value==0xfeedface && mock.reads==1);
    CHECK(!strcmp(ltid.reason,"adapter_access_cap"));
    direct_fixture();ltid.reads=4096;value=0xfeedface;
    CHECK(!ltsqp_read(NULL,0x030,&value) && value==0xfeedface && !mock.events);
}
static void test_guard(void)
{
    case_name="entry-logical-stop-first-failure-and-272-field-validity";
    reset_fixture();*word(0x075ba000)=1;lithium_touch_qup_state_probe();
    CHECK(!mock.writes && !captured.reset_issued);
    CHECK(!strcmp(captured.reason,"entry_not_exact_r29"));seal();
    unsigned logical_reads=mock.reads;
    reset_fixture();*word(0x075ba000)=1;mock.fault_kind='R';
    mock.fault_at=logical_reads-33;mock.fault_mode=1;
    lithium_touch_qup_state_probe();
    CHECK(mock.fired && !mock.after_fault && ltid.poisoned);
    CHECK(!strcmp(captured.reason,"entry_not_exact_r29"));
    CHECK(captured.snapshot_attempted[7] && !captured.snapshot_valid[7]);seal();
    reset_fixture();mock.corrupt_readback=true;lithium_touch_qup_state_probe();
    CHECK(mock.writes==6 && !captured.base_configured && !captured.run_observed);
    CHECK(!strcmp(captured.reason,"configuration_readback_not_exact"));seal();
    reset_fixture();mock.mutate_physical=0x075ba10c;
    mock.mutate_value=1;mock.mutate_after_writes=1;lithium_touch_qup_state_probe();
    CHECK(mock.writes==1 && !captured.base_configured);
    CHECK(!strcmp(captured.reason,"prefifo_counts_not_exact"));seal();
    reset_fixture();*word(0x075ba030)=0x12345678;lithium_touch_qup_state_probe();
    CHECK(!mock.writes && captured.snapshots[0].attempts==1);
    CHECK(strstr(mock.log,"valid=1 value=0x12345678"));seal();
    reset_fixture();mock.hold_state=true;lithium_touch_qup_state_probe();
    CHECK(!captured.run_observed && !captured.pause_observed && mock.writes==16);
    CHECK(mock.delays>0 && captured.polls==512);seal();
    for (unsigned bit=0;bit<32;++bit) {
        uint32_t mask=UINT32_C(1)<<bit;
        if (!(mask&0xc0u)) {
            reset_fixture();mock.full_after_reset=0xc0u|mask;
            lithium_touch_qup_state_probe();
            CHECK(mock.writes==1 && !captured.base_configured && !captured.run_observed);
            CHECK(!strcmp(captured.reason,(mask&0x3f30u)?
                "unexpected_prefifo_activity":"unknown_operational_bits"));seal();
        }
        reset_fixture();mock.mutate_physical=0x075ba01c;
        mock.mutate_value=mask;mock.mutate_after_writes=1;
        lithium_touch_qup_state_probe();
        CHECK(mock.writes==1 && !strcmp(captured.reason,"new_core_error"));seal();
        reset_fixture();mock.mutate_physical=0x075ba404;
        mock.mutate_value=0x0c000000u^mask;mock.mutate_after_writes=1;
        lithium_touch_qup_state_probe();
        CHECK(mock.writes==1 && !strcmp(captured.reason,"i2c_status_not_exact_idle"));seal();
    }
    direct_fixture();CHECK(!ltsqp_read(NULL,0x00c,NULL));
    const char *first=ltid.reason;CHECK(first && ltid.poisoned);
    CHECK(!ltsqp_write(NULL,0x00c,2));ltsqp_delay(NULL,11);
    CHECK(ltid.reason==first && !mock.events);
}
static void test_faults(void)
{
    case_name="every-physical-map-read-write-barrier-late-and-rollback";
    reset_fixture();lithium_touch_qup_state_probe();
    unsigned counts[]={mock.maps,mock.reads,mock.writes,mock.barriers};
    const char kinds[]={'M','R','W','B'};
    for (unsigned mode=1;mode<=2;++mode) for (unsigned k=0;k<4;++k)
        for (unsigned at=1;at<=counts[k];++at) {
            reset_fixture();mock.fault_kind=kinds[k];mock.fault_at=at;mock.fault_mode=mode;
            lithium_touch_qup_state_probe();
            CHECK(mock.fired && ltid.poisoned);
            CHECK(!mock.after_fault && !mock.writes_after_fault);
            if (kinds[k]=='W') {
                CHECK(mock.writes==at && ltsqp_write_count==at);
                CHECK(!ltsqp_write_trace[at-1].returned_on_time);
                CHECK(ltsqp_reset_written && captured.reset_issued);
                CHECK(captured.first_failed_offset_valid);
                CHECK(captured.first_failed_offset==expected_writes[at-1][0]);
            }
            seal();
        }
    for (unsigned mode=1;mode<=2;++mode) for (unsigned barrier=1;barrier<=2;++barrier) {
        direct_fixture();uint32_t out=0xfeedface;
        mock.fault_kind='B';mock.fault_at=barrier;mock.fault_mode=mode;
        CHECK(!ltsqp_read(NULL,0x030,&out));
        CHECK(out==0xfeedface && !mock.after_fault && mock.reads==barrier-1);
    }
    for (unsigned mode=1;mode<=2;++mode) {
        reset_fixture();mock.transition_wait=1;mock.fault_kind='D';
        mock.fault_at=1;mock.fault_mode=mode;lithium_touch_qup_state_probe();
        CHECK(mock.fired && mock.delays==1 && !mock.after_fault);seal();
    }
}
static void test_callbacks(void)
{
    case_name="all-external-callback-false-late-rollback-no-followup";
    reset_fixture();lithium_touch_qup_state_probe();
    unsigned counts[4];memcpy(counts,mock.callback_counts,sizeof(counts));
    const char kinds[]={'r','w','t'};
    for (unsigned k=0;k<3;++k) for (unsigned mode=1;mode<=2;++mode)
        for (unsigned at=1;at<=counts[k];++at) {
            /* A rollback before any baseline exists has no earlier time to
             * contradict; skip that non-test rather than asserting a fault. */
            if (k==2 && at==1) continue;
            reset_fixture();mock.callback_kind=kinds[k];mock.callback_at=at;mock.callback_mode=mode;
            lithium_touch_qup_state_probe();
            CHECK(mock.callback_failed && !mock.callback_after_false);
            CHECK(strcmp(captured.status,"observed"));
            if (k<2) {
                CHECK(captured.first_failed_offset_valid);
                CHECK(!strcmp(captured.first_failed_access,k==0?"read":"write"));
                CHECK(!strcmp(captured.reason,k==0?"read_callback_failed":"write_callback_failed"));
            }
            seal();
        }
    reset_fixture();mock.transition_wait=2;lithium_touch_qup_state_probe();
    unsigned delays=mock.callback_counts[3];CHECK(delays>0);
    for (unsigned mode=1;mode<=2;++mode) for (unsigned at=1;at<=delays;++at) {
        reset_fixture();mock.transition_wait=2;mock.callback_kind='d';
        mock.callback_at=at;mock.callback_mode=mode;lithium_touch_qup_state_probe();
        CHECK(strcmp(captured.status,"observed") && mock.callback_counts[3]==at);seal();
    }
}
int main(int argc,char **argv)
{
    (void)lithium_touch_id_probe; /* Actual R27 helpers compiled; old entry idle. */
    static const struct { const char *name;void (*run)(void); } groups[]={
        {"context",test_context},{"mapping",test_mapping},{"success",test_success},
        {"gic",test_gic},{"policy",test_policy},{"guard",test_guard},
        {"faults",test_faults},{"callbacks",test_callbacks}
    };
    unsigned selected=0;
    for (int a=1;a<argc;++a) {
        bool found=false;
        for (unsigned i=0;i<8;++i) if (!strcmp(argv[a],groups[i].name)) found=true;
        if (!found) { fprintf(stderr,"Unknown test group: %s\n",argv[a]);return 2; }
    }
    for (unsigned i=0;i<8;++i) {
        bool run=argc==1;
        for (int a=1;a<argc;++a) if (!strcmp(argv[a],groups[i].name)) run=true;
        if (run) {
            unsigned before=scenarios,failed=failures;
            groups[i].run();++selected;
            printf("group=%s scenarios=%u failed_checks=%u\n",groups[i].name,scenarios-before,failures-failed);
        }
    }
    printf("touch-qup-state-adapter-test: %u scenarios, %u failed checks, %u assertions; groups=%u/8 %s evidence_runs=%u; actual R30 adapter/core + unchanged R27/R28 helpers; host RAM mocks only\n",
        scenarios,failures,assertions,selected,selected==8?"FULL":"PARTIAL",evidence_checks);
    return failures || !selected?1:0;
}
