/* Host shims for the actual pinned HFS journal-init functions. No phone I/O. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
typedef char uuid_string_t[37];
#include "hfs_format.h"
#define ARM64_BOARD_CONFIG_LITHIUM 1
#define HFS_READ_ONLY 1
#define HFS_WRITEABLE_MEDIA 2
#define MNT_ROOTFS 1
#define HFSFSMNT_EXTENDED_ARGS 1
#define NOCRED 0
#define kRootDirID 2
#define KERN_SUCCESS 0
#define JOURNAL_RESET 1
#define JOURNAL_NEED_SWAP 2
#define MAX_JOURNAL_SIZE (1024*1024*1024)
#define DKIOCGETBLOCKSIZE 1
#define HFS_PRI_SECTOR(x) (1024/(x))
#define HFS_PRI_OFFSET(x) (1024%(x))
#define HFS_PHYSBLK_ROUNDDOWN(x,y) ((x)/(y)*(y))
#define SWAP_BE32(x) __builtin_bswap32((uint32_t)(x))
#define SWAP_BE64(x) __builtin_bswap64((uint64_t)(x))
#define SWAP32(x) __builtin_bswap32((uint32_t)(x))
typedef int kauth_cred_t;
typedef int64_t daddr64_t;
typedef struct { uint64_t hfsPlusIOPosOffset; uint32_t blockSize,vcbAtrb,vcbJinfoBlock; } ExtendedVCB;
struct vnode { int n; };
struct mount { int flags; };
struct cat_attr { int unused; };
struct cat_fork { off_t cf_size; HFSPlusExtentDescriptor cf_extents[8]; };
struct hfs_mount_args { int flags,journal_flags,journal_tbuffer_size,journal_disable; };
struct buf { void *data; int released; };
typedef struct hfsmount {
    int hfs_flags; struct mount *hfs_mp; struct vnode *hfs_devvp,*jvp;
    void *jnl; uint32_t hfs_logical_block_size,hfs_physical_block_size,hfs_log_per_phys;
    uint64_t hfs_logical_bytes,jnl_start,jnl_size;
    uint32_t hfs_jnlinfoblkid,hfs_jnlfileid; ExtendedVCB vcb;
} hfsmount;
#define HFSTOVCB(p) (&(p)->vcb)
static int vfs_flags(struct mount *p) { return p->flags; }
static const char *vnode_getname_printable(struct vnode *p) { (void)p; return "fixture"; }
static void vnode_putname_printable(const char *p) { (void)p; }
static int vfs_context_kernel(void) { return 0; }
static int writes,creates,opens,reads,io_error,missing_info,missing_journal,journal_read_error;
static uint32_t sector_size=512;
static JournalInfoBlock info;
static struct buf info_buf;
static struct cat_fork info_fork,journal_fork;
static unsigned char journal_bytes[4096];
static int ioctl_fixture(void *data) { *(uint32_t*)data=sector_size; return 0; }
#define VNOP_IOCTL(v,c,d,f,x) ioctl_fixture(d)
static int buf_meta_bread(struct vnode *v,daddr64_t block,int size,int cred,struct buf **out) {
    (void)v;(void)block;(void)size;(void)cred;reads++;
    info_buf=(struct buf){&info,0};*out=&info_buf;return io_error;
}
static void *buf_dataptr(struct buf *b) { return b->data; }
static void buf_brelse(struct buf *b) { assert(!b->released);b->released=1; }
static void buf_bwrite(struct buf *b) { writes++;buf_brelse(b); }
static int GetFileInfo(ExtendedVCB *v,int id,const char *name,struct cat_attr *a,struct cat_fork *f) {
    (void)v;(void)id;(void)a;
    if (!strcmp(name,".journal_info_block")) { *f=info_fork;return missing_info?0:16; }
    *f=journal_fork;return missing_journal?0:17;
}
static void *journal_create() { creates++;return NULL; }
static void *journal_open() { opens++;return NULL; }
static void *open_journal_dev() { opens++;return NULL; }
static int hfs_get_platform_serial_number() { return -1; }
static void journal_trim_set_callback() {}
static void hfs_trim_callback() {}
static void hfs_sync_metadata() {}

/* The generator inserts Apple's journal_header, checksum and is_clean code. */
@@JOURNAL_HEADER@@
typedef struct journal {
    void *header_buf; size_t header_buf_size; journal_header *jhdr;
    struct vnode *jdev,*fsdev; off_t jdev_offset; int flags;
} journal;
#define hfs_malloc(n) malloc(n)
#define hfs_free(p,n) free(p)
static void get_io_info() {}
static unsigned read_journal_header(journal *j,journal_header *h,unsigned n) {
    (void)j;reads++;if(journal_read_error)return 0;memcpy(h,journal_bytes,n);return n;
}
static void swap_journal_header(journal *j) {
    journal_header *h=j->jhdr;
    h->magic=SWAP32(h->magic);h->endian=SWAP32(h->endian);
    h->start=SWAP_BE64(h->start);h->end=SWAP_BE64(h->end);h->size=SWAP_BE64(h->size);
    h->blhdr_size=SWAP32(h->blhdr_size);h->checksum=SWAP32(h->checksum);
    h->jhdr_size=SWAP32(h->jhdr_size);h->sequence_num=SWAP32(h->sequence_num);
}
@@CHECKSUM@@
@@IS_CLEAN@@
@@HELPER@@
@@EARLY@@
@@LATE@@

