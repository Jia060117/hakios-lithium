#!/usr/bin/env python3
"""Sanitize extracted R9 production code; never prepare, build or touch a device.

Run in the approved WSL systemd task (resource_check is mandatory). Sources may
be an unchanged R8 workspace, transformed in memory with hakios_r9, or the
generated R9 workspace. No production source is written. The JSON report names
which input was used and rejects inputs that change while the checks run.
"""
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True

import hakios
import hakios_r9 as r9


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def masked(text):
    """Keep offsets while ignoring braces in C comments and string literals."""
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: re.sub(r'[^\n]', ' ', m[0]), text)


def function(text, name):
    """Extract a definition, including its original signature, without rewriting it."""
    clean = masked(text)
    matches = re.finditer(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', clean)
    hits = []
    for match in matches:
        line_start = text.rfind('\n', 0, match.start()) + 1
        declaration = text[line_start:match.start()].strip()
        if not declaration:
            previous_start = text.rfind('\n', 0, max(0, line_start - 1)) + 1
            declaration = text[previous_start:line_start].strip()
        if any(char in declaration for char in '()[]=,&|!?:') or not declaration:
            continue
        if declaration.split()[0] in {'if', 'for', 'while', 'return', 'switch', 'case'}:
            continue
        hits.append(match)
    hakios.require(len(hits) == 1, 'Expected one production definition: ' + name + ' (found ' + str(len(hits)) + ')')
    hit = hits[0]
    start = text.rfind('\n', 0, hit.start()) + 1
    if not text[start:hit.start()].strip():
        start = text.rfind('\n', 0, start - 1) + 1
    brace = clean.index('{', hit.start())
    depth, end = 1, brace + 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    hakios.require(depth == 0, 'Unbalanced production definition: ' + name)
    return text[start:end] + '\n'


class Sources:
    def __init__(self, work, port):
        self.work, self.port = work, port
        self.files, self.selections, self.functions = {}, {}, {}

    def read(self, path):
        path = Path(path)
        raw = path.read_bytes()
        value = hashlib.sha256(raw).hexdigest()
        old = self.files.setdefault(str(path), value)
        hakios.require(old == value, 'Input changed during extraction: ' + str(path))
        return raw.decode('utf-8').replace('\r\n', '\n')

    def select(self, name, transform, marker):
        text = self.read(self.work / name)
        actual = marker in text or (name=='userland/src/inject.c' and 'diagnostics/ls-ready-r11.inc' in text)
        result = text if actual else transform(text)
        hakios.require(actual or marker in result, 'Missing production marker: ' + name)
        self.selections[name] = dict(mode='workspace-r9' if actual else 'r8-transformed-in-memory',
                                     input_text_sha256=digest(text), extracted_text_sha256=digest(result))
        contract_path = self.port / 'diagnostics/r9.json'
        if contract_path.exists():
            import json
            contract = json.loads(self.read(contract_path))
            expected = contract.get('after_sha256', {}).get(name)
            r11_path=self.port/'diagnostics/r11.json'
            if r11_path.exists():
                expected=json.loads(self.read(r11_path)).get('after_sha256',{}).get(name,expected)
            r12_path=self.port/'diagnostics/r12.json'
            if r12_path.exists():
                expected=json.loads(self.read(r12_path)).get('after_sha256',{}).get(name,expected)
            r13_path=self.port/'diagnostics/r13.json'
            if r13_path.exists():
                expected=json.loads(self.read(r13_path)).get('after_sha256',{}).get(name,expected)
            r15_path=self.port/'diagnostics/r15.json'
            if r15_path.exists():
                expected=json.loads(self.read(r15_path)).get('after_sha256',{}).get(name,expected)
            r16_path=self.port/'diagnostics/r16.json'
            if r16_path.exists():
                c=json.loads(self.read(r16_path))
                if name in c.get('before_sha256',{}) and c['before_sha256'][name]==expected:
                    expected=c['after_sha256'][name]
            r18_path=self.port/'diagnostics/r18.json'
            if r18_path.exists():
                c=json.loads(self.read(r18_path))
                if expected and c.get('before_sha256',{}).get(name)==expected:
                    expected=c['after_sha256'][name]
            if expected:
                hakios.require(digest(result) == expected, 'R9 contract differs: ' + name)
        return result

    def include(self, name, deployed):
        path = self.port / 'diagnostics' / name
        code = self.read(path)
        target = self.work / 'devices/lithium/diagnostics' / name
        if deployed:
            hakios.require(target.exists(), 'R9 diagnostic has not been synchronized: ' + name)
            hakios.require(self.read(target) == code, 'Workspace/repository diagnostic differs: ' + name)
        self.selections['diagnostics/' + name] = dict(
            mode='workspace-and-repository' if deployed else 'repository-r9-candidate',
            extracted_text_sha256=digest(code))
        return code

    def extract(self, text, *names):
        result = []
        for name in names:
            code = function(text, name)
            self.functions[name] = digest(code)
            result.append(code)
        return '\n'.join(result)

    def stable(self):
        for name, expected in self.files.items():
            hakios.require(Path(name).exists() and hakios.sha(Path(name)) == expected,
                           'Input changed during checks; synchronize and rerun: ' + name)


COMMON = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
'''


def compile_run(folder, name, code, report, defines=()):
    source, binary = folder / (name + '.c'), folder / name
    source.write_text(code, encoding='utf-8', newline='\n')
    command = ['clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-Wno-unused-variable', '-fno-omit-frame-pointer',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               *defines, str(source), '-o', str(binary)]
    entry = dict(name=name, passed=False, command=command, harness_sha256=digest(code),
                 harness_path=str(source), production_functions_extracted=True)
    report['checks'].append(entry)
    built = subprocess.run(command, capture_output=True, text=True, timeout=90)
    entry['compile_stdout'], entry['compile_stderr'] = built.stdout, built.stderr
    hakios.require(built.returncode == 0, name + ' compilation failed:\n' + built.stdout + built.stderr)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    run = subprocess.run([str(binary)], capture_output=True, text=True, env=env, timeout=45)
    entry.update(stdout=run.stdout, stderr=run.stderr, returncode=run.returncode,
                 executable_sha256=hakios.sha(binary))
    hakios.require(run.returncode == 0 and run.stdout.startswith('PASS '),
                   name + ' failed:\n' + run.stdout + run.stderr)
    entry['passed'] = True
    print(run.stdout.strip(), flush=True)


def cpu_harness(src, inc, loader):
    helpers = src.extract(loader, 'platform_cpu_mpidr', 'platform_cpu_index')
    src.extract(inc, 'lithium_cpu_mpidr', 'lithium_cpu_index')
    return COMMON + r'''
#define HAKIOS_LITHIUM 1
struct platform { unsigned core_shift; };
static const struct platform platform_lithium = { 8 }, other_platform = { 8 };
''' + inc + helpers + r'''
int main(void) {
    static const uint64_t expected[] = {0,1,0x100,0x101};
    for (int i=0;i<4;i++) {
        assert(lithium_cpu_mpidr(i)==expected[i]);
        assert(lithium_cpu_index(expected[i])==i);
        assert(platform_cpu_mpidr(&platform_lithium,i)==expected[i]);
        assert(platform_cpu_index(&platform_lithium,expected[i])==i);
        assert(lithium_cpu_index(lithium_cpu_mpidr(i))==i);
    }
    const int invalid[] = {INT_MIN,-1024,-1,4,5,31,INT_MAX};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        assert(lithium_cpu_mpidr(invalid[i])==UINT32_MAX);
        assert(platform_cpu_mpidr(&platform_lithium,invalid[i])==UINT32_MAX);
    }
    for (uint64_t mpidr=0;mpidr<0x10000;mpidr++) {
        int want=mpidr==0?0:mpidr==1?1:mpidr==0x100?2:mpidr==0x101?3:-1;
        assert(lithium_cpu_index(mpidr)==want);
        assert(platform_cpu_index(&platform_lithium,mpidr)==want);
    }
    const uint64_t bad[] = {UINT64_MAX,UINT32_MAX,0x100000000ULL,0x8000000000000100ULL};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
        assert(lithium_cpu_index(bad[i])==-1 && platform_cpu_index(&platform_lithium,bad[i])==-1);
    for (int i=0;i<4;i++) {
        assert(platform_cpu_mpidr(&other_platform,i)==((uint64_t)i<<8));
        assert(platform_cpu_index(&other_platform,(uint64_t)i<<8)==i);
    }
    puts("PASS CPU actual MPIDR mapper/platform dispatch: roundtrips, 65536 affinities, invalid indices/high bits");
}
'''


def storage_harness(src, storage):
    code = src.extract(storage, 'ufs_data_ioctl', 'ufs_ioctl')
    hakios.require(code.count('case DKIOCISSOLIDSTATE:') == 2, 'Both real ioctl handlers must answer SSD')
    # Test the transform itself, including its refusal to silently miss a handler.
    insertion = '\tcase DKIOCISSOLIDSTATE:\n\t\t*(uint32_t *)data = 1;\n\t\treturn 0;\n'
    hakios.require(storage.count(insertion) == 2, 'SSD insertion differs; review test adaptation')
    r8 = storage.replace(insertion, '')
    hakios.require(r9.storage_source(r8) == storage, 'storage_source modifies more than the two SSD cases')
    for bad in (r8.replace('\tcase DKIOCGETPHYSICALBLOCKSIZE:', '', 1),
                r8 + '\n\tcase DKIOCGETPHYSICALBLOCKSIZE:\n'):
        try:
            r9.storage_source(bad)
        except RuntimeError:
            pass
        else:
            raise AssertionError('storage_source accepted an unexpected handler count')
    return COMMON + r'''
