#!/usr/bin/env python3
"""Exercise the actual swap adapter and deadline callback with bounded host inputs."""
import subprocess
import importlib.util
import hakios


def section(text, start):
    at=text.index(start); brace=text.index('{',at); end=brace+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[at:end]


def main():
    hakios.resource_check()
    spec=hakios.read(hakios.MANIFEST); work=hakios.verify_workspace(spec)
    fb=(work/'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp').read_text()
    lr=(work/'xnu/files/osfmk/arm64/loader_return.c').read_text()
    dest=work/'lithium-ui-host-checks';dest.mkdir(exist_ok=True)
    constants=fb[fb.index('#define SWAP_SIZE'):fb.index('#define kFBNotifyVSync')]
    h=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
'''+constants+section(fb,'static bool\nlithium_normalize_swap(')+r'''
int main() {
 uint8_t in[SWAP_SIZE],out[SWAP_SIZE+32];
 for(unsigned i=0;i<sizeof(in);i++)in[i]=(i*13+7)&255;
 memset(out,0xa5,sizeof(out));
 assert(lithium_normalize_swap(out,in,SWAP_SIZE)); assert(!memcmp(out,in,SWAP_SIZE));
 for(unsigned i=SWAP_SIZE;i<sizeof(out);i++)assert(out[i]==0xa5);
 puts("PASS original16 canonical swap and guards preserved");
 assert(lithium_normalize_swap(out,in,0x280));
 assert(!memcmp(out+SWAP_ID,in+0x40,4));
 assert(!memcmp(out+SWAP_SURFACE(0),in+0x44,12));
 assert(!memcmp(out+SWAP_SOURCE(0),in+0x50,48));
 assert(!memcmp(out+SWAP_ENABLED(0),in+0x80,12));
 assert(!memcmp(out+SWAP_DEST(0),in+0x98,48));
 uint32_t masks[2];memcpy(masks,out+SWAP_LAYERS_SET,8);
 assert(masks[0]<=7 && masks[1]<=7);
 for(unsigned i=0;i<4;i++)assert(out[SWAP_SURFACE(3)+i]==0);
 for(unsigned i=0;i<16;i++)assert(out[SWAP_SOURCE(3)+i]==0 && out[SWAP_DEST(3)+i]==0);
 puts("PASS exact14 three-layer fields translated, fourth layer empty");
 for(unsigned n: {0u,1u,639u,641u,1131u,1133u}) {memset(out,0xa5,sizeof(out));assert(!lithium_normalize_swap(out,in,n));for(auto c:out)assert(c==0xa5);}
 assert(!lithium_normalize_swap(out,nullptr,0x280));
 puts("PASS malformed swap lengths and null input rejected without writes");
}
'''
    h=h.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
    (dest/'swap.cpp').write_text(h)
    flags=['-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined']
    subprocess.run(['clang++','-std=c++17',*flags,str(dest/'swap.cpp'),'-o',str(dest/'swap')],check=True)
    checks=subprocess.check_output([str(dest/'swap')],text=True).splitlines()
    t=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define __unused __attribute__((unused))
#define NSEC_PER_SEC 1000000000
#define NSEC_PER_MSEC 1000000
#define KERN_SUCCESS 0
typedef void *thread_call_param_t;
static unsigned integrated=1,panicking=0,returns=0,scheduled=0;
static uint32_t lithium_ui_console_quiet,lithium_ui_layers,lithium_ui_backboard,lithium_ui_sbqueue,lithium_ui_svc,lithium_ui_surf;
static uint32_t lithium_ui_extensions,lithium_ui_last_layers,lithium_ui_last_signals;
static int lithium_ui_return_call;
static int PE_parse_boot_argn(const char*n,void*p,unsigned){if(!strcmp(n,"lithium_ui")){*(uint32_t*)p=integrated;return 1;}if(!strcmp(n,"return_panic")){*(uint32_t*)p=panicking;return 1;}return 0;}
static void loader_log_putc(char){} static void loader_log_line(const char*){}
static void lithium_ui_save(){}
static void clock_interval_to_deadline(unsigned n,unsigned,uint64_t*p){assert(n==300);*p=n;}
static void thread_call_enter_delayed(int,uint64_t){scheduled++;}
static int ml_get_max_cpu_number(){return 0;}
static int cpu_xcall(int,void(*)(void*),void*){return 1;}
static void delay_for_interval(int,int){}
static void loader_return_test(void*){returns++;}
static void hakios_var_save_on_return(){} static void hakios_adreno_stop(){}
static void hakios_wifi_stop(){} static void hakios_adsp_stop(){} static void hakios_mpss_stop(){}
'''+section(lr,'static void\nloader_return_timer(')+r'''
int main(){
 loader_return_timer(nullptr,nullptr);assert(returns==1&&scheduled==0);
 puts("PASS stalled UI returns retained failure at initial deadline");
 returns=0;lithium_ui_layers=1;lithium_ui_backboard=1;
 loader_return_timer(nullptr,nullptr);assert(returns==0&&scheduled==1&&lithium_ui_extensions==1);
 loader_return_timer(nullptr,nullptr);assert(returns==1&&scheduled==1);
 puts("PASS new progress extends once, unchanged progress does not loop");
 returns=0;lithium_ui_extensions=3;lithium_ui_layers=20;
 loader_return_timer(nullptr,nullptr);assert(returns==1&&scheduled==1);
 puts("PASS startup extensions bounded at thirty minutes total");
 returns=0;lithium_ui_sbqueue=1;
 loader_return_timer(nullptr,nullptr);assert(!returns&&scheduled==1);
 puts("PASS pixel-copy plus SB-queue candidate retained without desktop acceptance");
 integrated=0;loader_return_timer(nullptr,nullptr);assert(returns==1);
 puts("PASS legacy timer behavior preserved outside integrated mode");
 (void)lithium_ui_console_quiet;
}
'''
    (dest/'deadline.cpp').write_text(t)
    subprocess.run(['clang++',*flags,str(dest/'deadline.cpp'),'-o',str(dest/'deadline')],check=True)
    checks+=subprocess.check_output([str(dest/'deadline')],text=True).splitlines()
    surface=(work/'xnu/files/iokit/Kernel/MiniIOSurface.cpp').read_text()
    header=(work/'xnu/files/iokit/Kernel/MiniIOSurface.h').read_text()
    h=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstddef>
