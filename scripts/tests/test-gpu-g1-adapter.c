/* Actual G1 adapter/header tests with recording kernel and MMIO seams only.
 * Run: python -B evidence/gpu-g1-20261123/adapter/run-tests.py
 * Independent literal address/command oracles; no hardware or PMIC writes.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../ports/hakios-lithium/diagnostics/cpu-mmio-window-r38.h"
#include "../../ports/hakios-lithium/peripherals/boot-options.h"

typedef uintptr_t vm_offset_t;
static uint64_t PAGE_SIZE;
#define VM_WIMG_IO 7
static unsigned cases, checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"FAIL case=%u line=%d: %s\n",cases,__LINE__,#x); \
    exit(EXIT_FAILURE); } } while (0)

static const unsigned field_offsets[7] = {4,5,0x40,0x41,0x45,0x46,0x61};
static const unsigned row_ids[6] = {39,41,63,64,66,68};
static const uint64_t fuse_words[6] = {
    (UINT64_C(1)<<60)|(UINT64_C(2)<<48), UINT64_C(1)<<31,
    (UINT64_C(1)<<55)|(UINT64_C(2)<<50)|(UINT64_C(3)<<45)|(UINT64_C(4)<<40),
    UINT64_C(9)<<58, UINT64_C(5)<<42, UINT64_C(36)<<26
};
struct allocation { vm_offset_t va; uint64_t pa, bytes; unsigned kind; bool live; };
struct command { unsigned rail, field, pass; uint64_t pa; uint32_t word; };
static struct {
    struct allocation allocations[8];
    struct command commands[28];
    uint32_t version, channel[128], config[2], status;
    unsigned apid[2], channel_reads[128], version_reads;
    unsigned map_calls, map_success, unmap_calls, fail_map_mask;
    unsigned loads32, loads64, stores, barriers, delays;
    unsigned rail_commands[2], status_reads[2], data_reads[2], active_field[2], active_pass[2];
    unsigned enter_calls, leave_calls, admit_calls, clock_calls, print_bytes;
    unsigned mutate_mapping_at, mutate_version_at, fault_command, fault_kind;
    unsigned rollback_at, expire_at, lose_owner_at;
    unsigned unstable_fuse, unstable_rail, unstable_field;
    unsigned late_command, late_fuse;
    int logical;
    bool profile, bind, interrupts, freeze_clock, zero_fuses, late_final;
    uint64_t clock_us, clock_step, affinity;
    char printed[32768];
} mock;

struct lithium_touch_cpu_scope { bool bound; };
static bool lg0_adapter_admit(void) { ++mock.admit_calls; return mock.profile; }
static bool lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *s)
{ ++mock.enter_calls; s->bound=mock.bind; return mock.bind; }
static void lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *s)
{ CHECK(s->bound); ++mock.leave_calls; s->bound=false; }
static int cpu_number(void) { return mock.logical; }
static bool ml_get_interrupts_enabled(void) { return mock.interrupts; }
#define LITHIUM_PERIPHERAL_AFFINITY() (mock.affinity)
static uint64_t mach_absolute_time(void)
{
    ++mock.clock_calls;
    if (mock.clock_calls==mock.rollback_at) mock.clock_us=1;
    if (mock.clock_calls==mock.expire_at) mock.clock_us+=250000;
    if (mock.clock_calls==mock.lose_owner_at) mock.logical=1;
    uint64_t value=mock.clock_us;
    if (!mock.freeze_clock) mock.clock_us+=mock.clock_step;
    return value;
}
static void absolutetime_to_nanoseconds(uint64_t ticks,uint64_t *out)
{ CHECK(out!=NULL && ticks<UINT64_MAX/1000); *out=ticks*1000; }

static uint64_t observer(unsigned rail)
{ return UINT64_C(0x04c00000)+UINT64_C(0x8000)*mock.apid[rail]; }
static vm_offset_t ml_io_map_unmappable(uint64_t pa,uint64_t bytes,unsigned attr)
{
    unsigned slot=mock.map_calls++;
    CHECK(slot<8 && attr==7 && bytes==PAGE_SIZE && pa%PAGE_SIZE==0);
    unsigned kind=99;
    if (pa==(UINT64_C(0x00074000)&~(PAGE_SIZE-1))) kind=0;
    if (pa==(UINT64_C(0x0400f000)&~(PAGE_SIZE-1))) kind=1;
    for (unsigned rail=0;rail<2;++rail)
        if (pa==(observer(rail)&~(PAGE_SIZE-1))) kind=2+rail;
    CHECK(kind<4); /* Never map GPU/GPUCC/SMMU/WDATA/whole observer aperture. */
    if (mock.fail_map_mask&(1u<<slot)) return 0;
    void *memory=NULL;
    CHECK(posix_memalign(&memory,(size_t)PAGE_SIZE,(size_t)bytes)==0);
    memset(memory,0,(size_t)bytes);
    mock.allocations[slot]=(struct allocation){(uintptr_t)memory,pa,bytes,kind,true};
    ++mock.map_success;
    return (uintptr_t)memory;
}
static void ml_io_unmap(vm_offset_t va,uint64_t bytes)
{
    bool found=false;
    for (unsigned i=0;i<8;++i) if (mock.allocations[i].live && mock.allocations[i].va==va) {
        CHECK(bytes==mock.allocations[i].bytes && bytes==PAGE_SIZE && va%PAGE_SIZE==0);
        mock.allocations[i].live=false;
        free((void *)va); found=true; break;
    }
    CHECK(found); ++mock.unmap_calls;
}
static uint64_t physical(vm_offset_t pointer,unsigned bytes,unsigned *kind)
{
    for (unsigned i=0;i<8;++i) {
        const struct allocation *a=&mock.allocations[i];
        if (a->live && pointer>=a->va && pointer-a->va<=a->bytes-bytes) {
            CHECK(pointer%bytes==0);
            *kind=a->kind;
            return a->pa+pointer-a->va;
        }
    }
    CHECK(false); return 0;
}
static uint32_t load32(vm_offset_t pointer)
{
    unsigned kind;
    uint64_t pa=physical(pointer,4,&kind);
    ++mock.loads32;
    if (kind==1) {
        if (pa==UINT64_C(0x0400f000)) {
            ++mock.version_reads;
            return mock.version_reads==mock.mutate_version_at ? mock.version^1u : mock.version;
        }
        CHECK(pa>=UINT64_C(0x0400f800) && pa<=UINT64_C(0x0400f9fc));
        unsigned ch=(unsigned)((pa-UINT64_C(0x0400f800))/4);
        ++mock.channel_reads[ch];
        return ch==mock.apid[0] && mock.channel_reads[ch]==mock.mutate_mapping_at ?
            mock.channel[ch]^UINT32_C(0x80) : mock.channel[ch];
    }
    CHECK(kind==2 || kind==3);
    unsigned rail=kind-2;
    uint64_t offset=pa-observer(rail);
    if (offset==4) return mock.config[rail];
    CHECK(mock.rail_commands[rail]>0);
    bool fault=mock.stores==mock.fault_command;
    if (offset==8) {
        ++mock.status_reads[rail];
        if (fault && mock.fault_kind==1) return 5; /* DONE+DENIED */
        if (fault && mock.fault_kind==2) return 3; /* DONE+FAIL (NACK-like completion) */
        if (fault && mock.fault_kind==3) return 9; /* DONE+DROPPED */
        if (fault && mock.fault_kind==4) return 0x11; /* DONE+unknown */
        if (fault && mock.fault_kind==5) return 0; /* Still pending */
        if (fault && mock.fault_kind==6) mock.clock_us+=200; /* Late DONE beats pending timeout. */
        return 1;
    }
    CHECK(offset==0x18); /* Single byte must never read RDATA1 or WDATA. */
    CHECK(!fault || mock.fault_kind==6 || mock.fault_kind==0);
    ++mock.data_reads[rail];
    unsigned field=mock.active_field[rail], pass=mock.active_pass[rail];
    uint32_t value=(uint32_t)(0x20+rail*0x40+field);
    if (mock.unstable_rail==rail+1 && mock.unstable_field==field && pass==1) value^=1;
    if (mock.late_final && rail==1 && pass==1 && field==6) mock.clock_us+=250000;
    return UINT32_C(0xabc10000)|value; /* RAW RDATA low byte, not STATUS data. */
}
static uint64_t load64(vm_offset_t pointer)
{
    unsigned kind;
    uint64_t pa=physical(pointer,8,&kind);
    CHECK(kind==0 && mock.loads64<12);
    unsigned i=mock.loads64%6, pass=mock.loads64/6;
    CHECK(pa==UINT64_C(0x00074000)+8u*row_ids[i]);
    ++mock.loads64;
    if (mock.loads64==mock.late_fuse) mock.clock_us+=250000;
    uint64_t value=mock.zero_fuses ? 0 : fuse_words[i];
    if (mock.unstable_fuse==i+1 && pass==1) value^=1;
    return value;
}
static void store_read_command(vm_offset_t pointer,uint32_t cmd)
{
    unsigned kind;
    uint64_t pa=physical(pointer,4,&kind);
    CHECK(kind==2 || kind==3);
    unsigned rail=kind-2, at=mock.rail_commands[rail]++;
    CHECK(pa==observer(rail) && mock.stores<28 && at<14);
    unsigned field=at%7, pass=at/7;
    CHECK(cmd==(UINT32_C(0x08000000)|(field_offsets[field]<<4)));
    CHECK((cmd>>27)==1); /* Only EXT_READL, no SID/base insertion or PMIC WRITE. */
    mock.commands[mock.stores++]=(struct command){rail,field,pass,pa,cmd};
    mock.active_field[rail]=field; mock.active_pass[rail]=pass;
    if (mock.stores==mock.late_command) mock.clock_us+=250000;
    if (mock.stores==mock.fault_command && mock.fault_kind==7) mock.clock_us=1;
}
static void barrier(void) { ++mock.barriers; }
static void delay_us(unsigned us)
{
    CHECK(us==1 || us==1000);
    ++mock.delays;
    if (!mock.freeze_clock) mock.clock_us+=us;
}
static int record_printf(const char *format,...)
{
    CHECK(strstr(format,"LITHIUM GPU")!=NULL);
    va_list args;
    va_start(args,format);
    int length=vsnprintf(mock.printed+mock.print_bytes,sizeof(mock.printed)-mock.print_bytes,format,args);
    va_end(args);
    CHECK(length>=0 && (size_t)length<sizeof(mock.printed)-mock.print_bytes);
    mock.print_bytes+=(unsigned)length;
    return length;
}
#define LG1_BARRIER() barrier()
#define LG1_LOAD32(address) load32(address)
#define LG1_LOAD64(address) load64(address)
#define LG1_STORE_READ_COMMAND(address,value) store_read_command(address,value)
#define LG1_DELAY_US(us) delay_us(us)
#define printf record_printf
#include "../../ports/hakios-lithium/gpu/g1-precheck.inc"
#undef printf

