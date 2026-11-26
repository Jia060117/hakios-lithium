#!/usr/bin/env python3
"""Exercise the actual read-only owner lookup and profiled row path with sanitizers."""
from pathlib import Path
import subprocess
import hakios
import hakios_thread_wait_observer as observer


def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    contract = hakios.read(hakios.ROOT/spec['port_path']/'diagnostics/thread-wait-observer.json')
    trace = work/'xnu/files/osfmk/arm64/user_exception_trace.c'
    fb = work/'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    hakios.require(trace.read_text() == observer.trace_source((source/'xnu/files/osfmk/arm64/user_exception_trace.c').read_text()), 'Exact upstream walker extension differs')
    hakios.require(hakios.sha(fb) == contract['after_sha256']['xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'], 'Actual profiled framebuffer differs')
    folder = work/'build/thread-wait-checks'
    folder.mkdir(parents=True, exist_ok=True)
    inc = work/'devices/lithium/diagnostics/thread-wait-observer.inc'
    helper = inc.read_text()
    helper = helper[helper.index('static char lithium_ustacks_related'):]
    # Only declarations are SDK stand-ins; the tested selector, bounds, copyin
    # and IPC/thread-reference code below comes from the actual production file.
    prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t user_addr_t;
typedef int kern_return_t;
#define SYS_ulock_wait 515
#define SYS_ulock_wait2 544
#define TH_WAIT 1
#define UL_OPCODE_MASK 255
#define UL_COMPARE_AND_WAIT 1
#define UL_UNFAIR_LOCK 2
#define UL_COMPARE_AND_WAIT_SHARED 3
#define UL_UNFAIR_LOCK64_SHARED 4
#define UL_COMPARE_AND_WAIT64 5
#define UL_COMPARE_AND_WAIT64_SHARED 6
#define MACH_VM_MAX_ADDRESS UINT64_MAX
#define KERN_SUCCESS 0
#define KERN_INVALID_NAME 15
#define KERN_INVALID_RIGHT 17
typedef struct task { void *itk_space; } *task_t;
typedef struct owner { task_t task; uint64_t tid; unsigned refs; } *thread_read_t;
typedef struct port { bool locked; unsigned refs; } *ipc_port_t;
#define IP_NULL NULL
#define THREAD_NULL NULL
static struct task task1,task2;
static struct owner owner1;
static struct port port1;
static unsigned copies,lookups,converts,port_refs,port_releases,thread_releases;
static int copy_error,lookup_error;
static bool no_owner;
static bool plausible_user_address(uint64_t a) { return a>=0x100000000ULL && a<MACH_VM_MAX_ADDRESS; }
static int copyin(user_addr_t a,void *out,size_t size) { copies++;if(copy_error)return copy_error;memcpy(out,(void*)(uintptr_t)a,size);return 0; }
static uint32_t ulock_owner_value_to_port_name(uint32_t v) { return v|3; }
static int ipc_port_translate_send(void *space,uint32_t name,ipc_port_t *out)
{ assert(space==task1.itk_space && name!=0);lookups++;if(lookup_error)return lookup_error;port1.locked=true;*out=&port1;return 0; }
static void ip_reference(ipc_port_t p) { assert(p->locked);p->refs++;port_refs++; }
static void ip_mq_unlock(ipc_port_t p) { assert(p->locked);p->locked=false; }
static thread_read_t convert_port_to_thread_read(ipc_port_t p)
{ assert(!p->locked && p->refs==1);converts++;if(no_owner)return NULL;owner1.refs++;return &owner1; }
static void ip_release(ipc_port_t p) { assert(!p->locked && p->refs==1);p->refs--;port_releases++; }
static uint64_t thread_tid(thread_read_t t) { return t->tid; }
static task_t get_threadtask(thread_read_t t) { return t->task; }
static void thread_deallocate(thread_read_t t) { assert(t->refs==1);t->refs--;thread_releases++; }
static void reset(void)
{ copies=lookups=converts=port_refs=port_releases=thread_releases=0;copy_error=lookup_error=0;no_owner=false;memset(&port1,0,sizeof(port1));owner1.refs=0;owner1.task=&task1;owner1.tid=42;task1.itk_space=&task1; }
'''
    tests = r'''
