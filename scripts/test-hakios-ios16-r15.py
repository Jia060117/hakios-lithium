"""Exercise the actual Run A finalizer with VFS mocks, ASan and UBSan."""
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
import hakios
import hakios_r15 as r15

SDK=r'''
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <time.h>
#define MAXPATHLEN 1024
#define Z_WAITOK 1
#define Z_ZERO 2
#define MNT_RDONLY 1
#define MNT_WAIT 2
#define LK_NOWAIT 1
#define VDIR 1
#define VREG 2
#define VLNK 3
#define VNODE_LOOKUP_NOFOLLOW 1
#define VNODE_LOOKUP_NOCROSSMOUNT 2
#define UIO_SYSSPACE 1
#define UIO_READ 1
#define DKIOCSYNCHRONIZECACHE 1
#define FWRITE 1
struct mount {unsigned mnt_flag;struct node *mnt_devvp;char f_fstypename[16],f_mntfromname[64];};
typedef struct mount *mount_t;
struct node {const char *name;unsigned type;mount_t mp;struct node **children;unsigned n;int refs;};
typedef struct node *vnode_t;
typedef void *vfs_context_t;
struct vnode_attr {uint64_t va_data_size;struct timespec va_modify_time;bool supported;};
#define VATTR_INIT(a) memset(a,0,sizeof(*(a)))
#define VATTR_WANTED(a,f) ((void)(a))
#define VATTR_IS_SUPPORTED(a,f) ((a)->supported)
#pragma pack(push,4)
struct dirent {uint32_t d_ino;uint16_t d_reclen;uint8_t d_type,d_namlen;char d_name[256];};
#pragma pack(pop)
typedef struct {off_t offset;uintptr_t base;size_t size,resid;} *uio_t;
static struct mount data_mount={.f_fstypename="hfs",.f_mntfromname="/dev/ufs_darwin_data"},other_mount;
static struct node dev={.type=VREG,.mp=&data_mount},root;
static unsigned sync_calls,flush_calls,unbusy_calls,bootcpu=1,cpus=1;
static int lookup_error,busy_error,sync_error,flush_error,read_error,malformed,allocation_error,attr_error;
static uint64_t tick=1000,host_mpidr=0x80000001;static bool attr_supported=true;
static char output[262144];static size_t output_used;
static void log_capture(const char *fmt,...) {
 va_list ap;va_start(ap,fmt);int n=vsnprintf(output+output_used,sizeof(output)-output_used,fmt,ap);va_end(ap);
 assert(n>=0 && (size_t)n<sizeof(output)-output_used);output_used+=(size_t)n;
}
#define printf log_capture
static uint64_t mach_absolute_time(void){return tick++;}
static void absolutetime_to_nanoseconds(uint64_t a,uint64_t *b){*b=a*1000;}
static int cpu_number(void){return 0;}
static int ml_get_max_cpu_number(void){return (int)cpus-1;}
#define __builtin_arm_rsr64(n) host_mpidr
static bool PE_parse_boot_argn(const char *n,void *p,size_t size){assert(size==4);*(unsigned*)p=!strcmp(n,"cpus")?cpus:bootcpu;return true;}
static vfs_context_t vfs_context_kernel(void){return (void*)1;}
static char *strnstr(const char *s,const char *find,size_t n){size_t len=strlen(find),limit=strnlen(s,n);for(size_t i=0;i+len<=limit;i++)if(!memcmp(s+i,find,len))return (char*)s+i;return NULL;}
static void *kalloc_data(size_t s,int flags){(void)flags;return allocation_error?NULL:calloc(1,s);}
static void kfree_data(void *p,size_t s){(void)s;free(p);}
static int vnode_lookup(const char *p,int flags,vnode_t *vp,vfs_context_t ctx) {
 assert(flags==VNODE_LOOKUP_NOFOLLOW && ctx==(void*)1);
 if(lookup_error)return lookup_error;
 if(strcmp(p,"/private/var") && strcmp(p,"/private/var/mobile/Library"))return ENOENT;
 root.refs++;*vp=&root;return 0;
}
static int vnode_lookupat(const char *p,int flags,vnode_t *vp,vfs_context_t ctx,vnode_t parent) {
 assert(flags==(VNODE_LOOKUP_NOFOLLOW|VNODE_LOOKUP_NOCROSSMOUNT)&&ctx==(void*)1&&parent->refs>0);
 for(unsigned i=0;i<parent->n;i++)if(!strcmp(parent->children[i]->name,p)){*vp=parent->children[i];(*vp)->refs++;return 0;}
 return ENOENT;
}
static mount_t vnode_mount(vnode_t v){assert(v->refs>0);return v->mp;}
static unsigned vnode_vtype(vnode_t v){assert(v->refs>0);return v->type;}
static struct mount *vfs_statfs(mount_t m){return m;}
static int vfs_busy(mount_t m,int f){assert(m==&data_mount&&f==LK_NOWAIT);return busy_error;}
static void vfs_unbusy(mount_t m){assert(m==&data_mount);unbusy_calls++;}
static int vnode_get(vnode_t v){v->refs++;return 0;}
static void vnode_put(vnode_t v){assert(v->refs>0);v->refs--;}
static int vnode_getattr(vnode_t v,struct vnode_attr *a,vfs_context_t c) {
 assert(v->refs>0 && c==(void*)1);a->va_data_size=123456;a->va_modify_time.tv_sec=1794942458;a->va_modify_time.tv_nsec=123;
 a->supported=attr_supported;return attr_error;
}
static uio_t uio_create(int n,off_t o,int s,int r){assert(n==1&&s==1&&r==1);uio_t u=calloc(1,sizeof(*u));u->offset=o;return u;}
static void uio_reset(uio_t u,off_t o,int s,int r){assert(s==1&&r==1);memset(u,0,sizeof(*u));u->offset=o;}
static int uio_addiov(uio_t u,uintptr_t b,size_t s){u->base=b;u->size=u->resid=s;return 0;}
static void uio_free(uio_t u){free(u);}
static size_t uio_resid(uio_t u){return u->resid;}
static off_t uio_offset(uio_t u){return u->offset;}
static int VNOP_READDIR(vnode_t v,uio_t u,int f,int *eof,int *count,vfs_context_t c) {
 assert(v->refs>0&&f==0&&c==(void*)1);if(read_error)return read_error;
 size_t used=0;*count=0;
 for(unsigned i=(unsigned)u->offset;i<v->n;i++) {
  const char *name=v->children[i]->name;size_t n=strlen(name);assert(n<256);
  size_t len=(offsetof(struct dirent,d_name)+n+1+3)&~(size_t)3;
  if(used+len>u->size)break;
  struct dirent *e=(void*)(u->base+used);memset(e,0,len);e->d_ino=i+1;e->d_reclen=(uint16_t)len;e->d_namlen=(uint8_t)n;
  memcpy(e->d_name,name,n);used+=len;u->offset=i+1;(*count)++;
 }
 *eof=u->offset==(off_t)v->n;u->resid-=used;
 if(malformed && used)((struct dirent*)u->base)->d_reclen=65535;
 return 0;
}
static int VFS_SYNC(mount_t m,int w,vfs_context_t c){assert(m==&data_mount&&w==MNT_WAIT&&c==(void*)1);sync_calls++;return sync_error;}
static int VNOP_IOCTL(vnode_t d,int cmd,void *p,int flags,vfs_context_t c) {
 assert(d==&dev&&d->refs>0&&cmd==DKIOCSYNCHRONIZECACHE&&!p&&flags==FWRITE&&c==(void*)1);flush_calls++;return flush_error;
}
'''
TEST=r'''
static void reset(void) {
 assert(root.refs==0&&dev.refs==0);
 output_used=0;output[0]=0;sync_calls=flush_calls=unbusy_calls=0;
 lookup_error=busy_error=sync_error=flush_error=read_error=malformed=allocation_error=attr_error=0;
 data_mount.mnt_flag=0;data_mount.mnt_devvp=&dev;cpus=bootcpu=1;host_mpidr=0x80000001;attr_supported=true;
}
int main(void) {
 struct node file={"com.apple.LaunchServices-1.csstore",VREG,&data_mount,NULL,0,0};
 struct node tmp={"com.apple.LaunchServices-1.csstore.tmp",VREG,&data_mount,NULL,0,0};
 struct node ordinary={"unrelated",VREG,&data_mount,NULL,0,0};
 struct node symlink={"link",VLNK,&data_mount,NULL,0,0};
 struct node external={"outside",VDIR,&other_mount,NULL,0,0};
 struct node child={"data.db",VREG,&data_mount,NULL,0,0};struct node *sub[]={&child};
 struct node related={"com.apple.lsd",VDIR,&data_mount,sub,1,0};
 struct node *children[]={&file,&tmp,&ordinary,&symlink,&external,&related};
 root=(struct node){"Library",VDIR,&data_mount,children,6,0};reset();
 lithium_run_a_cpu();assert(strstr(output,"affinity=0x1")&&strstr(output,"match=1"));
 reset();bootcpu=2;host_mpidr=0x80000100;lithium_run_a_cpu();assert(strstr(output,"match=0"));
 reset();lithium_lsdb_finalize();
 assert(sync_calls==1&&flush_calls==1&&unbusy_calls==1);
 assert(strstr(output,"files=3")&&strstr(output,"missing_roots=4")&&strstr(output,"truncated=0")&&strstr(output,"skipped=2"));
 assert(strstr(output,"size=123456")&&strstr(output,"mtime_sec=1794942458")&&strstr(output,"data.db"));
 assert(strstr(output,"error=0 flush_error=0")&&strstr(output,"persistence=UNVERIFIED"));
 for(unsigned i=0;i<6;i++)assert(children[i]->refs==0);assert(child.refs==0);
 reset();sync_error=EIO;lithium_lsdb_finalize();assert(strstr(output,"end error=5 flush_error=0"));
 reset();flush_error=EIO;lithium_lsdb_finalize();assert(strstr(output,"end error=0 flush_error=5"));
 reset();lookup_error=EIO;lithium_lsdb_finalize();assert(!sync_calls&&!flush_calls&&strstr(output,"phase=lookup"));
 reset();busy_error=EBUSY;lithium_lsdb_finalize();assert(!sync_calls&&!unbusy_calls);
 reset();data_mount.mnt_flag=MNT_RDONLY;lithium_lsdb_finalize();assert(!sync_calls&&strstr(output,"phase=mount_validation"));
 reset();read_error=EIO;lithium_lsdb_finalize();assert(sync_calls==1&&strstr(output,"op=readdir error=5"));
 reset();malformed=1;lithium_lsdb_finalize();assert(sync_calls==1&&strstr(output,"op=dirent"));
 reset();allocation_error=1;lithium_lsdb_finalize();assert(sync_calls==1&&strstr(output,"inventory end error=12"));
 reset();attr_supported=false;lithium_lsdb_finalize();assert(strstr(output,"size_valid=0")&&strstr(output,"mtime_valid=0"));
 reset();attr_error=EIO;lithium_lsdb_finalize();assert(strstr(output,"op=getattr error=5"));
 reset();struct la_scan *s=calloc(1,sizeof(*s));s->ctx=(void*)1;s->mp=&data_mount;s->started=la_us();
 s->entries=LA_ENTRIES;la_root(s,"/private/var/mobile/Library",1);assert(s->truncated==1&&root.refs==0);
 s->entries=0;s->files=LA_FILES;la_file(s,&file,"/test");assert(s->truncated==2);
 struct dirent e={.d_reclen=12,.d_namlen=1,.d_name="x"};assert(la_entry(&e,12));
 e.d_namlen=255;assert(!la_entry(&e,12));e.d_reclen=0;assert(!la_entry(&e,12));assert(!la_entry(&e,1));
 la_path("file",1,"/test/line\nbreak\\");assert(strstr(output,"\\x0a")&&strstr(output,"\\x5c"));free(s);
 reset();puts("PASS production Run A: fast/slow core, live LSDB inventory, no-follow/cross-mount, bounds and malformed entries, attributes, sync/flush errors and reference cleanup");
}
'''

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
    port=hakios.ROOT/spec['port_path'];contract=hakios.read(port/'diagnostics/r15.json')
    for name,digest in contract['after_sha256'].items():
        hakios.require(hakios.sha(work/name)==digest,'Reviewed Run A source differs: '+name)
    for name,c in contract['port_changes'].items():
        hakios.require(hakios.sha(port/name)==c['after'],'Reviewed three-hour observer differs: '+name)
    folder=work/'build/r15-host-checks';folder.mkdir(parents=True,exist_ok=True)
    helper=port/'diagnostics/lsdb-finalize.inc';src=folder/'finalize.c';src.write_text(SDK+helper.read_text()+TEST)
    subprocess.run(['clang','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(folder/'finalize')],check=True)
    result=subprocess.run([str(folder/'finalize')],check=True,capture_output=True,text=True);print(result.stdout,end='')
    # Reconstruct exact consumed R14 common files, including the return path.
    with tempfile.TemporaryDirectory(prefix='r15-preservation-') as t:
        base=Path(t)
        for name in contract['after_sha256']:
            p=base/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((work/name).read_bytes())
        subprocess.run(['git','apply','--reverse',str(port/'patches/0015-lithium-run-a-finalize.patch')],cwd=base,check=True)
        for name,digest in contract['before_sha256'].items():
            hakios.require(hakios.sha(base/name)==digest,'R14 preserved body mismatch: '+name)
    ret=(work/'xnu/files/osfmk/arm64/loader_return.c').read_text();start=ret.index('loader_return_timer(__unused')
    seq=ret[start:ret.index('__startup_func',start)]
    hakios.require(seq.index('if (!as_panic)')<seq.index('lithium_lsdb_finalize();')<seq.index('hakios_var_save_on_return();')<seq.index('hakios_adreno_stop();')<seq.index('lithium_usb_log_drain(target'), 'Finalization must precede save/quiesce/drain on ordinary normal-return thread')
    cpu_map=(port/'diagnostics/cpu-map.inc').read_text()
    hakios.require('{ 0x0, 0x1, 0x100, 0x101 }' in cpu_map,'Loader numbering changed')
    prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r14/contract.json')['cmdline']
    cmd=r15.command_line(prior);r15.command_budget(cmd,'2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty')
    hakios.require(set(prior.split())-set(cmd.split())=={'bootcpu=2','cpus=3','loader_return_after=600'},'Unexpected removed boot argument')
    hakios.require(set(cmd.split())-set(prior.split())=={'bootcpu=1','cpus=1','loader_return_after=10800'},'Unexpected added boot argument')
    for bad in (prior,cmd.replace('bootcpu=1','bootcpu=2'),cmd.replace('cpus=1','cpus=3')):
        try:r15.command_budget(bad,'version')
        except RuntimeError:pass
        else:raise RuntimeError('Old slow-core configuration accepted')
    files=[Path(__file__),Path(r15.__file__),helper,port/'diagnostics/r15.json',port/'patches/0015-lithium-run-a-finalize.patch',
        port/'diagnostics/cpu-map.inc',port/'diagnostics/service-progress.inc',port/'diagnostics/springboard-observer.inc']+[work/n for n in contract['after_sha256']]
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r15-checks.json',dict(passed=True,revision=15,inputs=hakios.inputs(spec),
        actual_production_functions=True,sanitizers=['ASan','UBSan'],stdout=result.stdout,cmdline=cmd,hardware_verified=False,
        error_paths_checked=True,numbering_preserved=True,phone_operations=False,evidence_sha256={str(p):hakios.sha(p) for p in files}))

if __name__=='__main__':main()
