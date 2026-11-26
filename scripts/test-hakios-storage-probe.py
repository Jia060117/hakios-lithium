#!/usr/bin/env python3
"""Exercise the actual diagnostic and loader summary together using host fixtures.

Syscalls are intercepted to check mount arguments and inject results. These tests
do not execute XNU, read the phone, mount HFS, or validate iOS compatibility.
"""
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import hakios

HARNESS = r'''
#include <setjmp.h>
static jmp_buf done;
static char kernel_log[8192];
static size_t kernel_len, read_offset[5];
static int scenario, mount_called, unmount_called, file_reads;
int probe_errno;
static const char *file_data[]={"version fixture", "dyld fixture", "launchd fixture"};
static const char *file_paths[]={"/mnt/System/Library/CoreServices/SystemVersion.plist", "/mnt/usr/lib/dyld", "/mnt/sbin/launchd"};
static long fake_sys6(long n,long a,long b,long c,long d,long e,long f);
/* ACTUAL_PROBE */
long mini_syscall(long n,long a,long b,long c,long d,long e,long f) {
    long result=fake_sys6(n,a,b,c,d,e,f);
    if(result<0) {probe_errno=(int)-result;return -1;}
    return result;
}
static long fake_sys6(long n,long a,long b,long c,long d,long e,long f) {
    (void)e;(void)f;
    switch(n) {
    case SYS_open:
        if (!strcmp((char *)a,"/dev/console")) {assert(b==2);return 100;}
        assert(b==0 && c==0);
        if (!strcmp((char *)a,"/dev/rufs_userdata")) return 0;
        for (int i=0;i<3;i++) if (!strcmp((char *)a,file_paths[i])) return i+1;
        assert(!"unexpected open path");return -2;
    case SYS_read:
        assert(a>=0 && a<=3);
        if(a==0) {assert(c==8192);memset((void *)b,0,c);return c;}
        else {
            if(scenario==4 && a==2) return -5;
            const char *data=file_data[a-1];size_t remaining=strlen(data)-read_offset[a];
            size_t bytes=remaining<(size_t)c?remaining:(size_t)c;
            if(scenario==6 && bytes>4) bytes=4;
            memcpy((void *)b,data+read_offset[a],bytes);read_offset[a]+=bytes;
            if(bytes) {file_reads++;if(scenario==3 && a==2) ((char *)b)[0]^=1;}
            return bytes;
        }
    case SYS_close:return 0;
    case SYS_write:
        assert(a==100 && kernel_len+(size_t)c<sizeof(kernel_log));
        memcpy(kernel_log+kernel_len,(void *)b,c);kernel_len+=c;kernel_log[kernel_len]=0;return c;
    case SYS_mount:
        assert(!strcmp((char *)a,"hfs") && !strcmp((char *)b,"/mnt") && c==1);
        assert(!strcmp(((struct hfs_args *)d)->fspec,"/dev/ufs_userdata"));
        mount_called++;return scenario==1?-15:0;
    case SYS_statfs64:
        assert(!strcmp((char *)a,"/mnt"));memset((void *)b,0,2168);
        if(scenario!=2) {u32 readonly_flag=1;memcpy((char *)b+64,&readonly_flag,4);}
        memcpy((char *)b+72,"hfs",4);return 0;
    case SYS_unmount:assert(!strcmp((char *)a,"/mnt") && b==0);unmount_called++;return scenario==5?-16:0;
    case SYS_select:longjmp(done,1);
    default:assert(!"unexpected syscall");return -78;
    }
}
static void test_case(int which) {
    scenario=which;kernel_len=0;mount_called=unmount_called=file_reads=0;
    memset(read_offset,0,sizeof(read_offset));
    if(setjmp(done)==0) probe_main(0,NULL,NULL);
    assert(mount_called==1);
    assert(strstr(kernel_log,"LITHIUM P2 HEADER read=8192 match=1\n"));
    const char *suffix="--- loader_return_after expired ---\n";
    strcat(kernel_log,suffix);kernel_len+=strlen(suffix);
    lithium_return_screen(kernel_log,kernel_len);
    assert(strstr(output,"LITHIUM P2 START 18H17 READONLY UFS/HFS"));
    assert(strstr(output,"KERNEL TIMED RETURN OBSERVED"));
}
int main(void) {
    void *mapped=mmap((void *)0x415ff000,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapped==(void *)0x415ff000);
    uint64_t *ufs=(void *)0x415ff300;ufs[0]=0x465532505854494cULL;ufs[1]=6;
    test_case(0);
    assert(strstr(output,"MOUNT rc=0 readonly=1") && strstr(output,"UNMOUNT rc=0"));
    assert(strstr(output,"VERSION bytes=15 match=1") && strstr(output,"DYLD bytes=12 match=1") &&
        strstr(output,"LAUNCHD bytes=15 match=1") && strstr(output,"RESULT PASS read_only_mount_and_files"));
    assert(unmount_called==1 && file_reads==3);
    puts("PASS actual probe uses read-only block mount and retains actual START/file/PASS lines");
    test_case(1);
    assert(strstr(output,"MOUNT rc=-15") && strstr(output,"RESULT FAIL stage=2"));
    assert(unmount_called==0 && file_reads==0);
    puts("PASS injected ENOTBLK retains actual mount error and FAIL stage 2 without file reads");
    test_case(2);
    assert(strstr(output,"MOUNT READONLY CHECK rc=-1") && strstr(output,"RESULT FAIL stage=3"));
    assert(file_reads==0 && !strstr(output,"RESULT PASS"));
    puts("PASS writable filesystem flag prevents file checks and retains FAIL stage 3");
    test_case(3);
    assert(strstr(output,"DYLD bytes=12 match=0") && strstr(output,"RESULT FAIL stage=4"));
    assert(unmount_called==1 && !strstr(output,"RESULT PASS"));
    puts("PASS corrupt file fixture is rejected and successful unmount does not imply PASS");
    test_case(4);
    assert(strstr(output,"DYLD bytes=0 match=0") && strstr(output,"RESULT FAIL stage=4"));
    assert(unmount_called==1 && !strstr(output,"RESULT PASS"));
    assert(sys6(SYS_read,2,(long)buffer,sizeof(buffer),0,0,0)==-5);
    puts("PASS actual syscall adapter converts libmini errno after failed read without hashing failed data");
    test_case(5);
    assert(strstr(output,"UNMOUNT rc=-16") && strstr(output,"RESULT FAIL stage=4") && !strstr(output,"RESULT PASS"));
    puts("PASS libmini unmount failure retains the original errno and blocks PASS");
    test_case(6);
    assert(strstr(output,"VERSION bytes=15 match=1") && strstr(output,"DYLD bytes=12 match=1") &&
        strstr(output,"LAUNCHD bytes=15 match=1") && strstr(output,"RESULT PASS read_only_mount_and_files"));
    assert(unmount_called==1 && file_reads>3);
    puts("PASS repeated short reads through actual syscall adapter preserve all three hashes and EOF");
    assert(munmap(mapped,4096)==0);return 0;
}
'''

