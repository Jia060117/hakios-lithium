#!/usr/bin/env python3
"""Execute actual pinned/patched ufs_strategy with mapped/unmapped buffer IO stubs."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import hakios

HARNESS=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef char *caddr_t;
struct buffer {unsigned dev,flags,count,resid;uint64_t blk;void *data;int error;};
typedef struct buffer *buf_t;
#define B_READ 1
#define minor(d) (d)
static int ufs_ready=1;static unsigned ufs_parts=2;static uint32_t ufs_bsize[2]={4096,4096};
static unsigned maps,unmaps,reads,done,data_calls;static int map_error,read_error,is_data;
static unsigned char mapped_pages[8192];static uint64_t observed_offset,observed_len;
static unsigned buf_device(buf_t b){return b->dev;}
static unsigned buf_flags(buf_t b){return b->flags;}
static uint64_t buf_blkno(buf_t b){return b->blk;}
static unsigned buf_count(buf_t b){return b->count;}
static void *buf_dataptr(buf_t b){return b->data;}
static void buf_seterror(buf_t b,int e){b->error=e;}
static void buf_setresid(buf_t b,unsigned n){b->resid=n;}
static void buf_biodone(buf_t b){(void)b;done++;}
static bool ufs_is_data(unsigned d){(void)d;return is_data;}
static void ufs_data_strategy(buf_t b){(void)b;data_calls++;}
static uint64_t ufs_part_bytes(unsigned part){assert(part<2);return 16384;}
static int buf_map(buf_t b,caddr_t *p){maps++;if(map_error)return map_error;*p=b->data?b->data:(char*)mapped_pages;return 0;}
static int buf_unmap(buf_t b){(void)b;unmaps++;return 0;}
static int ufs_read(unsigned part,uint64_t off,uint8_t *dst,uint64_t len){
 assert(part<2);reads++;observed_offset=off;observed_len=len;
 if(read_error)return read_error;
 if(!dst)return EFAULT; /* Original passes no mapping: reject instead of crashing host. */
 memset(dst,0x5a,len);return 0;
}
@FUNCTION@
static void reset(void){maps=unmaps=reads=done=data_calls=0;map_error=read_error=is_data=0;ufs_ready=1;memset(mapped_pages,0,sizeof(mapped_pages));}
int main(void){struct buffer b;
 reset();b=(struct buffer){.dev=0,.flags=B_READ,.count=4096};ufs_strategy(&b);
#if !defined(LITHIUM_ARMV8_0)
 assert(b.error==EFAULT&&maps==0&&reads==1&&b.resid==4096&&done==1);puts("Original clustered read passed NULL buf_dataptr to IO");return 0;
#else
 assert(!b.error&&maps==1&&unmaps==1&&reads==1&&done==1&&b.resid==0&&mapped_pages[0]==0x5a);
 reset();unsigned char regular[4096];b=(struct buffer){.dev=1,.flags=B_READ,.count=4096,.data=regular};ufs_strategy(&b);assert(!b.error&&maps==1&&unmaps==1&&regular[0]==0x5a);
 reset();map_error=ENOMEM;b=(struct buffer){.flags=B_READ,.count=4096};ufs_strategy(&b);assert(b.error==ENOMEM&&b.resid==4096&&maps==1&&reads==0&&unmaps==0&&done==1);
 reset();read_error=EIO;b=(struct buffer){.flags=B_READ,.count=4096};ufs_strategy(&b);assert(b.error==EIO&&b.resid==4096&&maps==1&&unmaps==1&&done==1);
 reset();b=(struct buffer){.flags=B_READ,.count=8192,.blk=3};ufs_strategy(&b);assert(!b.error&&b.resid==4096&&observed_offset==12288&&observed_len==4096&&maps==unmaps&&done==1);
 reset();b=(struct buffer){.flags=B_READ,.count=4096,.blk=4};ufs_strategy(&b);assert(!b.error&&b.resid==4096&&maps==0&&reads==0&&done==1);
 reset();b=(struct buffer){.flags=B_READ,.count=4096,.blk=5};ufs_strategy(&b);assert(b.error==EINVAL&&maps==0&&reads==0&&done==1);
 reset();b=(struct buffer){.flags=B_READ,.count=4096,.dev=2};ufs_strategy(&b);assert(b.error==ENXIO&&maps==0&&reads==0&&done==1);
 reset();ufs_ready=0;b=(struct buffer){.flags=B_READ,.count=4096};ufs_strategy(&b);assert(b.error==ENXIO&&maps==0&&reads==0&&done==1);
 reset();b=(struct buffer){.count=4096};ufs_strategy(&b);assert(b.error==EROFS&&maps==0&&reads==0&&done==1);
 reset();is_data=1;b=(struct buffer){.flags=B_READ,.count=4096};ufs_strategy(&b);assert(data_calls==1&&maps==0&&reads==0&&done==0);
 puts("PASS 11 patched UFS mapping, read error, bounds, readonly and data-route cases");return 0;
#endif
}
'''

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    name='drivers/storage/hakios.ufs.c'
    patched=(work/name).read_text()
    original=subprocess.check_output(['git','-C',str(source),'show',hakios.PIN+':'+name]).decode()
    results={}
    with tempfile.TemporaryDirectory() as tmp:
        for label,code,defines in [('original',original,[]),('patched',patched,['-DLITHIUM_ARMV8_0'])]:
            start=code.index('static void\nufs_strategy(');end=code.index('\nstatic int\nufs_ioctl(',start)
            test=Path(tmp)/(label+'.c');test.write_text(HARNESS.replace('@FUNCTION@',code[start:end]))
            binary=Path(tmp)/label
            subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined',*defines,str(test),'-o',str(binary)],check=True)
            results[label]=subprocess.check_output([str(binary)],text=True).strip()
    assert 'NULL buf_dataptr' in results['original'] and 'PASS 11' in results['patched']
    hakios.write(hakios.ROOT/'reports/hakios-ufs-buffer-map-tests.json',dict(passed=True,patched_cases=11,
        original_unmapped_destination_reproduced_by_stub=True,asan_ubsan_passed=True,
        source_sha256=hakios.sha(work/name),patch_sha256=hakios.sha(hakios.ROOT/spec['port_path']/'patches/0006-msm8996-ufs-buffer-map.patch'),
        test_source_sha256=hakios.sha(Path(__file__)),results=results,physical_device_used=False,qemu_used=False,
        scope='Actual ufs_strategy, with host buf_map/IO/error stubs; runtime mapping remains for MIX verification'))
    print(results['original']);print(results['patched'])

if __name__=='__main__':main()