#include <errno.h>
#define __unused __attribute__((unused))
typedef unsigned long u_long;
typedef char *caddr_t;
typedef unsigned long dev_t;
typedef void *proc_t;
enum {DKIOCGETBLOCKSIZE=1, DKIOCSETBLOCKSIZE, DKIOCGETPHYSICALBLOCKSIZE,
      DKIOCGETBLOCKCOUNT, DKIOCISWRITABLE, DKIOCGETMAXBLOCKCOUNTREAD,
      DKIOCGETMAXBLOCKCOUNTWRITE, DKIOCSYNCHRONIZE, DKIOCSYNCHRONIZECACHE,
      DKIOCISSOLIDSTATE};
#define minor(dev) ((unsigned)(dev))
static unsigned ufs_data_bsize=4096,ufs_parts=2,ufs_buffer_size=65536;
static unsigned ufs_bsize[2]={512,4096};
static bool ufs_ready=true;
static struct { struct {uint32_t block_size;} part[2]; } page={{{4096},{4096}}},*ufs_page=&page;
static unsigned flush_calls;
static bool ufs_is_data(dev_t dev) { return dev==99; }
static uint64_t ufs_data_bytes(void) { return 16ULL*1024*1024; }
static uint64_t ufs_part_bytes(uint32_t part) { assert(part<2);return (part+1)*8ULL*1024*1024; }
static int ufs_data_flush(void) { flush_calls++;return 37; }
''' + code + r'''
int main(void) {
    struct {uint32_t before,value,after;} out;
    for (unsigned pass=0;pass<4;pass++) {
        out.before=0xa5a55a5a;out.after=0x12345678;out.value=0xdeadbeef;
        int rc=pass==0?ufs_data_ioctl(DKIOCISSOLIDSTATE,(char*)&out.value):
            ufs_ioctl(pass==1?99:pass-2,DKIOCISSOLIDSTATE,(char*)&out.value,0,NULL);
        assert(rc==0 && out.value==1 && out.before==0xa5a55a5a && out.after==0x12345678);
    }
    uint32_t v=0;
    assert(!ufs_data_ioctl(DKIOCISWRITABLE,(char*)&v) && v==1);
    assert(!ufs_ioctl(0,DKIOCISWRITABLE,(char*)&v,0,NULL) && v==0);
    assert(!ufs_data_ioctl(DKIOCGETPHYSICALBLOCKSIZE,(char*)&v) && v==4096);
    assert(!ufs_ioctl(0,DKIOCGETPHYSICALBLOCKSIZE,(char*)&v,0,NULL) && v==4096);
    v=512;assert(!ufs_data_ioctl(DKIOCSETBLOCKSIZE,(char*)&v));
    v=0;assert(!ufs_data_ioctl(DKIOCGETBLOCKSIZE,(char*)&v) && v==512);
    v=513;assert(ufs_data_ioctl(DKIOCSETBLOCKSIZE,(char*)&v)==EINVAL);
    v=8192;assert(ufs_ioctl(0,DKIOCSETBLOCKSIZE,(char*)&v,0,NULL)==EINVAL);
    v=1024;assert(!ufs_ioctl(0,DKIOCSETBLOCKSIZE,(char*)&v,0,NULL));
    uint64_t n=0;assert(!ufs_ioctl(0,DKIOCGETBLOCKCOUNT,(char*)&n,0,NULL) && n==8192);
    assert(ufs_data_ioctl(DKIOCSYNCHRONIZE,NULL)==37 && flush_calls==1);
    assert(ufs_ioctl(99,DKIOCSYNCHRONIZECACHE,NULL,0,NULL)==37 && flush_calls==2);
    v=0xdeadbeef;
    assert(ufs_ioctl(2,DKIOCISSOLIDSTATE,(char*)&v,0,NULL)==ENXIO && v==0xdeadbeef);
    ufs_ready=false;
    assert(ufs_ioctl(0,DKIOCISSOLIDSTATE,(char*)&v,0,NULL)==ENXIO && v==0xdeadbeef);
    assert(!ufs_ioctl(99,DKIOCISSOLIDSTATE,(char*)&v,0,NULL) && v==1);
    assert(ufs_data_ioctl(999,(char*)&v)==ENOTTY);
    assert(ufs_ioctl(99,999,(char*)&v,0,NULL)==ENOTTY);
    puts("PASS UFS actual Data/partition ioctl handlers: SSD=1, 32-bit guards, readiness, writable/geometry/flush regressions");
}
'''


def ls_harness(src, inc):
    names = ('lithium_ls_callsite', 'lithium_ls_caller_pc', 'lithium_ls_event',
             'lithium_group_enter', 'lithium_group_leave', 'lithium_group_wait',
             'lithium_init', 'lithium_seed', 'lithium_cryptex', 'lithium_complete',
             'lithium_sync', 'lithium_fetch', 'lithium_recovery', 'lithium_restore')
    src.extract(inc, *names)
    stop = inc.index(function(inc, 'lithium_ls_hook').rstrip())
    code = inc[:stop]
    hakios.require(not re.search(r'\bdispatch_(?:group_(?:notify|async|wait)|after)\s*\(', masked(code)),
                   'Observer added dispatch operations; review semantics before testing')
    prefix = COMMON + r'''