#define MINISURF_AUX_OFFSET 0x400
#define PAGE_SIZE 4096
typedef int IOReturn; typedef uint64_t mach_vm_address_t;
enum {kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotFound=2,kIOReturnUnsupported=3};
'''
    for name in ('minisurf_result','minisurf_plane','minisurf_shared'):
        h+=section(header,'struct '+name+' {')+';\n'
    h+=r'''
struct MiniIOSurface {minisurf_result info{};minisurf_plane planes[4]{};minisurf_shared state{};void *client=nullptr;uint64_t clientAddress=0;minisurf_shared* shared(){return &state;}uint32_t useCount[2]{};};
struct Map{uint64_t getAddress(){return 0x1000000;}};
struct minisurf_client_ref{MiniIOSurface *surface;Map *map,*pixelMap;uint32_t use[2]{};};
struct IOExternalMethodArguments {void *structureOutput;uint32_t structureOutputSize;const void *structureInput;uint32_t structureInputSize;uint64_t *scalarInput;uint32_t scalarInputCount;};
static int gSurfaceLock;
static void IOLockLock(int){} static void IOLockUnlock(int){}
struct IOSurfaceRootUserClient {
 minisurf_client_ref *fixture; bool released=false;
 minisurf_client_ref *findRef(uint32_t sid,bool){return fixture&&fixture->surface->info.sid==sid ? fixture:nullptr;}
 IOReturn returnSurface(minisurf_client_ref*,bool,IOExternalMethodArguments*);
 IOReturn useCountCommon(uint32_t,uint32_t,int);
 IOReturn lockCommon(uint32_t sid,uint32_t,uint64_t){auto r=findRef(sid,false);if(!r)return kIOReturnNotFound;r->surface->shared()->seed++;return kIOReturnSuccess;}
 IOReturn trapRelease(void *sid,void*,void*,void*,void*,void*){released=findRef((uint32_t)(uintptr_t)sid,false)!=nullptr;return released?0:2;}
 IOReturn legacy(uint32_t selector,IOExternalMethodArguments *args);
};
'''
    h+=section(surface,'static uint32_t\nresultSize(')+'\n'
    h+=section(surface,'IOReturn\nIOSurfaceRootUserClient::returnSurface(')+'\n'
    h+=section(surface,'IOReturn\nIOSurfaceRootUserClient::useCountCommon(')+'\n'
    start=surface.index('IOSurfaceRootUserClient::externalMethod(')
    start=surface.index('#if defined(LITHIUM_ARMV8_0)',start)
    end=surface.index('#endif',start)+len('#endif')
    h+='IOReturn IOSurfaceRootUserClient::legacy(uint32_t selector,IOExternalMethodArguments *args) {\n'
    h+='const uint8_t *in=(const uint8_t*)args->structureInput;size_t inSize=args->structureInputSize;IOReturn ret;minisurf_client_ref *ref;\n'
    h+=surface[start:end]+'\nreturn kIOReturnUnsupported;\n}\n'
    h+=r'''
