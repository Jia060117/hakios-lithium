#!/usr/bin/env python3
"""Exercise R8 real functions, old/new pixels and upstream console draining."""
from pathlib import Path
import subprocess
import hakios
import hakios_service_wait_observer as observer


def compile_run(folder, name, text, cpp=False):
    source=folder/(name+('.cpp' if cpp else '.c'))
    source.write_text(text)
    subprocess.run(['clang++' if cpp else 'clang','-std=c++17' if cpp else '-std=c11',
        '-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable',
        '-fsanitize=address,undefined',str(source),'-o',str(folder/name)],check=True)
    subprocess.run([str(folder/name)],check=True)


def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check()
    work=hakios.verify_workspace(spec)
    port=work/'devices/lithium/diagnostics'
    contract=hakios.read(port/'service-wait-observer.json')
    folder=work/'build/service-wait-checks';folder.mkdir(parents=True,exist_ok=True)
    old=folder/'r7-framebuffer.cpp'
    fb_name='xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    if not old.exists():
        original=work/'xnu-src/xnu-8796.121.2/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
        hakios.require(hakios.sha(original)==contract['before_sha256'][fb_name], 'Freeze exact R7 reference before dependency sync')
        old.write_bytes(original.read_bytes())
    hakios.require(hakios.sha(old)==contract['before_sha256'][fb_name], 'R7 reference changed')
    for name,digest in contract['after_sha256'].items():
        hakios.require(hakios.sha(work/name)==digest,'R8 source differs: '+name)
    prior=old.read_text()
    hakios.require(observer.framebuffer_source(prior)==(work/fb_name).read_text(),'Framebuffer changes exceed exact R8 delta')
    reference=prior[prior.index('void\nIOMobileFramebuffer::drawRows('):prior.index('/* A helper of the scaling')]
    reference=reference.replace('IOMobileFramebuffer::drawRows(', 'IOMobileFramebuffer::referenceRows(')
    base=(work/'build/thread-wait-checks/rows.cpp').read_text()
    base=base[:base.index('int main(void)')]
    base=base.replace('    void drawRows(const RowJob*,int32_t,int32_t,uint32_t*);',
        '    void drawRows(const RowJob*,int32_t,int32_t,uint32_t*);\n    void referenceRows(const RowJob*,int32_t,int32_t,uint32_t*);')
    tests=r'''
int main(void) {
    unsigned cases=0;uint32_t random=0x137521u;
    auto next=[&]() { random=random*1664525u+1013904223u;return random; };
    for(unsigned bytes: {3u,4u}) for(bool filter: {false,true}) for(bool diff: {false,true})
    for(unsigned offset=0;offset<8;offset++) for(unsigned width=1;width<=33;width++) {
        const unsigned height=19,stride=((width*bytes+3)&~3u)+8;
        IOMobileFramebuffer a{},b{};
        std::vector<uint8_t> aa(64+offset+height*stride,0xa5),bb=aa;
        std::vector<uint32_t> pixels(64*7);for(auto &p:pixels)p=next();
        a.fBase=aa.data()+32+offset;b.fBase=bb.data()+32+offset;
        a.fPanelHeight=b.fPanelHeight=height;a.fRowBytes=b.fRowBytes=stride;
        a.fPixelBytes=b.fPixelBytes=bytes;a.fDiff=b.fDiff=diff;
        a.fDiffLeft=b.fDiffLeft=a.fDiffTop=b.fDiffTop=~0u;
        for(unsigned x=0;x<width;x++){a.fColumn[x]=b.fColumn[x]=x*5/width;a.fColumnWeight[x]=b.fColumnWeight[x]=(x%3)?127:0;}
        RowJob job{};job.pixels=(const uint8_t*)pixels.data();job.stride=64*4;job.sw=5;job.sh=7;
        job.px0=0;job.py0=-1;job.px1=width;job.py1=21;job.cx0=width>2?1:0;job.cx1=width;job.filter=filter;
        // BGRA32's unchanged path needs 4-byte row alignment, as in production.
        if(bytes==4 && offset%4)continue;
        a.referenceRows(&job,-1,21,a.fBlendRow);b.drawRows(&job,-1,21,b.fBlendRow);
        assert(aa==bb && a.fDiffPixels==b.fDiffPixels && a.fDiffLeft==b.fDiffLeft &&
            a.fDiffRight==b.fDiffRight && a.fDiffTop==b.fDiffTop && a.fDiffBottom==b.fDiffBottom);cases++;
    }
    for(unsigned bytes: {3u,4u}) for(unsigned offset=0;offset<8;offset++) for(int width=1;width<=33;width++) {
        if(bytes==4 && offset%4)continue;
        IOMobileFramebuffer a{},b{};const unsigned stride=192,height=7;
        std::vector<uint8_t> aa(64+offset+height*stride,0xa5),bb=aa;
        std::vector<uint32_t> pixels(64*height);for(auto &p:pixels)p=next();
        a.fBase=aa.data()+32+offset;b.fBase=bb.data()+32+offset;
        a.fPanelHeight=b.fPanelHeight=height;a.fRowBytes=b.fRowBytes=stride;a.fPixelBytes=b.fPixelBytes=bytes;
        RowJob job{};job.pixels=(const uint8_t*)pixels.data();job.stride=64*4;job.sw=width;job.sh=7;job.sx=2;
        job.px0=1;job.py0=0;job.py1=7;job.unscaled=true;
        a.referenceRows(&job,0,7,a.fBlendRow);b.drawRows(&job,0,7,b.fBlendRow);assert(aa==bb);cases++;
    }
    printf("PASS %u frozen-R7/R8 pixel and damage-counter comparisons\n",cases);
}
'''
    compile_run(folder,'pixels',base+reference+tests,True)
    inc=port/'service-wait-observer.inc'
    code=inc.read_text().replace('#include <mach/message.h>','')
    prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