typedef void *dispatch_group_t;
typedef uint64_t dispatch_time_t;
typedef void *id;
typedef const void *SEL;
#define nil NULL
static uintptr_t slide,return_pc;
static unsigned event_count,real_calls[11],events_at_real,budget_notices;
static long real_result;
static dispatch_group_t expected_group;
static dispatch_time_t expected_timeout;
static id expected_self,expected_arg,init_result;
static SEL expected_sel;
static bool *expected_out,expected_complete;
struct event {char kind[64];dispatch_group_t group;uintptr_t pc;long value;};
static struct event events[256];
static void *host_return_address(unsigned depth) {assert(depth==0);return (void*)return_pc;}
/* Override only the CPU-specific return-address intrinsic; wrappers stay intact. */
#define __builtin_return_address(depth) host_return_address(depth)
static int pthread_threadid_np(void *thread,uint64_t *tid) {assert(!thread);*tid=0x123;return 0;}
static uint64_t mach_absolute_time(void) {return 0x456;}
static void say(const char *fmt,...) {
	if (strstr(fmt,"event budget exhausted")) { budget_notices++; return; }
    assert(strstr(fmt,"LITHIUM LS event=") && event_count<256);
    va_list ap;va_start(ap,fmt);
    const char *kind=va_arg(ap,const char*);unsigned seq=va_arg(ap,unsigned);
    assert(seq==event_count+1 && va_arg(ap,unsigned long long)==0x123);
    struct event *e=&events[event_count++];
    assert(strlen(kind)<sizeof(e->kind));strcpy(e->kind,kind);
    e->group=va_arg(ap,dispatch_group_t);e->pc=va_arg(ap,uintptr_t);e->value=va_arg(ap,long);
    assert(va_arg(ap,unsigned long long)==0x456);va_end(ap);
}
static void real_enter(dispatch_group_t g) {assert(g==expected_group);real_calls[0]++;events_at_real=event_count;}
static void real_leave(dispatch_group_t g) {assert(g==expected_group);real_calls[1]++;events_at_real=event_count;}
static long real_wait(dispatch_group_t g,dispatch_time_t t) {
    assert(g==expected_group && t==expected_timeout);real_calls[2]++;events_at_real=event_count;return real_result;
}
static void method_args(id self,SEL sel) {assert(self==expected_self && sel==expected_sel);}
static id real_init(id self,SEL sel,id arg) {method_args(self,sel);assert(arg==expected_arg);real_calls[3]++;return init_result;}
static void real_seed(id self,SEL sel,bool *out) {method_args(self,sel);assert(out==expected_out);real_calls[4]++;if(out)*out=true;}
static void real_cryptex(id self,SEL sel,bool *out) {method_args(self,sel);assert(out==expected_out);real_calls[5]++;if(out)*out=false;}
static void real_complete(id self,SEL sel,bool done) {method_args(self,sel);assert(done==expected_complete);real_calls[6]++;}
static void real_sync(id self,SEL sel,void *arg) {method_args(self,sel);assert(arg==expected_arg);real_calls[7]++;}
static void real_fetch(id self,SEL sel,id arg) {method_args(self,sel);assert(arg==expected_arg);real_calls[8]++;}
static void real_recovery(id self,SEL sel) {method_args(self,sel);real_calls[9]++;}
static void real_restore(id self,SEL sel) {method_args(self,sel);real_calls[10]++;}
'''
    tests = r'''
