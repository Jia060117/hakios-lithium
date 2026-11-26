#!/usr/bin/env python3
"""WSL-only offline preparation. No device calls; no legacy kernel/entrypoints.

Extract complete journal_create/size_up_tbuffer/journal_is_clean/checksum/header
definitions from the actual integrated HakiOS HFS source. Replace only kernel
allocation, locking and device IO with host stubs, then validate the payload.
The auxiliary boot image retains lithium's exact stock Linux kernel for two
fixed block writes; HakiOS remains the sole project runtime/acceptance baseline.
"""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/hakios-journal-maintenance'
SRC = Path('/home/jia/ios-mix1-build/hakios-lithium/xnu-src/xnu-8796.121.2/bsd/hfs')
READBACK = Path('/mnt/e/IOS-MIX1-artifacts/ios14-d20ap/userdata-readback-20261114-163722.img')
STOCK = ROOT / 'artifacts/stock/V9.5.2.0.NAHCNFA'
MAINT = ROOT / 'maintenance/hakios-lithium'


def sha(b):
    return hashlib.sha256(b).hexdigest()


def function(text, name):
    match = re.search(r'\n((?:static\s+)?(?:unsigned int|void|int|journal\s*\*)\s*\n' + re.escape(name) + r'\s*\()', text)
    assert match, name
    start = match.start(1)
    pos = text.index('{', match.end())
    # These functions have no braces in strings/comments; lexical scan still
    # handles them to preserve exact upstream function bytes.
    depth = 0
    token = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\{|\}', re.S)
    for m in token.finditer(text, pos):
        if m[0] == '{':
            depth += 1
        elif m[0] == '}':
            depth -= 1
            if depth == 0:
                return text[start:m.end()]
    raise ValueError(name)