static HFSPlusVolumeHeader vh;
static struct mount mount_obj;
static struct vnode device;
static hfsmount fs;
static void reset(void) {
    writes=creates=opens=reads=io_error=missing_info=missing_journal=journal_read_error=0;
    memset(&info,0,sizeof(info));memset(&vh,0,sizeof(vh));memset(&fs,0,sizeof(fs));
    memset(&info_fork,0,sizeof(info_fork));memset(&journal_fork,0,sizeof(journal_fork));
    memset(journal_bytes,0,sizeof(journal_bytes));
    mount_obj.flags=MNT_ROOTFS;fs.hfs_flags=HFS_READ_ONLY;fs.hfs_mp=&mount_obj;
    fs.hfs_devvp=&device;fs.hfs_logical_block_size=sector_size;
    fs.hfs_physical_block_size=4096;fs.hfs_log_per_phys=8;fs.hfs_logical_bytes=250710192128ULL;
    vh.attributes=SWAP_BE32(0x80002100);vh.blockSize=SWAP_BE32(4096);
    vh.totalBlocks=SWAP_BE32(4194304);vh.journalInfoBlock=SWAP_BE32(129);
    info.flags=SWAP_BE32(5);info.offset=SWAP_BE64(0x82000);info.size=SWAP_BE64(0x800000);
    fs.vcb.blockSize=4096;fs.vcb.vcbAtrb=0x80002100;
    info_fork.cf_size=4096;info_fork.cf_extents[0]=(HFSPlusExtentDescriptor){129,1};
    journal_fork.cf_size=0x800000;journal_fork.cf_extents[0]=(HFSPlusExtentDescriptor){130,2048};
}
static void initialized(int dirty,int corrupt,int swap) {
    info.flags=SWAP_BE32(1);
    journal_header *h=(journal_header*)journal_bytes;
    *h=(journal_header){.magic=JOURNAL_HEADER_MAGIC,.endian=ENDIAN_MAGIC,.start=512,
        .end=dirty?1024:512,.size=0x800000,.blhdr_size=512,.jhdr_size=512};
    if(swap) { journal j={.jhdr=h};swap_journal_header(&j); }
    uint32_t sum=calc_checksum((char*)h,JOURNAL_HEADER_CKSUM_SIZE);
    h->checksum=swap?SWAP32(sum):sum;
    if(corrupt)h->checksum^=1;
}
static int cases;
static void check(int late,int expected) {
    HFSPlusVolumeHeader before=vh;JournalInfoBlock jib_before=info;
    int ret=late?hfs_late_journal_init(&fs,&vh,NULL):
        hfs_early_journal_init(&fs,&vh,NULL,0,0,NULL,0);
    assert((expected==0 && ret==0)||(expected!=0 && ret!=0));
    assert(!writes && !creates && !opens && fs.jnl==NULL);
    assert(!memcmp(&vh,&before,sizeof(vh)) && !memcmp(&info,&jib_before,sizeof(info)));
    if(!ret)assert(fs.jnl_start==130 && fs.jnl_size==0x800000);
    cases++;
}
int main(void) {
    for(int late=0;late<=1;late++) {
        reset();check(late,0);
        reset();vh.attributes=SWAP_BE32(0x80002000);check(late,1);
        reset();vh.attributes=SWAP_BE32(0x80006100);check(late,1);
        reset();vh.attributes=SWAP_BE32(0x80002900);check(late,1);
        reset();info.flags=SWAP_BE32(7);check(late,1);
        reset();info.flags=SWAP_BE32(0x85);check(late,1);
        reset();info.flags=SWAP_BE32(4);check(late,1);
        reset();info.offset=SWAP_BE64(0x81000);check(late,1);
        reset();info.offset=SWAP_BE64(0x82001);check(late,1);
        reset();info.offset=SWAP_BE64(0x400000000ULL);check(late,1);
        reset();info.size=SWAP_BE64(UINT64_MAX);check(late,1);
        reset();info.size=SWAP_BE64(4096);check(late,1);
        reset();fs.hfs_logical_bytes=4096;check(late,1);
        reset();io_error=EIO;check(late,1);
        reset();initialized(0,0,0);check(late,0);
        reset();initialized(1,0,0);check(late,1);
        reset();initialized(0,1,0);check(late,1);
        reset();initialized(0,0,1);check(late,0);
        reset();initialized(1,0,1);check(late,1);
        reset();initialized(0,0,0);journal_read_error=1;check(late,1);
        reset();info.flags=SWAP_BE32(1);check(late,1);
    }
    reset();missing_info=1;check(1,1);
    reset();missing_journal=1;check(1,1);
    reset();info_fork.cf_extents[0].startBlock++;check(1,1);
    reset();journal_fork.cf_extents[0].startBlock++;check(1,1);
    reset();journal_fork.cf_extents[0].blockCount++;check(1,1);
    reset();info_fork.cf_size++;check(1,1);
    reset();info_fork.cf_extents[0].blockCount++;check(1,1);
    reset();journal_fork.cf_size++;check(1,1);
    /* Ordinary writable-media root still follows the existing path. */
    reset();fs.hfs_flags|=HFS_WRITEABLE_MEDIA;
    assert(hfs_late_journal_init(&fs,&vh,NULL)!=0 && creates==1);cases++;
    printf("HFS_READONLY_CASES=%d\n",cases);
    return 0;
}
