#!/usr/bin/env python3
"""Exercise the actual loader panic-text decoder against mixed XNU records."""
from pathlib import Path
import re
import subprocess
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    src=work/'xnuloader/loader.c';text=src.read_text()
    begin=text.index('struct lithium_panic_prefix {')
    end=text.index('static void\nlithium_return_screen(',begin)
    code=text[begin:end]
    header=work/'xnu-src/xnu-8796.121.2/osfmk/kern/debug.h'
    original=header.read_text()
    assert re.search(r'#define EMBEDDED_PANIC_HEADER_CURRENT_VERSION 4\b',original)
    assert re.search(r'#define EMBEDDED_PANIC_MAGIC 0x46554E4B\b',original)
    start=original.index('struct embedded_panic_header {')
    stop=original.index('\n\tunion {',start)
    prefix=original[start:stop]+'\n} __attribute__((packed));\n'
    harness=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
typedef uint64_t eph_panic_flags_t;
static char output[32768];static size_t used;
static void put_char(char c) {assert(used<sizeof(output)-1);output[used++]=c;output[used]=0;}
static void print(const char *fmt,...) {
    va_list ap;va_start(ap,fmt);
    for (;*fmt;fmt++) {
        if (*fmt=='%'&&fmt[1]=='d') {
            char b[40];snprintf(b,sizeof(b),"%llu",(unsigned long long)va_arg(ap,uint64_t));
            for(char *p=b;*p;p++) put_char(*p);fmt++;
        } else put_char(*fmt);
    } va_end(ap);
}
'''+prefix+code+r'''
_Static_assert(offsetof(struct embedded_panic_header,eph_panic_log_offset)==
    offsetof(struct lithium_panic_prefix,text_offset),"text ABI");
_Static_assert(offsetof(struct embedded_panic_header,eph_panic_log_len)==
    offsetof(struct lithium_panic_prefix,text_len),"length ABI");
_Static_assert(offsetof(struct embedded_panic_header,eph_stackshot_offset)==
    offsetof(struct lithium_panic_prefix,stack_offset),"stack ABI");
_Static_assert(offsetof(struct embedded_panic_header,eph_stackshot_len)==
    offsetof(struct lithium_panic_prefix,stack_len),"stack length ABI");
static uint8_t region[32768];
static void reset(void) {used=0;output[0]=0;}
static unsigned rows(void) {
    unsigned n=0,col=0;
    for(size_t i=0;i<used;i++) {
        if(output[i]=='\n') {n++;col=0;}
        else {if(col==57) {n++;col=0;}col++;}
    }return n+(col!=0);
}
int main(void) {
    const char *body="panic(cpu 0 caller 0xffffff0012345678): regression fault\n"
        "Debugger message: kernel panic\nContext still readable\n"
        "Panicked task 0xff: pid 1 launchd\n"
        "Panicked thread: 0xff tid 1\n\t  lr: 0xffff001122334455  fp: 0xff\n"
        "\t  lr: 0xffff223344556677  fp: 0xff\n";
    struct lithium_panic_prefix h={.magic=0x46554e4b,.version=4,
        .text_offset=256,.text_len=(uint32_t)strlen(body),.stack_offset=4096,.stack_len=10000};
    memcpy(region,&h,sizeof(h));memcpy(region+h.text_offset,body,h.text_len);
    memset(region+h.stack_offset,0xa5,h.stack_len);
    memcpy(region+h.stack_offset,"BINARY_STACK_SENTINEL",21);
    reset();lithium_log_summary("truncated returned log",22,region,sizeof(region));
    assert(strstr(output,"regression fault")&&strstr(output,"pid 1 launchd"));
    assert(strstr(output,"0xffff001122334455")&&!strstr(output,"BINARY_STACK_SENTINEL"));
    assert(rows()<38&&lithium_summary_rows<=28);
    const char *p=NULL;uint64_t n=0;
    assert(lithium_panic_text(region,sizeof(region),&p,&n));
    assert(p==(char *)region+256&&n==strlen(body));
    h.text_offset=32760;h.text_len=0xffffffff;memcpy(region,&h,sizeof(h));
    assert(!lithium_panic_text(region,sizeof(region),&p,&n));
    h.text_offset=256;h.text_len=100;h.stack_offset=260;memcpy(region,&h,sizeof(h));
    assert(!lithium_panic_text(region,sizeof(region),&p,&n));
    h.version=5;memcpy(region,&h,sizeof(h));assert(!lithium_panic_text(region,sizeof(region),&p,&n));
    assert(!lithium_panic_text(NULL,0,&p,&n));
    const char fallback[]="panic(cpu 0): early fault\nboot context\n--- panic buffer ---\nBINARY_STACK_SENTINEL\n";
    reset();lithium_log_summary(fallback,sizeof(fallback)-1,NULL,0);
    assert(strstr(output,"early fault")&&!strstr(output,"BINARY_STACK_SENTINEL")&&rows()<38);
    const char timed[]="still booting\n--- loader_return_after expired ---\nlast file lookup = 2\n";
    reset();lithium_log_summary(timed,sizeof(timed)-1,NULL,0);
    assert(strstr(output,"RETURN: TIMER DEADLINE REACHED")&&strstr(output,"NO PANIC TEXT"));
    assert(!strstr(output,"PANIC CAUSE NOT RECOVERED")&&rows()<38);
    const char unknown[]="last file lookup = 2\n";
    reset();lithium_log_summary(unknown,sizeof(unknown)-1,NULL,0);
    assert(strstr(output,"RETURN: REASON UNCONFIRMED")&&!strstr(output,"TIMER DEADLINE"));
    const char shutdown_fault[]="\n--- loader_return_after expired ---\npanic(cpu 0): shutdown fault\n";
    reset();lithium_log_summary(shutdown_fault,sizeof(shutdown_fault)-1,NULL,0);
    assert(strstr(output,"shutdown fault")&&!strstr(output,"RETURN: TIMER DEADLINE"));
    char long_text[12000];memset(long_text,'X',sizeof(long_text));
    memcpy(long_text,"panic(",6);
    reset();lithium_log_summary(long_text,sizeof(long_text),NULL,0);
    assert(used<4000&&rows()<38&&lithium_summary_rows<=28);
    reset();lithium_log_summary(NULL,0,NULL,0);assert(strstr(output,"did not supply"));
    puts("PASS actual loader summary: v4 ABI, mixed binary/text, cause/task/backtrace, bad spans, timer/unknown/shutdown-fault precedence, long lines, null log");
}
'''
    folder=work/'host-ios16-checks';folder.mkdir(exist_ok=True)
    c=folder/'return-summary.c';c.write_text(harness);exe=folder/'return-summary'
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined',c,'-o',exe],check=True)
    result=subprocess.check_output([str(exe)],text=True).strip()
    hakios.write(hakios.ROOT/'reports/hakios-ios16-return-checks.json',dict(passed=True,
        inputs=hakios.inputs(spec),checks=[result],actual_production_functions=True,
        sanitizers=['address','undefined'],hardware_verified=False,
        evidence_sha256={str(src):hakios.sha(src),str(header):hakios.sha(header),
            'test_script':hakios.sha(Path(__file__))}))
    print(result)

if __name__=='__main__':main()
