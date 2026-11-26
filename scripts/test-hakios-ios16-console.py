#!/usr/bin/env python3
"""Exercise the actual console sink: BSD and tty text, CR, video/serial and UI ownership."""
from pathlib import Path
import subprocess
import hakios


def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    tree=work/'xnu-src/xnu-8796.121.2'
    source=tree/'osfmk/console/serial_console.c';text=source.read_text()
    r9=(work/'devices/lithium/diagnostics/r9.json').exists()
    begin=text.index('/* USB retains all emitted text;') if r9 else text.index('static inline void\n_cnputc(char c, bool poll)')
    puts_begin=text.index('static inline void\n_cnputs(char *c, int size, bool poll)',begin)
    end=text.index('\n}\n',puts_begin)+3
    actual=text[begin:end]
    km=tree/'bsd/dev/arm/km.c'
    hakios.require('loader_log_putc' not in km.read_text(),
        'TTY must not capture again before the shared console sink')
    kprintf=tree/'pexpert/arm/pe_kprintf.c'
    hakios.require('loader_log_putc(c);' in kprintf.read_text(),
        'Upstream direct early kprintf capture must remain')
    usertraps=tree/'osfmk/arm64/user_exception_trace.c'
    trap_text=usertraps.read_text()
    hakios.require('"usertraps", 0' in trap_text and
        '(!user_exception_trace_enabled && !user_exception_trace_only)' in trap_text,
        'Fatal tracing must work without global syscall tracing')
    harness=r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
static char ring[128],wire[128];static size_t nr,nw;
int disableConsoleOutput;
static unsigned idx,kernel_debugger_entry_count;static bool hide_video,saw_poll;
void loader_log_putc(char c) {if(c!='\r') {assert(nr<sizeof(ring)-1);ring[nr++]=c;ring[nr]=0;}}
void lithium_log_bytes(const char *s,unsigned n) {while(n--)loader_log_putc(*s++);}
#define os_atomic_load(p,m) __atomic_load_n(p,__ATOMIC_RELAXED)
#define os_atomic_inc_orig(p,m) __atomic_fetch_add(p,1,__ATOMIC_RELAXED)
static void target(char c,bool poll) {
    saw_poll|=poll;
    if(idx==1&&hide_video) return;
    assert(nw<sizeof(wire)-1);wire[nw++]=c;wire[nw]=0;
}
static const struct {void (*putc)(char,bool);} cons_ops[]={{target},{target}};
static uint32_t get_cons_ops_index(void) {return idx;}
'''+actual+r'''
static void reset(void) {nr=nw=0;ring[0]=wire[0]=0;saw_poll=false;hide_video=false;kernel_debugger_entry_count=0;disableConsoleOutput=0;}
int main(void) {
    for(idx=0;idx<2;idx++) {
        reset();char p[]="kernel and user text\n";
        _cnputs(p,(int)strlen(p),false);
        assert(!strcmp(wire,"kernel and user text\r\n"));
#if HAKIOS_LITHIUM_USB_LOG
        assert(!strcmp(ring,"kernel and user text\n"));
#else
        assert(nr==0);
#endif
    }
    idx=1;reset();hide_video=true;kernel_debugger_entry_count=1;
    const char *p="after UI\n";for(;*p;p++) _cnputc(*p,false);
    assert(nw==0&&saw_poll);
#if HAKIOS_LITHIUM_USB_LOG
    assert(!strcmp(ring,"after UI\n"));
#else
    assert(nr==0);
#endif
    reset();disableConsoleOutput=1;
    char muted[]="console muted\n";_cnputs(muted,(int)strlen(muted),false);
    assert(nw==0);
#if HAKIOS_LITHIUM_USB_LOG
    assert(!strcmp(ring,"console muted\n"));
#else
    assert(nr==0);
#endif
    _cnputs(NULL,7,false);_cnputs(muted,0,false);
    puts("PASS actual _cnputc: serial/video, CR normalization, single capture, UI ownership and debugger polling");
}
'''
    folder=work/'host-ios16-checks';folder.mkdir(exist_ok=True)
    c=folder/'console-sink.c';c.write_text(harness);checks=[]
    for enabled in (0,1):
        exe=folder/f'console-sink-{enabled}'
        subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined',f'-DHAKIOS_LITHIUM_USB_LOG={enabled}',c,'-o',exe],check=True)
        checks.append(dict(usb_enabled=bool(enabled),result=subprocess.check_output([str(exe)],text=True).strip()))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-console-checks.json',dict(passed=True,
        inputs=hakios.inputs(spec),checks=checks,actual_console_sink_exercised=True,
        sanitizers=['address','undefined'],hardware_verified=False,
        evidence_sha256={str(p):hakios.sha(p) for p in (source,km,kprintf,usertraps)},
        test_script_sha256=hakios.sha(Path(__file__))))
    print('PASS actual shared console sink in both build configurations; fatal trace independent of systrace')


if __name__=='__main__':main()