static void setup(uint64_t page)
{
    for (unsigned i=0;i<8;++i) CHECK(!mock.allocations[i].live);
    memset(&mock,0,sizeof(mock));
    memset(&lg1,0,sizeof(lg1));
    PAGE_SIZE=page;
    mock.version=UINT32_C(0x20010000);
    mock.apid[0]=17; mock.apid[1]=29;
    mock.channel[17]=UINT32_C(0xf00317a5);
    mock.channel[29]=UINT32_C(0x00011700);
    mock.profile=mock.bind=mock.interrupts=true;
    mock.affinity=1; mock.clock_us=1000; mock.clock_step=1;
}
static void cleanup_check(void)
{
    CHECK(mock.map_success==mock.unmap_calls);
    for (unsigned i=0;i<8;++i) CHECK(!mock.allocations[i].live);
    CHECK(lg1.core.allocation==0 && lg1.observer.allocation==0 && lg1.fuses.allocation==0);
    CHECK(lg1.observer_pa==0 && lg1.allowed_cmd==0);
    CHECK(mock.stores==lg1.writes && mock.loads32==lg1.reads && mock.loads64==lg1.fuse_reads);
    CHECK(mock.loads32+mock.loads64+mock.stores<=9000);
    CHECK(mock.stores<=28);
    CHECK(mock.barriers==2*(mock.loads32+mock.loads64+mock.stores));
    CHECK(mock.leave_calls==1);
    CHECK(strstr(mock.printed,"settled=UNVERIFIED chip_id=UNREAD")!=NULL);
}
static void full_rail(unsigned rail)
{
    CHECK(lg1.rail_valid[rail][0]==0x7f && lg1.rail_valid[rail][1]==0x7f);
    CHECK(lg1.rail_error[rail]==LGS_OK && mock.rail_commands[rail]==14);
    CHECK(mock.data_reads[rail]==14);
    for (unsigned pass=0;pass<2;++pass) for (unsigned field=0;field<7;++field) {
        unsigned value=0x20+rail*0x40+field;
        if (mock.unstable_rail==rail+1 && mock.unstable_field==field && pass==1) value^=1;
        CHECK(lg1.rail_raw[rail][pass][field]==value);
    }
}
static void full_fuses(void)
{
    CHECK(lg1.fuse_valid[0]==0x3f && lg1.fuse_valid[1]==0x3f && mock.loads64==12);
    for (unsigned pass=0;pass<2;++pass) for (unsigned row=0;row<6;++row) {
        uint64_t value=mock.zero_fuses ? 0 : fuse_words[row];
        if (mock.unstable_fuse==row+1 && pass==1) value^=1;
        CHECK(lg1.fuse_raw[pass][row]==value);
    }
}
static void test_success(void)
{
    for (unsigned page=0;page<2;++page) for (unsigned route=0;route<2;++route) {
        setup(page?16384:4096);
        if (route) {
            mock.channel[17]=mock.channel[29]=0;
            mock.apid[0]=0; mock.apid[1]=127;
            mock.channel[0]=0xf00317a5; mock.channel[127]=0x00011700;
        }
        ++cases; lithium_gpu_g1_precheck(); cleanup_check(); full_fuses();
        full_rail(0); full_rail(1);
        CHECK(mock.map_calls==4 && mock.stores==28 && mock.loads32==4010);
        CHECK(lg1.fuse_stable==0x3f && lg1.rail_stable[0]==0x7f && lg1.rail_stable[1]==0x7f);
        CHECK(lg1.fuse_decoded.revision==2 && lg1.fuse_decoded.speedbin==1);
        CHECK(lg1.fuse_decoded.limitation==1 && lg1.fuse_decoded.aging==36);
        for (unsigned i=0;i<4;++i) CHECK(lg1.fuse_decoded.initcode[i]==i+1);
        CHECK(lg1.fuse_decoded.offsetraw[1]==5 && lg1.fuse_decoded.offsetraw[3]==9);
        for (unsigned rail=0;rail<2;++rail) {
            CHECK(lg1.routes[rail].sid==(rail?1u:3u));
            CHECK(lg1.routes[rail].base==0x1700 && lg1.routes[rail].ppid==(rail?0x117u:0x317u));
            CHECK(lg1.routes[rail].observer_pa==observer(rail));
        }
        CHECK(!lg1.poisoned && lg1.used==1 && strstr(mock.printed,"reg=STATUS")==NULL);
    }
    puts("PASS two rails x seven source fields x two passes; v2 PPIDs317/117 distinct windows; 4K/16K cleanup; six fuse rows x two passes");
}
static void test_validity_stability(void)
{
    setup(16384); mock.zero_fuses=true; ++cases;
    lithium_gpu_g1_precheck(); cleanup_check(); full_fuses();
    CHECK(lg1.fuse_stable==0x3f && lg1.fuse_decoded.revision==0);
    CHECK(strstr(mock.printed,"fuse decode valid=1")!=NULL);
    for (unsigned row=0;row<6;++row) {
        setup(16384); mock.unstable_fuse=row+1; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check(); full_fuses();
        CHECK(lg1.fuse_stable==(0x3fu^(1u<<row)));
        CHECK(strstr(mock.printed,"fuse decode valid=0")!=NULL);
        CHECK(lg1.fuse_decoded.revision==0);
    }
    for (unsigned rail=0;rail<2;++rail) for (unsigned field=0;field<7;++field) {
        setup(16384); mock.unstable_rail=rail+1; mock.unstable_field=field; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check(); full_rail(0); full_rail(1);
        CHECK(lg1.rail_stable[rail]==(0x7fu^(1u<<field)));
        CHECK(lg1.rail_stable[1-rail]==0x7f);
    }
    puts("PASS valid zero fuse data, every fuse-row/rail-field stability difference and no stale fuse decode");
}
static void test_map_failures(void)
{
    for (unsigned page=0;page<2;++page) for (unsigned fail=0;fail<4;++fail) {
        setup(page?16384:4096); mock.fail_map_mask=1u<<fail; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(mock.map_calls==(fail==1?2u:4u));
        if (fail==0) CHECK(lg1.fuse_valid[0]==0 && lg1.fuse_valid[1]==0 && mock.loads64==0);
        if (fail==1) CHECK(mock.loads32==0 && mock.stores==0);
        if (fail>=2) {
            unsigned rail=fail-2;
            CHECK(lg1.rail_valid[rail][0]==0 && lg1.rail_valid[rail][1]==0);
            CHECK(lg1.rail_error[rail]==LGS_ARGUMENT && mock.rail_commands[rail]==0);
            full_rail(1-rail);
        }
    }
    setup(16384); mock.fail_map_mask=0xff; ++cases;
    lithium_gpu_g1_precheck(); cleanup_check();
    CHECK(mock.map_calls==2 && mock.map_success==0 && mock.loads32==0 && mock.loads64==0 && mock.stores==0);
    puts("PASS each partial map failure, all mapping failures and exact successful-allocation cleanup");
}
static void test_routes(void)
{
    for (unsigned rail=0;rail<2;++rail) for (unsigned failure=0;failure<3;++failure) {
        setup(16384); ++cases;
        if (failure==0) mock.channel[mock.apid[rail]]=0;
        if (failure==1) mock.channel[100]=(rail?0x117u:0x317u)<<8;
        if (failure==2) mock.channel[mock.apid[rail]]=0x14000; /* S1/wrong PPID */
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[rail]==(failure==1?LGS_DUPLICATE_MAPPING:LGS_NO_MAPPING));
        CHECK(mock.rail_commands[rail]==0 && lg1.rail_valid[rail][0]==0);
        full_rail(1-rail);
    }
    const uint32_t versions[]={0,0x20000000,0x20010001,0xffffffff};
    for (unsigned i=0;i<4;++i) {
        setup(16384); mock.version=versions[i]; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[0]==LGS_VERSION && lg1.rail_error[1]==LGS_VERSION && mock.stores==0);
    }
    for (unsigned at=2;at<=3;++at) {
        setup(16384); mock.mutate_mapping_at=at; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[0]==LGS_ROUTE_CHANGED && mock.rail_commands[0]==0);
        full_rail(1);
        setup(16384); mock.mutate_version_at=at; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[0]==(at==2?LGS_VERSION:LGS_ROUTE_CHANGED));
        CHECK(mock.rail_commands[0]==0); full_rail(1);
    }
    for (unsigned rail=0;rail<2;++rail) {
        setup(16384); mock.config[rail]=1; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[rail]==LGS_CONFIG_UNSUPPORTED && mock.rail_commands[rail]==0);
        full_rail(1-rail);
    }
    puts("PASS missing/duplicate/wrong PPID, exact version, route/version changes and CONFIG rejection before READCMD");
}
static void test_completions(void)
{
    const unsigned errors[]={LGS_DENIED,LGS_FAILURE,LGS_DROPPED,LGS_UNKNOWN_STATUS,LGS_TIMEOUT};
    for (unsigned rail=0;rail<2;++rail) for (unsigned kind=1;kind<=5;++kind) {
        setup(16384); mock.fault_kind=kind; mock.fault_command=rail?15:1; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[rail]==errors[kind-1] && mock.rail_commands[rail]==1);
        CHECK(lg1.rail_valid[rail][0]==0 && lg1.rail_valid[rail][1]==0 && mock.data_reads[rail]==0);
        CHECK(mock.status_reads[rail]>=1 && mock.status_reads[rail]<=100);
        full_rail(1-rail);
    }
    for (unsigned rail=0;rail<2;++rail) for (unsigned slot=0;slot<14;++slot) {
        setup(16384); mock.fault_kind=2; mock.fault_command=(rail?14:0)+slot+1; ++cases;
        lithium_gpu_g1_precheck(); cleanup_check();
        CHECK(lg1.rail_error[rail]==LGS_FAILURE && mock.rail_commands[rail]==slot+1);
        CHECK(lg1.rail_valid[rail][0]==(slot>=7?0x7fu:(1u<<slot)-1u));
        CHECK(lg1.rail_valid[rail][1]==(slot<7?0u:(1u<<(slot-7))-1u));
        CHECK(mock.data_reads[rail]==slot); full_rail(1-rail);
    }
    setup(16384); mock.fault_kind=6; mock.fault_command=1; ++cases;
    lithium_gpu_g1_precheck(); cleanup_check(); full_rail(0); full_rail(1);
    setup(16384); mock.freeze_clock=true; mock.fault_kind=5; mock.fault_command=1; ++cases;
    lithium_gpu_g1_precheck(); cleanup_check();
    CHECK(lg1.rail_error[0]==LGS_TIMEOUT && mock.status_reads[0]==100 && !lg1.poisoned);
    full_rail(1);
    setup(16384); mock.fault_kind=7; mock.fault_command=1; ++cases;
    lithium_gpu_g1_precheck(); cleanup_check();
    CHECK(lg1.poisoned && mock.stores==1 && mock.data_reads[0]==0);
    CHECK(lg1.rail_error[0]==LGS_WRITE_ERROR && lg1.rail_valid[0][0]==0);
    CHECK(lg1.transaction.cmd_attempted==1 && lg1.transaction.cmd_completed==0);
    CHECK(lg1.transaction.callback_rc==-3 && mock.status_reads[0]==0);
    puts("PASS DENIED, FAIL/NACK-like, DROPPED, unknown, pending timeout, every partial failure slot, late DONE and frozen-clock poll cap");
}
static void test_gates_and_clock(void)
{
    setup(16384); mock.profile=false; ++cases;
    lithium_gpu_g1_precheck(); CHECK(!lg1.used && mock.map_calls==0 && mock.enter_calls==0);
    mock.profile=true; lithium_gpu_g1_precheck(); cleanup_check();
    unsigned maps=mock.map_calls, reads=mock.loads32, writes=mock.stores, times=mock.clock_calls;
    lithium_gpu_g1_precheck(); CHECK(mock.map_calls==maps && mock.loads32==reads && mock.stores==writes);
    CHECK(mock.clock_calls==times && mock.enter_calls==1 && mock.leave_calls==1);
    setup(16384); mock.bind=false; ++cases;
    lithium_gpu_g1_precheck(); CHECK(lg1.used==1 && mock.map_calls==0 && mock.leave_calls==0);
    mock.bind=true; lithium_gpu_g1_precheck(); CHECK(mock.enter_calls==1 && mock.map_calls==0);
    for (unsigned owner=0;owner<3;++owner) {
        setup(16384); ++cases;
        if (owner==0) mock.logical=1;
        if (owner==1) mock.affinity=0x100;
        if (owner==2) mock.interrupts=false;
        lithium_gpu_g1_precheck(); cleanup_check(); CHECK(lg1.poisoned && mock.map_calls==0);
    }
    const unsigned points[]={2,4,16,150,1600,3000};
    for (unsigned i=0;i<sizeof(points)/sizeof(*points);++i) for (unsigned mode=0;mode<3;++mode) {
        setup(16384); ++cases;
        if (mode==0) mock.rollback_at=points[i];
        if (mode==1) mock.expire_at=points[i];
        if (mode==2) mock.lose_owner_at=points[i];
        lithium_gpu_g1_precheck(); cleanup_check(); CHECK(lg1.poisoned);
        CHECK(mock.stores<28 && mock.loads64<=12 && mock.loads32<4010);
    }
    puts("PASS profile/CPU rejection, consumed once gate, live owner checks, rollback and overall deadline with partial cleanup");
}
static void direct_maps(void)
{
    lg1.start_us=lg1.last_us=1000;
    CHECK(lg1_map(&lg1.core,0x0400f000,4096));
    CHECK(lg1_map(&lg1.observer,observer(0),4096));
    CHECK(lg1_map(&lg1.fuses,0x00074000,4096));
    lg1.observer_pa=observer(0); lg1.allowed_cmd=0x08000040;
}
static void direct_cleanup(void)
{
    lg1_unmap(&lg1.core); lg1_unmap(&lg1.observer); lg1_unmap(&lg1.fuses);
    CHECK(mock.unmap_calls==mock.map_success);
    for (unsigned i=0;i<8;++i) CHECK(!mock.allocations[i].live);
}
static void test_access_allowlist_and_caps(void)
{
    setup(16384); direct_maps(); ++cases;
    const uint64_t bad_reads[]={0x00b00000,0x00b40000,0x008c4034,0x0400f004,0x0400f7fc,0x0400fa00,
        0x0400f801,0x00074138,0x04c00000,0x04c88000,0x04c88010,0x04c8800c,0x04c88020};
    uint32_t value=0;
    unsigned before=mock.loads32;
    for (unsigned i=0;i<sizeof(bad_reads)/sizeof(*bad_reads);++i)
        CHECK(lg1_read32(NULL,bad_reads[i],&value)!=0 && mock.loads32==before);
    CHECK(lg1_read32(NULL,0x0400f000,NULL)!=0 && mock.loads32==before);
    const uint32_t bad_commands[]={0,0x18000040,0x08000041,0x08000080,0x08317040,0xffffffff};
    for (unsigned i=0;i<sizeof(bad_commands)/sizeof(*bad_commands);++i)
        CHECK(lg1_read_command(NULL,observer(0),bad_commands[i])!=0 && mock.stores==0);
    CHECK(lg1_read_command(NULL,observer(0)+4,0x08000040)!=0 && mock.stores==0);
    CHECK(lg1_read_command(NULL,observer(1),0x08000040)!=0 && mock.stores==0);
    CHECK(lg1_delay(NULL,1001)!=0 && mock.delays==0);
    CHECK(lg1_time(NULL,NULL)!=0);
    direct_cleanup();
    setup(16384); direct_maps(); ++cases;
    lg1.reads=8999; CHECK(lg1_read32(NULL,0x0400f000,&value)==-3 && lg1.reads==9000);
    before=mock.loads32; CHECK(lg1_read32(NULL,0x0400f000,&value)!=0 && lg1.poisoned && mock.loads32==before);
    direct_cleanup();
    setup(16384); direct_maps(); ++cases;
    lg1.reads=8999; CHECK(lg1_read_command(NULL,observer(0),0x08000040)==-3);
    CHECK(lg1_read_command(NULL,observer(0),0x08000040)!=0 && lg1.poisoned && mock.stores==1);
    direct_cleanup();
    setup(16384); direct_maps(); ++cases;
    lg1.writes=27; CHECK(lg1_read_command(NULL,observer(0),0x08000040)==0 && lg1.writes==28);
    CHECK(lg1_read_command(NULL,observer(0),0x08000040)!=0 && mock.stores==1);
    direct_cleanup();
    puts("PASS adapter read/CMD pointer allowlists, exact READCMD words, 9000 combined access cap and 28-command cap (counter fault injection)");
}
static void test_parser(void)
{
    struct lithium_peripheral_options o;
    const char good[]="gpu_probe=1 gpu_stage=0 gpu_power_diag=1";
    ++cases; CHECK(lithium_peripheral_options_parse(good,sizeof(good)-1,&o));
    CHECK(o.gpu_probe && o.gpu_power_diag && o.gpu_stage==0);
    const char *bad[]={"gpu_power_diag=1","gpu_stage=0 gpu_power_diag=1",
      "gpu_probe=1 gpu_power_diag=1","gpu_probe=0 gpu_stage=0 gpu_power_diag=1",
      "gpu_probe=1 gpu_stage=1 gpu_power_diag=1","gpu_probe=1 gpu_stage=0 gpu_power_diag=2",
      "gpu_probe=1 gpu_stage=0 gpu_power_diag=1 gpu_power_diag=1",
      "gpu_probe=1 gpu_stage=0 gpu_power_diag=1 touch=1",
      "gpu_probe=1 gpu_stage=0 gpu_power_diag=1 touch_stage=0"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        ++cases; CHECK(!lithium_peripheral_options_parse(bad[i],strlen(bad[i]),&o));
        CHECK(!o.gpu_power_diag);
    }
    ++cases; CHECK(lithium_peripheral_options_parse(NULL,0,&o) && !o.gpu_power_diag && !o.gpu_probe);
    puts("PASS actual parser explicit default-off G0 power diagnostic admission and malformed/mixed/higher-stage rejection");
}
static void test_returning_access_crossings(void)
{
    setup(16384); mock.late_final=true; ++cases;
    lithium_gpu_g1_precheck(); cleanup_check(); full_rail(0);
    CHECK(mock.clock_us-lg1.start_us>=250000 && lg1.poisoned);
    CHECK(lg1.rail_valid[1][0]==0x7f && lg1.rail_valid[1][1]==0x3f);
    CHECK(lg1.rail_stable[1]==0x3f && lg1.rail_error[1]==LGS_READ_ERROR);
    CHECK(lg1.rail_raw[1][1][6]==0);
    CHECK(lg1.transaction.phase==LGS_DATA && lg1.transaction.callback_rc==-3);
    CHECK(!lg1.transaction.data_valid && !lg1.transaction.data_words_valid);
    CHECK(mock.stores==28 && mock.data_reads[1]==14 && mock.loads32==4010);
    for (unsigned page=0;page<2;++page) {
        for (unsigned position=0;position<2;++position) {
            setup(page?16384:4096); mock.late_command=position?28:1; ++cases;
            lithium_gpu_g1_precheck(); cleanup_check();
            CHECK(lg1.poisoned && mock.stores==mock.late_command);
            CHECK(lg1.transaction.error==LGS_WRITE_ERROR && lg1.transaction.phase==LGS_COMMAND);
            CHECK(lg1.transaction.cmd_attempted==1 && lg1.transaction.cmd_completed==0);
            CHECK(lg1.transaction.writes==1 && lg1.transaction.callback_rc==-3);
            CHECK(!lg1.transaction.status_valid && !lg1.transaction.data_valid);
            if (position) {
                full_rail(0);
                CHECK(lg1.rail_valid[1][0]==0x7f && lg1.rail_valid[1][1]==0x3f);
                CHECK(lg1.rail_error[1]==LGS_WRITE_ERROR && mock.data_reads[1]==13);
            } else {
                CHECK(lg1.rail_valid[0][0]==0 && lg1.rail_valid[0][1]==0);
                CHECK(lg1.rail_error[0]==LGS_WRITE_ERROR && mock.status_reads[0]==0);
            }
        }
        for (unsigned at=1;at<=12;++at) {
            setup(page?16384:4096); mock.late_fuse=at; ++cases;
            lithium_gpu_g1_precheck(); cleanup_check();
            CHECK(lg1.poisoned && mock.loads64==at && lg1.fuse_reads==at);
            CHECK(mock.map_calls==1 && mock.map_success==1 && mock.loads32==0 && mock.stores==0);
            unsigned before=at-1;
            unsigned valid0=before>=6?0x3fu:(1u<<before)-1u;
            unsigned valid1=before<=6?0u:(1u<<(before-6))-1u;
            CHECK(lg1.fuse_valid[0]==valid0 && lg1.fuse_valid[1]==valid1);
            CHECK(lg1.fuse_stable==(valid0&valid1));
            CHECK(lg1.fuse_decoded.revision==0 && strstr(mock.printed,"fuse decode valid=0")!=NULL);
            for (unsigned i=0;i<12;++i)
                CHECK(lg1.fuse_raw[i/6][i%6]==(i<at?fuse_words[i%6]:0));
            /* The returning late row is retained as raw but never valid. */
            CHECK((lg1.fuse_valid[(at-1)/6]&(1u<<((at-1)%6)))==0);
        }
    }
    puts("PASS post-access250ms rejection: final RDATA invalid/error/poisoned, first/final READCMD attempted retained, every returning fuse slot invalid; 4K/16K cleanup");
}
int main(void)
{
    CHECK(LGS_RAIL_COUNT==2 && LGS_FIELD_COUNT==7 && LGF_ROW_COUNT==6);
    CHECK(lgs_rails[0].base==0x1700 && lgs_rails[1].base==0x1700);
    for (unsigned i=0;i<7;++i) CHECK(lgs_fields[i].offset==field_offsets[i]);
    for (unsigned i=0;i<6;++i) CHECK(lgf_rows[i].rowid==row_ids[i]);
    test_success(); test_validity_stability(); test_map_failures(); test_routes();
    test_completions(); test_gates_and_clock(); test_access_allowlist_and_caps();
    test_parser(); test_returning_access_crossings();
    printf("SUMMARY cases=%u checks=%u result=PASS hardware_operations=0\n",cases,checks);
    return EXIT_SUCCESS;
}