typedef int pid_t;typedef int kern_return_t;
struct task { void *itk_space; };typedef struct task *task_t;
struct port { int locked; };typedef struct port *ipc_port_t;
#define IP_NULL NULL
#define TH_WAIT 1
#define KERN_INVALID_NAME 15
#define KERN_SUCCESS 0
#define MACH_SEND_MSG 1
#define MACH_PORT_VALID(p) ((p)!=0 && (p)!=~0u)
static struct port port1;static int calls,unlocks,queries,lines,lookup;
static kern_return_t ipc_port_translate_send(void *space,uint32_t name,ipc_port_t *p) {
    assert(space && name==0x123);calls++;if(lookup)return lookup;port1.locked=1;*p=&port1;return 0;
}
static pid_t ipc_port_get_receiver_task_locked(ipc_port_t p,uintptr_t *task) {assert(p->locked && !task);queries++;return 25;}
static void ip_mq_unlock(ipc_port_t p) {assert(p->locked);p->locked=0;unlocks++;}
static int record(const char *fmt,...) {char b[256];va_list ap;va_start(ap,fmt);int n=vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);assert(n>0 && n<255);lines++;return n;}
#define printf record
'''
    tests=r'''
int main(void) {
    struct task task={&task};
    lithium_ustacks_ipc(&task,9,TH_WAIT,1,0,0x123,0,0,(uint64_t)-47);
    assert(calls==1 && queries==1 && unlocks==1 && !port1.locked && lines==2);
    lookup=17;lithium_ustacks_ipc(&task,9,TH_WAIT,1,0,0x123,0,0,(uint64_t)-47);
    assert(calls==2 && queries==1 && unlocks==1 && lines==4);
    lithium_ustacks_ipc(&task,9,TH_WAIT,0,0,0x123,0,0,(uint64_t)-47);
    lithium_ustacks_ipc(&task,9,TH_WAIT,1,0,0,0,0,(uint64_t)-47);
    lithium_ustacks_ipc(&task,9,TH_WAIT,1,0,~0u,0,0,(uint64_t)-47);
    assert(calls==2 && lines==10);
    lithium_ustacks_ipc(&task,9,0,1,0,0x123,0,0,(uint64_t)-47);
    lithium_ustacks_ipc(&task,9,TH_WAIT,1,0,0x123,0,0,(uint64_t)-31);
    lithium_ustacks_ipc(&task,9,TH_WAIT,1,0,0x123,0,0,515);
    assert(calls==2 && lines==10);
    puts("PASS read-only current receiver lookup, balanced locks, unsupported/nonwaiting traps");
}
'''
    compile_run(folder,'receiver',prefix+code+tests)
    tree=work/'xnu-src/xnu-8796.121.2'
    proto=(tree/'osfmk/console/serial_protos.h').read_text()
    state=proto[proto.index('#define SERIAL_CONS_BUF_SIZE'):proto.index('extern int console_printbuf_drain_initialized')]
    general=tree/'osfmk/console/serial_general.c'
    actual=general.read_text();actual=actual[actual.index('void\nconsole_printbuf_state_init'):]
    actual=actual[:actual.index('void\nconsole_printbuf_clear')]+actual[actual.index('void\nconsole_printbuf_clear'):].split('\n}\n',1)[0]+'\n}\n'
    prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define bzero(p,n) memset(p,0,n)
#define SERIALMODE_SYNCDRAIN 1
static int serialmode;static char output[16384];static size_t used;
static void console_write(char *s,int n) {assert(n>=0 && used+n<sizeof(output));memcpy(output+used,s,n);used+=n;}
'''
    tests=r'''
int main(void) {
    const int sizes[]={1,254,255,256,257,1024,8191};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++)for(int sync=0;sync<2;sync++) {
        struct console_printbuf_state state;used=0;serialmode=sync;
        console_printbuf_state_init(&state,1,1);
        for(int j=0;j<sizes[i];j++)console_printbuf_putc('A'+j%23,&state);
        console_printbuf_putc('\n',&state);console_printbuf_clear(&state);
        if(sync){assert(used==(size_t)sizes[i]+1 && output[used-1]=='\n');for(int j=0;j<sizes[i];j++)assert(output[j]=='A'+j%23);}
        else assert(used<=(size_t)256);
    }
    puts("PASS upstream actual console drain: 14 long/short cases, loss reproduced without sync");
}
'''
    compile_run(folder,'console',prefix+state+actual+tests)
    minimal='cpus=1 serial=3 ufs=2'
    budget=observer.command_budget(minimal,'test')
    maxline=minimal+' '+('x'*(budget['spare_bytes']-1))
    hakios.require(observer.command_budget(maxline,'test')['spare_bytes']==0,'Exact capacity differs')
    try:observer.command_budget(maxline+'x','test')
    except RuntimeError:pass
    else:raise AssertionError('Oversized full loader prefix accepted')
    paths=[inc,port/'bgr24-pack.inc',port/'service-wait-observer.json',work/fb_name,
        work/'xnuloader/loader.c',work/'xnu/files/osfmk/arm64/user_exception_trace.c',general,
        hakios.ROOT/'scripts/hakios_service_wait_observer.py',Path(__file__)]
    hakios.write(hakios.ROOT/'reports/hakios-ios16-service-wait-checks.json',dict(passed=True,
        inputs=hakios.inputs(spec),actual_production_functions=True,sanitizer='ASan/UBSan',
        frozen_r7_reference_sha256=hakios.sha(old),pixel_and_damage_comparisons=1650,
        console_cases=14,complete_loader_budget_boundary_checked=True,
        sampled_receiver_not_historical_peer=True,hardware_verified=False,
        evidence_sha256={str(p):hakios.sha(p) for p in paths}))


if __name__=='__main__':main()