int main(){
 static_assert(offsetof(minisurf_shared,useCount)==0x10);
 static_assert(offsetof(minisurf_shared,valuesSeed)==0x18);
 static_assert(offsetof(minisurf_shared,valuesSize)==0x20);
 static_assert(offsetof(minisurf_shared,pixelSizeCasting)==0x24);
 static_assert(offsetof(minisurf_shared,protectionOptions)==0x28);
 static_assert(offsetof(minisurf_shared,attachments)==0x30);
 MiniIOSurface s;s.info.sid=7;s.info.width=13;s.info.height=9;s.info.traceID=99;
 s.planes[0].width=13;s.planes[0].height=9;s.planes[0].extendedPixels[0]=19;
 Map map;minisurf_client_ref ref{&s,&map,nullptr,{0,0}};IOSurfaceRootUserClient uc{&ref};
 uint8_t out[0x300];memset(out,0xa5,sizeof(out));
 IOExternalMethodArguments args{out,0xd8,nullptr,0,nullptr,0};
 assert(!uc.returnSurface(&ref,false,&args));assert(args.structureOutputSize==0xd8);
 uint32_t value;memcpy(&value,out+0x60,4);assert(value==13);
 memcpy(&value,out+0xc8,4);assert(value==19);
 for(unsigned i=0xd8;i<sizeof(out);i++)assert(out[i]==0xa5);
 puts("PASS actual14 result serializer, plane stride, extended pixels and exact buffer bounds");
 args.structureOutputSize=0xd7;memset(out,0xa5,sizeof(out));
 assert(uc.returnSurface(&ref,false,&args)==kIOReturnBadArgument);for(auto c:out)assert(c==0xa5);
 puts("PASS short14 result rejected without writes; shared offsets statically match original getters");
 uint32_t request[3]={7,0,0};args.structureInput=request;args.structureInputSize=12;args.structureOutputSize=0xf60;
 assert(!uc.legacy(2,&args));uint64_t base;memcpy(&base,out,8);assert(base==map.getAddress()+PAGE_SIZE);
 memcpy(&value,out+0x48,4);assert(value==1);args.structureOutputSize=4;
 assert(!uc.legacy(3,&args));memcpy(&value,out,4);assert(value==2);
 puts("PASS actual14 lock/unlock envelope returns live address and seed through upstream ownership");
 uint64_t sid=7;args.scalarInput=&sid;args.scalarInputCount=1;
 assert(!uc.legacy(14,&args));assert(ref.use[0]==1&&s.state.useCount==1);
 assert(!uc.legacy(15,&args));assert(!ref.use[0]&&!s.state.useCount);
 assert(!uc.legacy(15,&args));assert(!ref.use[0]);
 args.scalarInputCount=0;assert(uc.legacy(14,&args)==kIOReturnBadArgument);
 args.scalarInputCount=1;assert(!uc.legacy(1,&args));assert(uc.released);
 puts("PASS method-form use counts balance without underflow, release reuses upstream helper");
 request[0]=8;args.structureOutputSize=0xf60;assert(uc.legacy(2,&args)==kIOReturnNotFound);
 request[0]=7;args.structureInputSize=11;assert(uc.legacy(2,&args)==kIOReturnBadArgument);
 puts("PASS stale IDs and malformed14 lock requests refused");
}
'''
    (dest/'surface.cpp').write_text(h)
    subprocess.run(['clang++','-std=c++17','-DLITHIUM_ARMV8_0=1',*flags,str(dest/'surface.cpp'),'-o',str(dest/'surface')],check=True)
    checks+=subprocess.check_output([str(dest/'surface')],text=True).splitlines()
    # Use the existing screen harness on the actual newly adapted loader.
    script=hakios.ROOT/'scripts/test-hakios-fault-summary.py'
    subprocess.run(['python3',str(script)],check=True)
    m=importlib.util.spec_from_file_location('summary_harness',script)
    summary=importlib.util.module_from_spec(m);m.loader.exec_module(summary)
    cases=r'''
    const char *ui="LITHIUM UI FINAL layers_copied=4 bb_inject=1 sb_queue=1 svc_exit_plus1=1 surf_exit_plus1=1\n"
        "LITHIUM UI SPRINGBOARD main_queue=REPLIED desktop_visible=UNVERIFIED\n"
        "userabort: backboardd[34] signal=6 pc=1801234\n"
        "LITHIUM UI DEADLINE progress_stalled_or_budget_ended=1 desktop=UNVERIFIED\n"
        "LITHIUM CLOCK elapsed_ms=900000 valid=1\n";
    screen(ui,strlen(ui));
    assert(strstr(output,"layers_copied=4")&&strstr(output,"bb_inject=1")&&strstr(output,"main_queue=REPLIED")&&
        strstr(output,"userabort: backboardd[34]")&&strstr(output,"PHYSICAL TOUCH NOT CONNECTED")&&
        strstr(output,"READ THIS SCREEN LATER")&&!strstr(output,"P2 STORAGE KERNEL:"));
    puts("PASS actual combined UI screen retains diagnostics and child abort without legacy scroll");
    const char *uip="LITHIUM UI FINAL layers_copied=0 sb_queue=0\n"
        "panic(cpu 0): real UI panic cause\nLITHIUM P2 STATE pc=abcdef\n";
    screen(uip,strlen(uip));
    assert(strstr(output,"KERNEL PANIC CAUSE")&&strstr(output,"real UI panic cause")&&strstr(output,"pc=abcdef"));
    assert(!strstr(output,"UI DEADLINE RETURN"));
    puts("PASS UI panic and missing progress remain distinct from a deadline return");