static void reset_events(void) {event_count=0;lithium_ls_events=0;lithium_ls_dropped=0;budget_notices=0;}
static void event_at(unsigned at,const char *kind,dispatch_group_t group,uintptr_t pc,long value) {
    assert(at<event_count && !strcmp(events[at].kind,kind));
    assert(events[at].group==group && events[at].pc==pc && events[at].value==value);
}
int main(void) {
    static const uintptr_t sites[3][8]={
        {0x181c54750UL,0x181c5879cUL,0x181c8aeb4UL,0x181c8b0b4UL},
        {0x181c5466cUL,0x181c589e4UL,0x181c8af28UL,0x181c8b308UL},
        {0x181c00860UL,0x181c0436cUL,0x181c9b2b8UL,0x181cc498cUL,
         0x181cea484UL,0x181cea49cUL,0x181cea4b4UL,0x181d07bf8UL}};
    static const unsigned counts[]={4,4,8};
    for (unsigned op=0;op<3;op++) for (unsigned j=0;j<counts[op];j++) {
        for (unsigned test=0;test<3;test++)
            assert(lithium_ls_callsite(test,sites[op][j])==(test==op));
        assert(!lithium_ls_callsite(op,sites[op][j]+1));
        assert(!lithium_ls_callsite(op,sites[op][j]-4));
    }
    const uintptr_t slides[]={0,0x1000,0x7fff000};
    for (unsigned s=0;s<3;s++) {
        slide=slides[s];assert(!lithium_ls_caller_pc(0));
        assert(!lithium_ls_caller_pc(slide+3) && !lithium_ls_caller_pc(slide+4));
        for (unsigned op=0;op<3;op++) for (unsigned j=0;j<counts[op];j++)
            assert(lithium_ls_caller_pc(sites[op][j]+slide+4)==sites[op][j]);
    }
    lithium_real_enter=real_enter;lithium_real_leave=real_leave;lithium_real_wait=real_wait;
    int groups[2];slide=0x35000;
    for (unsigned g=0;g<2;g++) for (unsigned op=0;op<3;op++)
    for (unsigned j=0;j<counts[op];j++) for (unsigned observed=0;observed<2;observed++) {
        reset_events();expected_group=&groups[g];
        uintptr_t pc=observed?sites[op][j]:0x181000000UL;
        return_pc=pc+slide+4;unsigned before=real_calls[op];
        expected_timeout=j%3==0?0:j%3==1?UINT64_MAX:0x123456789ULL;
        real_result=j%3==0?0:j%3==1?-17:42;
        if(op==0)lithium_group_enter(expected_group);
        else if(op==1)lithium_group_leave(expected_group);
        else assert(lithium_group_wait(expected_group,expected_timeout)==real_result);
        assert(real_calls[op]==before+1);
        assert(event_count==(observed?(op==2?2u:1u):0u));
        assert(events_at_real==(observed && op==2?1u:0u));
        if (observed) {
            if(op==2) {
                event_at(0,"group-wait-enter",expected_group,pc,(long)expected_timeout);
                event_at(1,"group-wait-return",expected_group,pc,real_result);
            } else event_at(0,op==0?"group-enter":"group-leave",expected_group,pc,0);
        }
    }
    lithium_orig_init=real_init;lithium_orig_seed=real_seed;lithium_orig_cryptex=real_cryptex;
    lithium_orig_complete=real_complete;lithium_orig_sync=real_sync;lithium_orig_fetch=real_fetch;
    lithium_orig_recovery=real_recovery;lithium_orig_restore=real_restore;
    expected_self=&groups[0];expected_sel=&groups[1];
    for (unsigned nullable=0;nullable<2;nullable++) {
        reset_events();expected_arg=nullable?NULL:&groups[1];init_result=expected_arg;
        assert(lithium_init(expected_self,expected_sel,expected_arg)==init_result);
        event_at(0,"builder-init-return",NULL,0,init_result!=NULL);
        bool out=false;expected_out=nullable?NULL:&out;
        lithium_seed(expected_self,expected_sel,expected_out);assert(nullable || out);
        event_at(2,"database-seed-return",NULL,0,nullable?-1:1);
        lithium_cryptex(expected_self,expected_sel,expected_out);assert(nullable || !out);
        event_at(4,"cryptex-seed-return",NULL,0,nullable?-1:0);
        expected_complete=nullable;lithium_complete(expected_self,expected_sel,expected_complete);
        lithium_sync(expected_self,expected_sel,expected_arg);
        lithium_fetch(expected_self,expected_sel,expected_arg);
        event_at(10,"blocking-fetch-method-return",NULL,0,0);
        lithium_recovery(expected_self,expected_sel);lithium_restore(expected_self,expected_sel);
        assert(event_count==15);
    }
    for (unsigned i=3;i<11;i++) assert(real_calls[i]==2);
    reset_events();expected_group=&groups[0];expected_timeout=UINT64_MAX;real_result=-33;
    return_pc=sites[2][0]+slide+4;
    unsigned before=real_calls[2];
    for (unsigned i=0;i<140;i++) assert(lithium_group_wait(expected_group,expected_timeout)==-33);
    assert(real_calls[2]==before+140 && event_count==256 && lithium_ls_events==280 && lithium_ls_dropped==24 && budget_notices==1);
    puts("PASS LS actual callsites/slide/14 helpers: all wrappers forward once, preserve group/timeout/result, bounded events");
}
'''
    return prefix + code + tests


def ipc_harness(src, inc):
    capture = src.extract(inc, 'lithium_ipc_send_locked')
    src.extract(inc, 'lithium_ipc_selected', 'lithium_ipc_history_dump')
    hakios.require(not re.search(r'\b(?:printf|IOLog|os_log\w*|kprintf|say|\w*alloc\w*|free|'
                                 r'ip_mq_unlock|ml_set_interrupts_enabled|__builtin_arm_wsr\w*)\s*\(',
                                 masked(capture)), 'IPC capture emits, allocates, unlocks or masks interrupts')
    code = re.sub(r'^#include[^\n]*\n', '', inc, flags=re.M)
    return COMMON + r'''