SCAFFOLD = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
struct vnode {int unused;}; struct mount {int unused;}; struct vfs_context {int unused;};
typedef struct journal {
 struct vnode *jdev,*fsdev; off_t jdev_offset,active_start; const char *jdev_name;
 struct mount *fsmount; void (*flush)(void*); void *flush_arg;
 int flags,tbuffer_size,old_start_lock,jlock,flock,trim_lock;
 void *header_buf; size_t header_buf_size; journal_header *jhdr;
 int flushing,asyncIO,flush_aborted,writing_header; void *async_trim; uint32_t sequence_num;
} journal;
#define MAX_JOURNAL_SIZE 0x80000000U
#define DEFAULT_TRANSACTION_BUFFER_SIZE (128*1024)
#define MAX_TRANSACTION_BUFFER_SIZE (3072*1024)
#define JOURNAL_OPTION_FLAGS_MASK 0
#define JOURNAL_NEED_SWAP 1
#define FALSE 0
#define SWAP32(x) __builtin_bswap32(x)
#define SWAP64(x) __builtin_bswap64(x)
#define DKIOCGETBLOCKSIZE 1
#define hfs_assert assert
#define hfs_malloc_type(t) calloc(1,sizeof(t))
#define hfs_free_type(p,t) free(p)
#define hfs_malloc_data(n) calloc(1,n)
#define hfs_free_data(p,n) free(p)
#define lck_mtx_init(...) ((void)0)
#define lck_rw_init(...) ((void)0)
static const char *vnode_getname_printable(struct vnode*p){(void)p;return "host-journal";}
static void vnode_putname_printable(const char*p){(void)p;}
static struct vfs_context *vfs_context_kernel(void){return NULL;}
static int host_ioctl(struct vnode*p,int cmd,void*v,int flags,struct vfs_context*c){(void)p;(void)flags;(void)c;assert(cmd==1);*(uint32_t*)v=4096;return 0;}
#define VNOP_IOCTL host_ioctl
static int sysctlbyname(const char*n,void*p,size_t*l,void*a,size_t z){(void)a;(void)z;assert(!strcmp(n,"hw.memsize")&&*l==8);*(uint64_t*)p=6ULL*1024*1024*1024;return 0;}
static void get_io_info(struct vnode*v,size_t b,journal*j,struct vfs_context*c){(void)v;(void)b;(void)j;(void)c;}
static unsigned char disk[4096];
static size_t read_journal_header(journal*j,void*p,size_t n){(void)j;assert(n==4096);memcpy(p,disk,n);return n;}
int def_tbuffer_size=0;
'''

WRITE_STUB = r'''
static int write_journal_header(journal*j,int updating,uint32_t seq){
 assert(updating==1 && j->jhdr->jhdr_size==4096);
 j->jhdr->sequence_num=seq;j->jhdr->checksum=0;
 j->jhdr->checksum=calc_checksum((char*)j->jhdr,JOURNAL_HEADER_CKSUM_SIZE);
 memcpy(disk,j->header_buf,4096);return 0;
}
'''

TEST = r'''
static void sum(void){journal_header*h=(journal_header*)disk;h->checksum=0;h->checksum=calc_checksum((char*)h,JOURNAL_HEADER_CKSUM_SIZE);}
int main(int argc,char**argv){
 assert(argc==2);assert(sizeof(journal_header)==48&&JOURNAL_HEADER_CKSUM_SIZE==44&&sizeof(block_info)==16);
 struct vnode dev={0};
 assert(journal_is_clean(&dev,532480,8388608,&dev,4096)==EINVAL);
 assert(journal_create(&dev,532480,1000,&dev,4096,0,0,NULL,NULL,NULL)==NULL);
 srandom(0x1848796);journal*j=journal_create(&dev,532480,8388608,&dev,4096,0,0,NULL,NULL,NULL);assert(j);
 journal_header*h=(journal_header*)disk;
 assert(h->start==4096&&h->end==4096&&h->size==8388608&&h->jhdr_size==4096&&h->blhdr_size==12288);
 assert(journal_is_clean(&dev,532480,8388608,&dev,4096)==0);
 FILE*f=fopen(argv[1],"wb");assert(f&&fwrite(disk,1,4096,f)==4096&&fclose(f)==0);
 unsigned char saved[4096];memcpy(saved,disk,4096);
 disk[8]^=1;assert(journal_is_clean(&dev,532480,8388608,&dev,4096)==EINVAL);
 memcpy(disk,saved,4096);h->end=8192;sum();assert(journal_is_clean(&dev,532480,8388608,&dev,4096)==EBUSY);
 memcpy(disk,saved,4096);assert(journal_is_clean(&dev,532480,8388608,&dev,512)==EINVAL);
 journal view={.jhdr=h};h->checksum=0;swap_journal_header(&view);sum();
 h->checksum=SWAP32(h->checksum);assert(journal_is_clean(&dev,532480,8388608,&dev,4096)==0);
 free(j->header_buf);free(j);puts("PASS 7 upstream journal create/clean cases");return 0;
}
'''


def entries(b):
    p = 0
    while p < len(b):
        assert b[p:p+6] == b'070701'
        h = [int(b[p+6+i*8:p+14+i*8], 16) for i in range(13)]
        n = b[p+110:p+110+h[11]-1].decode()
        p = (p+110+h[11]+3) & ~3
        d = b[p:p+h[6]]
        p = (p+h[6]+3) & ~3
        if n == 'TRAILER!!!':
            return
        yield n, (h[1], d)


def archive(items):
    ordered = sorted(items.items(), key=lambda x: (x[0].count('/'), 0 if x[1][0]&0o170000 == 0o040000 else 1, x[0]))
    seen = set()
    for name, (mode, _) in ordered:
        parent = str(Path(name).parent)
        assert parent == '.' or parent in seen
        if mode&0o170000 == 0o040000:
            seen.add(name)
    out = bytearray()
    for i, (n, (mode, d)) in enumerate(ordered+[('TRAILER!!!', (0, b''))]):
        name = n.encode()+b'\0'
        h = [i+1, mode, 0, 0, 1, 0, len(d), 0, 0, 0, 0, len(name), 0]
        out += b'070701'+b''.join(f'{v:08x}'.encode() for v in h)+name
        out += bytes(-len(out)%4)
        out += d
        out += bytes(-len(out)%4)
    return bytes(out)


def main():
    assert os.name == 'posix' and str(ROOT) == '/mnt/f/IOS-MIX1'
    policy = (ROOT/'AGENTS.md').read_text()
    assert '63fc2b4633bac3af8e57dadab9f5a8d4cb940438' in policy
    OUT.mkdir(parents=True, exist_ok=True)
    s = (SRC/'hfs_journal.c').read_text()
    h = (SRC/'hfs_journal.h').read_text()
    definitions = h[h.index('typedef struct journal_header'):h.index('/*\n * In memory structure about the journal.')]
    bi = h[h.index('typedef struct _blk_info'):h.index('typedef struct block_list_header')]
    names = ['calc_checksum', 'swap_journal_header', 'size_up_tbuffer', 'journal_create', 'journal_is_clean']
    funcs = {n: function(s,n) for n in names}
    extracted = '#include <stdint.h>\n#include <sys/types.h>\n#include <stddef.h>\n' + definitions + bi + SCAFFOLD + funcs['calc_checksum'] + WRITE_STUB
    extracted += '\n'.join(funcs[n] for n in names[1:]) + TEST
    host = OUT/'upstream-journal-host.c'
    host.write_text(extracted)
    subprocess.run(['clang','-O1','-g','-Wno-format','-fsanitize=address,undefined',str(host),'-o',str(OUT/'upstream-journal-host')],check=True)
    test = subprocess.run([str(OUT/'upstream-journal-host'),str(OUT/'journal-header.bin')],check=True,capture_output=True,text=True)
    (OUT/'upstream-host-tests.txt').write_text(test.stdout)
    initialized = (OUT/'journal-header.bin').read_bytes()
    assert len(initialized) == 4096 and initialized[48:] == bytes(4096-48)
    with READBACK.open('rb') as f:
        volume = f.read(8192)
        assert volume[1024:1028] == b'HX\x00\x05'
        attrs, version, jibblock = struct.unpack_from('>III', volume, 1028)
        block, total = struct.unpack_from('>II', volume, 1064)
        assert (attrs, version, jibblock, block, total) == (0x80002100,0x31302e30,129,4096,4194304)
        f.seek(528384)
        jib = f.read(4096)
        assert struct.unpack_from('>I',jib)[0] == 5
        offset, size = struct.unpack_from('>QQ',jib,36)
        assert (offset,size) == (532480,8388608)
        f.seek(offset)
        assert f.read(size) == bytes(size)
    newjib = bytearray(jib)
    struct.pack_into('>I',newjib,0,1)
    parts = {'expected_volume':volume,'expected_jib':jib,'initialized_jib':bytes(newjib),'initialized_header':initialized}
    header = ''
    for name, data in parts.items():
        (OUT/(name+'.bin')).write_bytes(data)
        header += f'static const unsigned char {name}[{len(data)}]={{'+','.join(str(v) for v in data)+'};\n'
    (OUT/'journal-payload.h').write_text(header)
    subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined','-I',str(OUT),str(MAINT/'journal-maintenance-host-test.c'),'-o',str(OUT/'maintainer-host-test')],check=True)
    io_test = subprocess.run([str(OUT/'maintainer-host-test')],check=True,capture_output=True,text=True)
    (OUT/'maintainer-host-tests.txt').write_text(io_test.stdout)
    subprocess.run(['clang','--target=aarch64-linux-gnu','-O2','-Wall','-Wextra','-Werror',
                    '-nostdlib','-static','-fno-stack-protector','-fno-builtin','-fuse-ld=lld','-Wl,-e,_start',
                    '-I',str(OUT),str(MAINT/'journal-maintenance.c'),'-o',str(OUT/'maintainer')],check=True)
    original = dict(entries(gzip.decompress((STOCK/'recovery/ramdisk.cpio.gz').read_bytes())))
    wanted = ['init','sbin/adbd','sepolicy','file_contexts.bin','property_contexts','seapp_contexts','service_contexts']
    items = {n:original[n] for n in wanted if n in original}
    assert all(n in items for n in ['init','sbin/adbd','sepolicy','file_contexts.bin','property_contexts'])
    for n in ['dev','proc','sys','sbin','system','system/bin','data']:
        items[n] = (0o40755,b'')
    items['sbin/ueventd'] = (0o120777,b'../init')
    items['system/bin/sh'] = (0o100755,(OUT/'maintainer').read_bytes())
    rc = (MAINT/'journal-init.rc').read_bytes()
    assert not any(x.strip().startswith((b'import ',b'mount_all ',b'service recovery ',b'service vold ')) for x in rc.splitlines())
    items['init.rc'] = (0o100644,rc)
    items['ueventd.rc'] = (0o100644,b'/dev/null 0666 root root\n/dev/zero 0666 root root\n/dev/random 0666 root root\n/dev/urandom 0666 root root\n/dev/ptmx 0666 root root\n')
    items['default.prop'] = (0o100644,b'ro.secure=0\nro.adb.secure=0\nro.debuggable=1\nro.product.name=lithium\nro.product.device=lithium\nro.product.model=MIX1JournalMaintenance\nro.product.manufacturer=Xiaomi\nro.mix1.journal.maintenance=1\n')
    key = Path('/mnt/c/Users/Jia33/.android/adbkey.pub')
    assert key.is_file()
    items['adb_keys'] = (0o100644,key.read_bytes())
    cpio = archive(items)
    assert dict(entries(cpio)) == items
    rd = gzip.compress(cpio,compresslevel=9,mtime=0)
    boot = (STOCK/'boot/boot.img').read_bytes()
    assert sha(boot) == '2c070c67b07000a02de42fa05a6bcd93b7c32b3770d3fa9e495f321626278f44'
    ks,ka,rs,ra,ss,sa,tags,page = struct.unpack_from('<8I',boot,8)
    assert ss == 0
    kernel = boot[page:page+ks]
    assert kernel == (STOCK/'recovery/kernel-dtb.gz').read_bytes()
    bh = bytearray(boot[:page])
    struct.pack_into('<I',bh,16,len(rd))
    cmdline = boot[64:576].split(b'\0')[0]+boot[608:1632].split(b'\0')[0]
    args = [x for x in cmdline.decode().split() if not x.startswith('androidboot.selinux=')]
    args += ['androidboot.selinux=permissive','mix1_hakios_journal=1']
    cmdline = ' '.join(args).encode()
    assert len(cmdline) < 1536
    bh[64:576] = cmdline[:512].ljust(512,b'\0')
    bh[608:1632] = cmdline[512:].ljust(1024,b'\0')
    digest = hashlib.sha1()
    for part in [kernel,rd,b'']:
        digest.update(part);digest.update(struct.pack('<I',len(part)))
    bh[576:608] = digest.digest()+bytes(12)
    output = bytes(bh)+kernel+bytes(-len(kernel)%page)+rd+bytes(-len(rd)%page)
    image = OUT/'lithium-hakios-journal-maintenance.img'
    image.write_bytes(output)
    spec = importlib.util.spec_from_file_location('stock',ROOT/'scripts/extract-stock.py')
    stock = importlib.util.module_from_spec(spec);spec.loader.exec_module(stock)
    checked,_ = stock.unpack_boot(image,OUT/'roundtrip')
    assert checked['android_sha1_id_valid']
    assert (OUT/'roundtrip/kernel-dtb.gz').read_bytes() == kernel
    assert (OUT/'roundtrip/ramdisk.cpio.gz').read_bytes() == rd
    report = dict(purpose='Initialize existing empty HFSX journal for HakiOS P2; auxiliary stock Linux maintenance only',
        active_kernel='complete HakiOS 8796.121.2', stock_kernel_unchanged=True,
        image_path=image.relative_to(ROOT).as_posix(),image_sha256=sha(output),bytes=len(output),
        host_cases_passed=7,bounded_writer_cases_passed=11,host_asan_ubsan_passed=True,physical_write_performed=False,device_boot_issued=False,
        source_functions={n:sha(funcs[n].encode()) for n in names},
        integrated_source_sha256={n:sha((SRC/n).read_bytes()) for n in ['hfs_journal.c','hfs_journal.h','hfs_vfsutils.c']},
        source_dependency_commit='31d5ec238dfab485e78a9a30b8b92d13e8914f03',
        write_order=[dict(offset=532480,bytes=4096,purpose='valid empty journal header'),dict(offset=528384,bytes=4096,purpose='clear only NeedInit bit')],
        payload_sha256={n:sha(b) for n,b in parts.items()},
        evidence_sha256={p.relative_to(ROOT).as_posix():sha(p.read_bytes()) for p in [Path(__file__),MAINT/'journal-maintenance.c',MAINT/'journal-maintenance-host-test.c',MAINT/'journal-init.rc',host,OUT/'journal-payload.h',OUT/'maintainer',OUT/'upstream-host-tests.txt',OUT/'maintainer-host-tests.txt']},
        journal_size=8388608,journal_all_zero_before=True,volume_header_unchanged=True,
        block_mounts=[],services=['ueventd','adbd'],writes_bounded_to_two_blocks=True,
        qemu_used=False,legacy_7195_used=False,hfs_mount_verified=False)
    (OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    (ROOT/'reports/hakios-journal-maintenance-ready.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['image_path','image_sha256','host_cases_passed','bytes','stock_kernel_unchanged']},indent=2))


if __name__ == '__main__':
    main()