'''
    loader=(work/'xnuloader/loader.c').read_text()
    start=loader.index('static const char *\nlithium_last_line(')
    end=loader.index('\n#endif',loader.index('static void\nlithium_return_screen(',start))
    combined=summary.HARNESS.replace('/* ACTUAL_FUNCTIONS */',loader[start:end])
    combined=combined.replace('    assert(munmap(mapped,4096)==0);',cases+'\n    assert(munmap(mapped,4096)==0);')
    (dest/'ui-summary.c').write_text(combined)
    subprocess.run(['clang',*flags,str(dest/'ui-summary.c'),'-o',str(dest/'ui-summary')],check=True)
    output=subprocess.check_output([str(dest/'ui-summary')],text=True).splitlines()
    assert len(output)==19
    checks+=output[-2:]
    # The BGR24 harness already executes adapted drawRows and debug blit.
    subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-bgr24.py')],check=True)
    hakios.write(hakios.ROOT/'reports/hakios-ui-host-checks.json',dict(passed=True,checks=checks,
        actual_swap_adapter_exercised=True,actual_deadline_callback_exercised=True,sanitizer='address,undefined',
        framebuffer_source_sha256=hakios.sha(work/'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'),
        deadline_source_sha256=hakios.sha(work/'xnu/files/osfmk/arm64/loader_return.c'),
        patch_sha256=hakios.sha(hakios.ROOT/spec['port_path']/'patches/0016-lithium-integrated-ui.patch'),
        surface_patch_sha256={p.name:hakios.sha(p) for p in (hakios.ROOT/spec['port_path']/'patches').glob('001[78]-*.patch')},
        actual_surface_serializer_and_envelopes_exercised=True,
        source_sha256=hakios.sha(hakios.ROOT/'scripts/test-hakios-ui.py'),hardware_verified=False))
    print('\n'.join(checks))


if __name__=='__main__': main()