typedef int32_t mach_msg_id_t;
typedef uint32_t mach_msg_bits_t;
struct port {bool locked;int receiver;};typedef struct port *ipc_port_t;
struct proc {const char *name;int pid;};
static struct proc process={"lsd",20};static struct port port={false,25};
static bool no_proc;static uint64_t current_tid,ticks;
static unsigned cas_calls,owned,queries,printed,allocations;
static char output[131072];static size_t output_used;
static void *current_task(void) {return &process;}
static struct proc *get_bsdtask_info(void *task) {assert(task==&process);return no_proc?NULL:&process;}
static const char *proc_best_name(struct proc *p) {assert(p==&process);return p->name;}
static int proc_pid(struct proc *p) {assert(p==&process);return p->pid;}
static void *current_thread(void) {return &current_tid;}
static uint64_t thread_tid(void *t) {assert(t==&current_tid);return current_tid;}
static uint64_t mach_absolute_time(void) {return ++ticks;}
static int ipc_port_get_receiver_task_locked(ipc_port_t p,uintptr_t *task) {
    assert(p==&port && p->locked && owned==1 && !task);queries++;return p->receiver;
}
static bool host_cas(uint32_t *p,uint32_t expected,uint32_t desired) {
    cas_calls++;bool ok=__atomic_compare_exchange_n(p,&expected,desired,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
    if(ok)owned++;return ok;
}
static void host_release(uint32_t *p,uint32_t value) {assert(owned==1 && *p==1 && value==0);owned--;__atomic_store_n(p,value,__ATOMIC_RELEASE);}
#define os_atomic_cmpxchg(p,a,b,order) host_cas(p,a,b)
#define os_atomic_store(p,v,order) host_release(p,v)
#define os_atomic_inc(p,order) ((void)__atomic_add_fetch(p,1,__ATOMIC_RELAXED))
#define os_atomic_load(p,order) __atomic_load_n(p,__ATOMIC_RELAXED)
static int host_print(const char *fmt,...) {
    assert(!port.locked && owned==0);printed++;
    va_list ap;va_start(ap,fmt);int n=vsnprintf(output+output_used,sizeof(output)-output_used,fmt,ap);va_end(ap);
    assert(n>0 && (size_t)n<sizeof(output)-output_used);output_used+=(size_t)n;return n;
}
static void *host_alloc(void) {assert(!port.locked && owned==0);allocations++;return NULL;}
#define printf host_print
#define IOLog host_print
#define kprintf host_print
#define malloc(...) host_alloc()
#define calloc(...) host_alloc()
#define realloc(...) host_alloc()
#define kalloc_data(...) host_alloc()
''' + code + r'''
static void reset(void) {
    memset(lithium_ipc_slots,0,sizeof(lithium_ipc_slots));memset(lithium_ipc_dumped,0,sizeof(lithium_ipc_dumped));
    lithium_ipc_sequence=0;lithium_ipc_dropped=0;cas_calls=owned=queries=printed=allocations=0;
    output_used=0;output[0]=0;no_proc=false;process.name="lsd";process.pid=20;ticks=0;current_tid=7;
}
static void send_attempt(int msgid,unsigned bits) {
    unsigned before=printed;port.locked=true;lithium_ipc_send_locked(&port,msgid,bits);
    assert(port.locked && !owned && before==printed && !allocations);port.locked=false;
}
int main(void) {
    const char *selected[]={"lsd","cfprefsd","containermanagerd","containermanagerd_system",
        "SpringBoard","backboardd","runningboardd","installcoordinationd"};
    const char *excluded[]={NULL,"","lsd_helper","LSd","cfprefsd_helper","boardd","containermanage","other"};
    reset();assert(LITHIUM_IPC_SLOTS==128);
    for (unsigned i=0;i<8;i++) {
        assert(lithium_ipc_selected(selected[i]));process.name=selected[i];current_tid=i;
        send_attempt(100+(int)i,0x80000000u+i);
        struct lithium_ipc_record *r=&lithium_ipc_slots[i].record;
        assert(r->tid==i && r->sender==20 && r->receiver==25 && r->msgid==100+(int)i);
        assert(r->bits==0x80000000u+i && r->sequence==i+1 && r->ticks==i+1);
    }
    assert(queries==8 && cas_calls==8 && !printed);
    for (unsigned i=0;i<8;i++) {assert(!lithium_ipc_selected(excluded[i]));process.name=excluded[i];send_attempt(0,0);}
    no_proc=true;send_attempt(0,0);assert(cas_calls==8 && queries==8);no_proc=false;
    lithium_ipc_history_dump();assert(printed==9 && strstr(output,"emitted=8"));
    assert(strstr(output,"attempt_only=1") && strstr(output,"scope=latest-per-hashed-slot"));
    lithium_ipc_history_dump();assert(printed==10 && strstr(output,"emitted=0"));
    reset();send_attempt(101,0x12);struct lithium_ipc_record saved=lithium_ipc_slots[7].record;
    lithium_ipc_slots[7].busy=1;unsigned attempts=cas_calls;
    send_attempt(999,0xff);
    assert(cas_calls==attempts+1 && lithium_ipc_dropped==1 && queries==1);
    assert(!memcmp(&saved,&lithium_ipc_slots[7].record,sizeof(saved)));
    lithium_ipc_history_dump();assert(printed==1 && !lithium_ipc_dumped[7]);
    lithium_ipc_slots[7].busy=0;lithium_ipc_history_dump();
    assert(printed==3 && lithium_ipc_dumped[7]==saved.sequence);
    /* Collision replaces the slot, rather than attributing another thread's record. */
    current_tid=7+128;send_attempt(202,0x34);
    assert(lithium_ipc_slots[7].record.tid==135 && lithium_ipc_slots[7].record.msgid==202);
    assert(lithium_ipc_slots[7].record.sequence==2 && lithium_ipc_sequence==2);
    output_used=0;output[0]=0;lithium_ipc_history_dump();
    assert(strstr(output,"tid=0x87") && strstr(output,"msgid=202") && !strstr(output,"msgid=101"));
    reset();
    for (unsigned i=0;i<128;i++) {current_tid=(uint64_t)i+0x100000000ULL;send_attempt((int)i,i);}
    assert(queries==128 && lithium_ipc_sequence==128);
    for(unsigned i=0;i<128;i++) assert(lithium_ipc_slots[i].record.tid==(uint64_t)i+0x100000000ULL);
    lithium_ipc_history_dump();assert(printed==129 && !owned && !allocations);
    puts("PASS IPC actual hashed capture/dump: 8 selected names, trylock drop, collision, 128 slots, no locked print/allocation");
}
'''


def log_console_harness(src, returned, console, general, protos):
    defines = []
    for name in ('LOADER_LOG_SIZE', 'LOADER_LOG_HEAD', 'LOADER_LOG_RING'):
        hit = re.search(r'^#define ' + name + r'\b[^\n]*', returned, re.M)
        hakios.require(hit is not None, 'Missing retained-ring size: ' + name)
        defines.append(hit[0])
    log_functions = src.extract(returned, 'loader_log_put_locked', 'loader_log_putc',
                                'lithium_log_bytes', 'lithium_log_copy')
    sink = console[console.index('/* USB retains all emitted text;'):console.index('/**\n * Attempt to reserve space')]
    src.extract(console, 'lithium_screen_echo', '_cnputc_physical', '_cnputc', '_cnputs')
    state = protos[protos.index('#define SERIAL_CONS_BUF_SIZE'):protos.index('extern int console_printbuf_drain_initialized')]
    drain = src.extract(general, 'console_printbuf_state_init', 'console_printbuf_putc', 'console_printbuf_clear')
    return COMMON + '\n'.join(defines) + r'''
char loader_log[LOADER_LOG_SIZE];
uint32_t loader_log_len;
static uint32_t loader_log_ring_at,loader_log_lock;
static bool loader_log_wrapped,loader_log_linear;
static uint64_t lithium_log_total,lithium_log_contention_dropped_bytes;
static uint64_t daif=0x40,irq_start_total;
static unsigned mask_calls,restore_calls,lock_calls,max_irq_bytes;
static bool force_lock_failure;
#define DAIFSC_IRQF 2
#define DAIFSC_FIQF 1
static uint64_t host_rsr(const char *reg) {assert(!strcmp(reg,"DAIF"));return daif;}
static void host_wsr(const char *reg,unsigned flags) {
    assert(!strcmp(reg,"DAIFSet") && flags==(DAIFSC_IRQF|DAIFSC_FIQF) && daif==0x40);
    mask_calls++;irq_start_total=lithium_log_total;daif=0xc0;
}
static void host_restore(const char *reg,uint64_t value) {
    assert(!strcmp(reg,"DAIF") && value==0x40 && daif==0xc0 && !loader_log_lock);
    unsigned bytes=(unsigned)(lithium_log_total-irq_start_total);assert(bytes<=256);
    if(bytes>max_irq_bytes)max_irq_bytes=bytes;restore_calls++;daif=value;
}
static bool host_log_lock(uint32_t *p,unsigned old,unsigned value) {
    assert(p==&loader_log_lock && old==0 && value==1 && daif==0xc0);lock_calls++;
    if(force_lock_failure)return false;
    return __atomic_compare_exchange_n(p,&old,value,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void host_log_unlock(uint32_t *p,unsigned value) {
    assert(p==&loader_log_lock && *p==1 && value==0 && daif==0xc0);__atomic_store_n(p,value,__ATOMIC_RELEASE);
}
#define __builtin_arm_rsr64(reg) host_rsr(reg)
#define __builtin_arm_wsr(reg,value) host_wsr(reg,value)
#define __builtin_arm_wsr64(reg,value) host_restore(reg,value)
#define os_atomic_cmpxchg(p,a,b,order) host_log_lock(p,a,b)
#define os_atomic_store(p,v,order) host_log_unlock(p,v)
#define os_atomic_load(p,order) __atomic_load_n(p,__ATOMIC_RELAXED)
#define os_atomic_inc_orig(p,order) __atomic_fetch_add(p,1,__ATOMIC_RELAXED)
''' + log_functions + r'''
int disableConsoleOutput;
static unsigned kernel_debugger_entry_count,cons_index,screen_calls,poll_calls;
static unsigned char screen[65536];static size_t screen_used;
static void physical(char c,bool poll) {
    assert(!loader_log_lock && daif==0x40 && screen_used<sizeof(screen));
    screen[screen_used++]=(unsigned char)c;screen_calls++;poll_calls+=poll;
}
static const struct {void (*putc)(char,bool);} cons_ops[]={{physical},{physical}};
static uint32_t get_cons_ops_index(void) {assert(cons_index<2);return cons_index;}
''' + sink + r'''
#define bzero(p,n) memset(p,0,n)
#define SERIALMODE_SYNCDRAIN 1
static int serialmode;
static int console_write(char *bytes,int size) {_cnputs(bytes,size,false);return size;}
''' + state + drain + r'''
static void reset(void) {
    loader_log_len=loader_log_ring_at=loader_log_lock=0;loader_log_wrapped=loader_log_linear=false;
    lithium_log_total=lithium_log_contention_dropped_bytes=0;
    mask_calls=restore_calls=lock_calls=max_irq_bytes=0;force_lock_failure=false;daif=0x40;
    screen_used=screen_calls=poll_calls=0;kernel_debugger_entry_count=disableConsoleOutput=0;
#if HAKIOS_LITHIUM_USB_LOG
    lithium_screen_used=0;
#endif
}
static size_t without_cr(const unsigned char *in,size_t n,unsigned char *out) {
    size_t used=0;for(size_t i=0;i<n;i++)if(in[i]!='\r')out[used++]=in[i];return used;
}
static void check_log(const unsigned char *in,size_t n) {
    unsigned char expected[20000],copy[20000];size_t want=without_cr(in,n,expected);
    assert(lithium_log_total==want && loader_log_len==want && !lithium_log_contention_dropped_bytes && !loader_log_lock && daif==0x40);
    assert(!memcmp(loader_log,expected,want));
    uint64_t cursor=0,lost=0;size_t used=0;
    while(cursor<lithium_log_total) {
        uint32_t got=lithium_log_copy(&cursor,(char*)copy+used,173,&lost);
        assert(got && !lost && got<=173);used+=got;
    }
    assert(used==want && !memcmp(copy,expected,want));
    assert(lithium_log_copy(&cursor,(char*)copy,173,&lost)==0 && !lost);
    assert(mask_calls==restore_calls);
}
static void check_screen(const unsigned char *in,size_t n) {
    unsigned char expected[40000];size_t used=0;
    for(size_t i=0;i<n;i++) {if(in[i]=='\n')expected[used++]='\r';expected[used++]=in[i];}
    assert(screen_used==used && !memcmp(screen,expected,used));
}
int main(void) {
    const unsigned sizes[]={255,256,257,8191};unsigned cases=0;
    unsigned char input[10000];
    /* Direct bulk writes exercise the actual 256-byte lock/IRQ boundaries. */
    for(unsigned s=0;s<4;s++)for(unsigned control=0;control<2;control++) {
        unsigned n=sizes[s];
        for(unsigned i=0;i<n;i++)input[i]=(unsigned char)('A'+i%23);
        if(control)for(unsigned i=0;i<n;i+=31)input[i]='\r';
        reset();lithium_log_bytes((const char*)input,n);
        assert(lock_calls==(n+255)/256 && mask_calls==lock_calls && max_irq_bytes<=256);
        check_log(input,n);cases++;
    }
    reset();lithium_log_bytes(NULL,0);assert(!mask_calls && !lithium_log_total);
    reset();memset(input,'K',257);force_lock_failure=true;
    lithium_log_bytes((const char*)input,257);
    assert(!loader_log_len && !lithium_log_total && lithium_log_contention_dropped_bytes==257);
    assert(lock_calls==128 && mask_calls==2 && restore_calls==2 && !loader_log_lock && daif==0x40);
    force_lock_failure=false;
    /* Exact-size nonterminated arrays expose strlen/truncation and CR/NUL loss. */
    const unsigned char controls[]={0,1,'\t','\r','\n',0x7f,0xff,'X'};
    for(unsigned idx=0;idx<2;idx++)for(unsigned s=0;s<4;s++)
    for(unsigned control=0;control<2;control++)for(unsigned disabled=0;disabled<2;disabled++) {
        unsigned n=sizes[s];for(unsigned i=0;i<n;i++)input[i]=control?controls[i%8]:(unsigned char)('A'+i%23);
        reset();cons_index=idx;disableConsoleOutput=disabled;_cnputs((char*)input,(int)n,false);
#if HAKIOS_LITHIUM_USB_LOG
        check_log(input,n);
        if(disabled) {assert(!screen_used && !lithium_screen_used);}
        else {check_screen(input,n<4096?n:4096);assert(lithium_screen_used==(n<4096?n:4096));}
        unsigned before=lock_calls;_cnputs(NULL,9,false);_cnputs((char*)input,0,false);_cnputs((char*)input,-1,false);
        assert(before==lock_calls);
#else
        assert(!lithium_log_total);if(disabled)assert(!screen_used);else check_screen(input,n);
#endif
        cases++;
    }
    /* printf staging forwards bounded chunks even without forced UART drain. */
    for(unsigned idx=0;idx<2;idx++)for(unsigned s=0;s<4;s++)for(unsigned control=0;control<2;control++)
    for(unsigned sync=0;sync<2;sync++)for(unsigned newline=0;newline<2;newline++)
    for(unsigned canblock=0;canblock<2;canblock++)for(unsigned disabled=0;disabled<2;disabled++) {
        unsigned n=sizes[s];for(unsigned i=0;i<n;i++)input[i]=control?controls[i%8]:(unsigned char)('A'+i%23);
        reset();cons_index=idx;disableConsoleOutput=disabled;serialmode=sync?SERIALMODE_SYNCDRAIN:0;
        struct console_printbuf_state state;console_printbuf_state_init(&state,newline,canblock);
        assert(!!(state.flags&CONS_PB_WRITE_NEWLINE)==!!newline && !!(state.flags&CONS_PB_CANBLOCK)==!!canblock);
        for(unsigned i=0;i<n;i++)console_printbuf_putc(input[i],&state);
        assert(state.total==(int)n);console_printbuf_clear(&state);assert(state.pos==0 && state.total==0);
        unsigned calls=lock_calls;size_t rendered=screen_used;console_printbuf_clear(&state);
        assert(calls==lock_calls && rendered==screen_used);
#if HAKIOS_LITHIUM_USB_LOG
        check_log(input,n);
        if(disabled)assert(!screen_used);else check_screen(input,n<4096?n:4096);
#else
        assert(!lithium_log_total);
        if(disabled)assert(!screen_used);
        else if(sync || (control && newline) || n<=255)check_screen(input,n);
        else check_screen(input,255); /* Legacy async truncation is a negative control. */
#endif
        cases++;
    }
#if HAKIOS_LITHIUM_USB_LOG
    /* Screen quota is shared across calls; debugger bypass never duplicates USB. */
    reset();memset(input,'Z',sizeof(input));_cnputs((char*)input,4095,false);
    _cnputc('Y',false);_cnputs((char*)input,257,false);
    assert(screen_used==4096 && lithium_screen_used==4096 && lithium_log_total==4353);
    kernel_debugger_entry_count=1;_cnputs("debug\n",6,false);
    assert(screen_used==4103 && poll_calls==7 && lithium_log_total==4359 && lithium_screen_used==4096);
    reset();cons_index=1;const unsigned char chars[]={'A','\r','\n',0,'\t',0xff};
    for(unsigned i=0;i<sizeof(chars);i++)_cnputc((char)chars[i],false);
    check_log(chars,sizeof(chars));check_screen(chars,sizeof(chars));
    reset();disableConsoleOutput=1;_cnputs("muted\r\n",7,false);
    check_log((const unsigned char*)"muted\r\n",7);assert(!screen_used);
#endif
    printf("PASS actual bulk/ring/console/drain USB=%d: %u cases, controls, 255/256/257/8191, quota, disabled single capture\n",
           HAKIOS_LITHIUM_USB_LOG,cases);
}
'''


def main():
    report_path = hakios.ROOT / 'reports/hakios-ios16-r9-checks.json'
    report = dict(passed=False, revision=9, started_at=datetime.now(timezone.utc).isoformat(),
                  actual_production_functions=False, sanitizers=['address', 'undefined'], checks=[],
                  hardware_verified=False, phone_operations_performed=False, qemu_used=False,
                  full_kernel_compiled=False,
                  scope='Extracted production functions with host SDK/IO/atomic/return-address stubs; no hardware or SMP proof')
    src = None
    try:
        spec = hakios.read(hakios.MANIFEST)
        hakios.policy(spec)
        hakios.resource_check()
        work = hakios.workspace(spec)
        hakios.check_common(work, hakios.read(work / 'ios-mix1-workspace.json'))
        port = hakios.ROOT / spec['port_path']
        src = Sources(work, port)
        for path in (Path(__file__), Path(r9.__file__), Path(hakios.__file__),
                     hakios.MANIFEST, hakios.ROOT / 'config/hakios-resources.json',
                     work / 'ios-mix1-workspace.json', hakios.ROOT / 'scripts/hakios_boot_observer.py'):
            src.read(path)
        report['inputs'] = hakios.inputs(spec)
        loader = src.select('xnuloader/loader.c', r9.loader_source, 'platform_cpu_mpidr(')
        storage = src.select('drivers/storage/hakios.ufs.c', r9.storage_source, 'case DKIOCISSOLIDSTATE:')
        gic = src.select('xnu/files/osfmk/arm64/gic_v3_irq.c', r9.gic_source,
                         'MSM8996 firmware does not provide reliable GICR_WAKER state.')
        returned = src.select('xnu/files/osfmk/arm64/loader_return.c', r9.return_source, 'lithium_log_bytes(')
        trace = src.select('xnu/files/osfmk/arm64/user_exception_trace.c', r9.trace_source, 'diagnostics/ipc-history.inc')
        inject = src.select('userland/src/inject.c', r9.inject_source, 'diagnostics/ls-ready-observer.inc')
        tree = 'xnu-src/xnu-8796.121.2/'
        console = src.select(tree + 'osfmk/console/serial_console.c', r9.console_source, 'lithium_screen_echo(')
        general = src.select(tree + 'osfmk/console/serial_general.c', r9.serial_general_source,
                             'bool chunk_long_messages = ')
        ipc = src.select(tree + 'osfmk/ipc/ipc_kmsg.c', r9.ipc_source, 'lithium_ipc_send_locked(port,')
        protos = src.read(work / tree / 'osfmk/console/serial_protos.h')
        cpu_inc = src.include('cpu-map.inc', src.selections['xnuloader/loader.c']['mode'] == 'workspace-r9')
        r11 = (work/'devices/lithium/diagnostics/r11.json').exists()
        ls_inc = src.include('ls-ready-observer.inc', src.selections['userland/src/inject.c']['mode'] == 'workspace-r9')
        ipc_inc = src.include('ipc-history.inc', src.selections['xnu/files/osfmk/arm64/user_exception_trace.c']['mode'] == 'workspace-r9')
        # These are integration assertions, in addition to executing the helpers.
        hakios.require('kernel_mpidr = platform_cpu_mpidr(PLAT,' in loader and
                       'boot_core = platform_cpu_index(plat, kernel_mpidr)' in loader and
                       'uint64_t mpidr = platform_cpu_mpidr(plat, core);' in loader and
                       'mpidr == this_mpidr' in loader and 'if (state == 1' in loader and
                       'boot=0x%llx resident=0x%llx' in loader and
                       'mpidr=0x%llx psci=%lld' in loader,
                       'Actual loader does not use the tested mapper at all three call sites')
        hakios.require(re.search(r'#if defined\(LITHIUM_ARMV8_0\)\s*/\* MSM8996 firmware.*?#else\s*'
                                 r'uint32_t waker = gic_read32\(gicr \+ GICR_WAKER\);.*?#endif', gic, re.S),
                       'MSM8996 GICR_WAKER workaround is not limited to lithium')
        hakios.require('chunk_long_messages = true;' in general and '|| true' not in general,
                       'Long-line chunking must not force UART synchronous drain')
        hakios.require('lithium_ipc_history_dump();' in trace and 'cfprefsd' in trace and
                       'static task_t found[10];' in trace, 'History dump/process selection is not wired into ustacks')
        hakios.require(re.search(r'ip_mq_lock\(port\);\s*#if defined\(LITHIUM_ARMV8_0\)\s*'
                                 r'extern void lithium_ipc_send_locked\([^;]+;\s*'
                                 r'lithium_ipc_send_locked\(port, hdr->msgh_id, hdr->msgh_bits\);', ipc),
                       'IPC capture must run at the actual locked send call site')
        hakios.require('if (strcmp(name, "lsd") == 0) lithium_ls_watch(cache, length);' in inject,
                       'LS observer must be restricted to lsd')
        folder = work / 'build/r9-host-checks'
        folder.mkdir(parents=True, exist_ok=True)
        report['compiler'] = subprocess.check_output(['clang', '--version'], text=True).splitlines()[0]
        compile_run(folder, 'cpu-map', cpu_harness(src, cpu_inc, loader), report)
        compile_run(folder, 'storage-ssd', storage_harness(src, storage), report)
        if not r11:
            compile_run(folder, 'ls-observer', ls_harness(src, ls_inc), report)
        else:
            report['superseded_ls_check']='R11 cumulative LS wrappers are checked by test-hakios-ios16-r11.py; R9 event budget no longer active'
        compile_run(folder, 'ipc-history', ipc_harness(src, ipc_inc), report)
        bulk = log_console_harness(src, returned, console, general, protos)
        for enabled in (0, 1):
            compile_run(folder, 'bulk-console-' + str(enabled), bulk, report,
                        ['-DHAKIOS_LITHIUM_USB_LOG=' + str(enabled)])
        src.stable()
        hakios.require(report['inputs'] == hakios.inputs(spec), 'Device layer changed during checks; rerun after synchronization')
        report.update(passed=True, actual_production_functions=True, asan_ubsan_passed=True, source_inputs_unchanged=True,
                      storage_transform_exact_two_handlers=True,
                      host_stub_replacements=['SDK declarations and UFS state/flush',
                          'LS original operation pointers and event sink; safe __builtin_return_address macro only',
                          'IPC task/thread/receiver, trylock atomics and guarded output/allocation',
                          'DAIF register intrinsics, ring atomics and physical console'],
                      limitations=['No concurrent scheduling or real SMP/interrupt timing proof',
                          'LS runtime hook installation and original Apple implementations are not executed',
                          'IPC records are send attempts, not delivery or historical RPC proof',
                          'Host log checks do not establish USB transport capture or physical screen behavior'])
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        if src is not None:
            report.update(evidence_sha256=src.files, source_selection=src.selections,
                          extracted_production_function_sha256=src.functions)
        report['finished_at'] = datetime.now(timezone.utc).isoformat()
        hakios.write(report_path, report)
    print('PASS R9 ASan/UBSan production host checks; ' + str(report_path))


if __name__ == '__main__':
    main()