int main(void)
{
    uint32_t word=0x1201;
    uint64_t wide=0x12345678abcdef00ULL;
    struct lithium_wait_context r;
    reset();lithium_ustacks_top=1;lithium_ustacks_sequence=1;
    assert(lithium_ustacks_top && lithium_ustacks_sequence);
    strcpy(lithium_ustacks_related,"backboardd,lsd,containermanagerd,runningboardd");
    assert(lithium_ustacks_match("lsd") && lithium_ustacks_match("runningboardd"));
    assert(!lithium_ustacks_match("lsd_helper") && !lithium_ustacks_match("boardd") && !lithium_ustacks_match(""));
    assert(lithium_ustacks_bounds(96,8,360000));
    assert(!lithium_ustacks_bounds(0,8,360000) && !lithium_ustacks_bounds(97,8,360000));
    assert(!lithium_ustacks_bounds(96,0,360000) && !lithium_ustacks_bounds(96,17,360000));
    assert(!lithium_ustacks_bounds(96,8,179999) && !lithium_ustacks_bounds(96,8,600001));
    r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.recognized && r.copy_error==0 && r.owner_tid==42 && r.same_task && r.word_matches && r.owner_port==(word|3));
    assert(copies==1 && lookups==1 && converts==1 && port_refs==port_releases && thread_releases==1);
    assert(!port1.refs && !owner1.refs && !port1.locked);
    reset();r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,7,544,TH_WAIT);
    assert(r.owner_tid==42 && !r.word_matches);
    reset();owner1.task=&task2;r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.owner_tid==42 && !r.same_task);
    reset();copy_error=14;r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.recognized && r.copy_error==14 && !lookups && !r.owner_tid);
    reset();lookup_error=15;r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.lookup_error==15 && !port_refs && !converts && !r.owner_tid);
    reset();no_owner=true;r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.lookup_error==17 && port_refs==port_releases && !thread_releases);
    reset();r=lithium_ustacks_wait(&task1,1,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.recognized && r.word_matches && !lookups && !r.owner_tid);
    reset();r=lithium_ustacks_wait(&task1,4,(uintptr_t)&wide,wide,544,TH_WAIT);
    assert(r.recognized && r.word==wide && r.word_matches && !lookups);
    reset();word=0;r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,TH_WAIT);
    assert(r.recognized && !r.owner_port && !lookups);
    reset();r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word+1,word,515,TH_WAIT);
    assert(!r.recognized && !copies);
    r=lithium_ustacks_wait(&task1,2,0xffffffffULL,word,515,TH_WAIT);assert(!r.recognized && !copies);
    r=lithium_ustacks_wait(&task1,4,UINT64_MAX-3,wide,544,TH_WAIT);assert(!r.recognized && !copies);
    r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,515,0);assert(!r.recognized && !copies);
    r=lithium_ustacks_wait(&task1,2,(uintptr_t)&word,word,1,TH_WAIT);assert(!r.recognized && !copies);
    r=lithium_ustacks_wait(&task1,253,(uintptr_t)&word,word,515,TH_WAIT);assert(!r.recognized && !copies);
    puts("PASS selectors, finite limits, fault/owner-state cases, balanced IPC/thread references");
}
'''
    c = folder/'owner.c'
    c.write_text(prefix + helper + tests)
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(folder/'owner')],check=True)
    subprocess.run([str(folder/'owner')],check=True)

    text = fb.read_text()
    rows = text[text.index('static inline uint32_t\nblend('):text.index('/* A helper of the scaling')]
    bands = text[text.index('\t\tstruct lithium_display_stamp bands_begin[8]'):text.index('#else\n\t\tdrawRows(&job, job.ry0, job.ry1, fBlendRow);',text.index('\t\tstruct lithium_display_stamp bands_begin[8]'))]
    profile = (work/'devices/lithium/diagnostics/framebuffer-profile.inc').read_text()
    profile = profile[profile.index('struct lithium_display_stamp'):]
    cpp_prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
typedef bool boolean_t;
#define FALSE false
static bool irq=true;
static uint64_t tick;
static unsigned output_lines;
struct recount_times_mach { uint64_t rtm_user,rtm_system; };
static bool ml_set_interrupts_enabled(bool value) { bool old=irq;irq=value;return old; }
static recount_times_mach recount_current_thread_times(void) { assert(!irq);return {0,tick/3}; }
static uint64_t mach_absolute_time(void) { tick+=3000;return tick; }
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *n) { *n=t; }
static void IOLog(const char *fmt,...) { assert(irq);char line[512];va_list ap;va_start(ap,fmt);vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);assert(strstr(line,"LITHIUM DISPLAY CPU"));output_lines++; }
static unsigned lithium_display_owns_console;
struct RowJob;
class IOMobileFramebuffer {
public:
    uint8_t *fBase;
    uint32_t fPanelHeight,fRowBytes,fPixelBytes,fSwapID;
    bool fDiff;
    uint32_t fDiffPixels,fDiffLeft,fDiffRight,fDiffTop,fDiffBottom;
    uint32_t fColumn[64];uint16_t fColumnWeight[64];uint32_t fBlendRow[64];
    void drawRows(const RowJob*,int32_t,int32_t,uint32_t*);
    void profiled(RowJob,bool);
};
'''
    cpp_method = '\nvoid IOMobileFramebuffer::profiled(RowJob job,bool lithium_profile)\n{\n' + bands + r'''
    if(lithium_profile) {
        lithium_display_span(fSwapID,1,job.ry0,job.ry1,lithium_rows,lithium_display_stamp_now());
        for(unsigned i=0;i<bands;i++) lithium_display_span(fSwapID,i+2,bands_y0[i],bands_y1[i],bands_begin[i],bands_end[i]);
    }
}
'''
    cpp_tests = r'''
int main(void)
{
    unsigned cases=0;
    auto stamp=lithium_display_stamp_now();assert(irq);
    irq=false;auto nested=lithium_display_stamp_now();assert(!irq);irq=true;
    lithium_display_span(1,0,0,1,nested,stamp); // backwards counters remain bounded
    for(unsigned bytes: {3u,4u}) for(bool filter: {false,true}) for(bool diff: {false,true})
    for(int rows: {1,2040,5000}) {
        IOMobileFramebuffer a{},b{};
        const unsigned width=7,stride=width*bytes+8;
        std::vector<uint8_t> aa(32+(rows+2)*stride,0xa5),bb=aa;
        std::vector<uint32_t> pixels(64*5);
        for(unsigned i=0;i<pixels.size();i++)pixels[i]=0xff000000u|(i*777u);
        a.fBase=aa.data()+16;b.fBase=bb.data()+16;
        a.fPanelHeight=b.fPanelHeight=rows+2;a.fRowBytes=b.fRowBytes=stride;
        a.fPixelBytes=b.fPixelBytes=bytes;a.fDiff=b.fDiff=diff;
        a.fDiffLeft=b.fDiffLeft=a.fDiffTop=b.fDiffTop=~0u;
        a.fSwapID=b.fSwapID=1;
        for(unsigned x=0;x<width;x++){a.fColumn[x]=b.fColumn[x]=x*5/width;a.fColumnWeight[x]=b.fColumnWeight[x]=x?64:0;}
        RowJob job{};job.pixels=(const uint8_t*)pixels.data();job.stride=64*4;
        job.sw=5;job.sh=5;job.px0=0;job.py0=1;job.px1=width;job.py1=rows+1;
        job.cx0=1;job.cx1=6;job.ry0=1;job.ry1=rows+1;job.filter=filter;
        a.drawRows(&job,job.ry0,job.ry1,a.fBlendRow);b.profiled(job,true);
        assert(aa==bb && a.fDiffPixels==b.fDiffPixels && a.fDiffLeft==b.fDiffLeft && a.fDiffBottom==b.fDiffBottom);
        for(unsigned i=0;i<16;i++)assert(aa[i]==0xa5 && aa[aa.size()-1-i]==0xa5);
        assert(irq);cases++;
        std::fill(bb.begin(),bb.end(),0xa5);b.fBase=bb.data()+16;b.profiled(job,false);assert(aa==bb);cases++;
    }
    for(unsigned bytes: {3u,4u}) {
        IOMobileFramebuffer a{},b{};std::vector<uint8_t> aa(16+1024*32,0xa5),bb=aa;
        std::vector<uint32_t> pixels(1024*8,0xff123456);
        a.fBase=aa.data()+8;b.fBase=bb.data()+8;a.fPanelHeight=b.fPanelHeight=1024;
        a.fRowBytes=b.fRowBytes=32;a.fPixelBytes=b.fPixelBytes=bytes;
        RowJob job{};job.pixels=(const uint8_t*)pixels.data();job.stride=32;job.sw=3;job.sh=1024;
        job.px0=1;job.px1=4;job.py0=0;job.py1=1024;job.ry1=1024;job.unscaled=true;
        a.drawRows(&job,0,1024,a.fBlendRow);b.profiled(job,true);assert(aa==bb);cases++;
    }
    assert(lithium_display_owns_console && output_lines>0);
    printf("PASS %u actual-row/profile comparisons, clipping/guard bytes, BGR24/BGRA32, finite bands, IRQ restoration\n",cases);
}
'''
    cpp = folder/'rows.cpp'
    cpp.write_text(cpp_prefix + profile + rows + cpp_method + cpp_tests)
    # The unchanged upstream drawRows declares px1 without reading it. Keep
    # the actual function intact; ASan/UBSan and all other warnings stay enabled.
    subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined',str(cpp),'-o',str(folder/'rows')],check=True)
    subprocess.run([str(folder/'rows')],check=True)
    paths = [inc,fb,trace,work/'devices/lithium/diagnostics/framebuffer-profile.inc',hakios.ROOT/'scripts/hakios_thread_wait_observer.py',Path(__file__)]
    hakios.write(hakios.ROOT/'reports/hakios-ios16-thread-wait-checks.json', dict(
        passed=True, inputs=hakios.inputs(spec),actual_production_functions=True,
        sanitizer='ASan/UBSan',sdk_stubs_only=True,
        owner_cases='selectors/bounds/unsupported syscall/invalid alignment/copyin faults/stale sampled word/missing owner/balanced references',
        framebuffer_comparisons=50,rendering_algorithm_preserved=True,
        evidence_sha256={str(p):hakios.sha(p) for p in paths},
        test_script_sha256=hakios.sha(Path(__file__)),
        lock_owner_hardware_verified=False,desktop_hardware_verified=False))
    print('PASS actual R7 wait-context and framebuffer profile functions; real lock owner still requires hardware evidence')


if __name__ == '__main__':
    main()