def fnv(data):
    h=0xcbf29ce484222325
    for b in data:h=((h^b)*0x100000001b3)&((1<<64)-1)
    return h

def main():
    spec=hakios.read(hakios.MANIFEST)
    work=hakios.verify_workspace(spec)
    probe=hakios.ROOT/spec['port_path']/'tests/storage_probe.c'
    text=probe.read_text()
    # Keep the actual C adapter. Only the upstream assembly entry is intercepted;
    # host fixtures must now exercise its -1/errno boundary too (R19).
    assert 'mini_syscall(n,a,b,c,d,e,f)' in text and '__asm__' not in text
    text=text.replace('int main(', 'int probe_main(').replace('extern int errno;', 'extern int probe_errno;').replace('-errno', '-probe_errno')
    summary_spec=importlib.util.spec_from_file_location('fault_summary',Path(__file__).with_name('test-hakios-fault-summary.py'))
    summary=importlib.util.module_from_spec(summary_spec)
    summary_spec.loader.exec_module(summary)
    prefix=summary.HARNESS.split('/* ACTUAL_FUNCTIONS */')[0]
    loader=work/'xnuloader/loader.c'
    loader_text=loader.read_text()
    start=loader_text.index('static const char *\nlithium_last_line(')
    end=loader_text.index('\n#endif',loader_text.index('static void\nlithium_return_screen(',start))
    actual=loader_text[start:end]
    # Tie the test's VBLK expectation to the actual complete upstream driver/VFS.
    ufs=(work/'drivers/storage/hakios.ufs.c').read_text()
    assert re.search(r'"ufs_%s".*?devfs_make_node\([^\n]+DEVFS_BLOCK',ufs,re.S)
    assert re.search(r'"rufs_%s".*?devfs_make_node\([^\n]+DEVFS_CHAR',ufs,re.S)
    vfs=(work/'xnu-src/xnu-8796.121.2/bsd/vfs/vfs_syscalls.c').read_text()
    assert 'if (devvp->v_type != VBLK) {\n\t\t\t\terror = ENOTBLK;' in vfs
    with tempfile.TemporaryDirectory(prefix='hakios-probe-contract-') as temporary:
        root=Path(temporary)
        values={'HEADER':bytes(8192),'VERSION':b'version fixture','DYLD':b'dyld fixture','LAUNCHD':b'launchd fixture'}
        expected=[]
        for label,data in values.items():
            expected.extend([f'#define EXPECTED_{label}_BYTES {len(data)}UL',f'#define EXPECTED_{label}_FNV 0x{fnv(data):x}UL'])
        (root/'storage_expected.h').write_text('\n'.join(expected)+'\n')
        source=root/'probe-contract.c'
        source.write_text(prefix+actual+HARNESS.replace('/* ACTUAL_PROBE */',text))
        binary=root/'probe-contract'
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(source),'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
    checks=result.stdout.splitlines()
    assert len(checks)==7 and all(line.startswith('PASS ') for line in checks)
    hakios.write(hakios.ROOT/'reports/hakios-storage-probe-contract-tests.json',dict(passed=True,checks=checks,
        probe_source_sha256=hakios.sha(probe),loader_sha256=hakios.sha(loader),test_source_sha256=hakios.sha(Path(__file__)),
        actual_diagnostic_and_summary_exercised=True,syscalls_intercepted=True,synthetic_file_fixtures=True,
        actual_syscall_adapter_exercised=True,libmini_assembly_entry_intercepted=True,
        sanitizer='address,undefined',hardware_verified=False,hfs_mounted=False,ios14_processes_executed=False,qemu_used=False))
    print(result.stdout,end='')

if __name__=='__main__':main()
