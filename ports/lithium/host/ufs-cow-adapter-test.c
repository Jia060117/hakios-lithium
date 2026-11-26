#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define __unused __attribute__((unused))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define bzero(p,n) memset(p,0,n)
#define UFS_BLOCK 4096u
#define BOUNCE_SIZE (1u<<20)
#define DMA_BOUNCE 0
#define KHEAP_DEFAULT 0
#define KHEAP_DATA_BUFFERS 1
#define Z_WAITOK 0
#define Z_ZERO 4
#define Z_NOWAIT 1
#define Z_NOPAGEWAIT 2
#define kSuccess 0
typedef int DTEntry;
static struct { uint64_t nblocks,first_block; uintptr_t dma; bool ready,failed; dev_t bdev; uint32_t secsize; } su;
static uint32_t heap_objects, read_calls;
static int fail_heap, fail_lower;
static void *heap_alloc(int heap,size_t bytes,int flags) {
    if(heap==KHEAP_DATA_BUFFERS)assert(flags==Z_NOPAGEWAIT);
    if(fail_heap)return NULL;
    void *p=calloc(1,bytes);if(p)heap_objects++;return p;
}
static void heap_free(int heap,void *p,size_t bytes) { (void)heap;(void)bytes;if(p){heap_objects--;free(p);} }
#define kheap_alloc heap_alloc
#define kheap_free heap_free
static uint8_t lower_image[2<<20], bounce[BOUNCE_SIZE], seed[32+4104*2];
static unsigned seed_size=sizeof(seed);
static int no_property;
static uint32_t OSReadLittleInt32(const void *p,uint32_t off) { const uint8_t *b=(const uint8_t*)p+off;return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
static uint64_t OSReadLittleInt64(const void *p,uint32_t off) { return OSReadLittleInt32(p,off)|(uint64_t)OSReadLittleInt32(p,off+4)<<32; }
static void put32(uint8_t *b,uint32_t n) { for(int i=0;i<4;i++)b[i]=(uint8_t)(n>>(8*i)); }
static int SecureDTGetProperty(DTEntry node,const char *key,const void **data,unsigned *size) {
    (void)node;assert(!strcmp(key,"ram-cow-seed"));if(no_property)return -1;
    *data=seed;*size=seed_size;return kSuccess;
}
static int ufs_read_blocks(uint64_t lba,uint32_t count) {
    read_calls++;if(fail_lower)return EIO;
    assert(lba>=su.first_block&&lba-su.first_block<512&&count<=256);
    assert(count<=512-(lba-su.first_block));
    memcpy(bounce,lower_image+(lba-su.first_block)*4096,count*4096);return 0;
}
#include "../native/ufs_gpt.h"
#include "../native/ufs_cow.h"
static struct lc_cow root_cow;
static bool cow_enabled;
#include "../native/ufs_cow_adapter.inc"
#ifdef TEST_DRIVER_GLUE
typedef char *caddr_t;
typedef struct test_buf { int64_t block;uint32_t count,flags,resid;int error,mapped,completed,map_error;char *data; } *buf_t;
#define B_READ 1
static int lithium_ufs_lock,held;
static void lck_mtx_lock(int *p) { (void)p;assert(!held);held=1; }
static void lck_mtx_unlock(int *p) { (void)p;assert(held);held=0; }
#define buf_count(b) ((b)->count)
#define buf_blkno(b) ((b)->block)
#define buf_flags(b) ((b)->flags)
#define buf_setresid(b,n) ((b)->resid=(n))
#define buf_seterror(b,n) ((b)->error=(n))
#define buf_biodone(b) ((b)->completed=1)
static int buf_map(buf_t b,caddr_t *p) { if(b->map_error)return 1;b->mapped=1;*p=b->data;return 0; }
static void buf_unmap(buf_t b) { assert(b->mapped);b->mapped=0; }
#include "driver-strategy.inc"
#endif
static void fixture(void) {
    ufs_cow_discard();assert(!heap_objects);
    seed_size=sizeof(seed);no_property=fail_heap=fail_lower=0;read_calls=0;
    su.first_block=98304;su.nblocks=512;su.dma=(uintptr_t)bounce;su.ready=true;su.bdev=123;
    su.secsize=512;su.failed=false;
    for(unsigned i=0;i<sizeof(lower_image);i++)lower_image[i]=(uint8_t)(i*13+(i>>12));
    memset(seed,0,sizeof(seed));memcpy(seed,"LCOWSE01",8);put32(seed+12,4);
    put32(seed+16,2);put32(seed+20,4104);put32(seed+24,1);
    for(unsigned i=0;i<2;i++) {
        uint8_t *p=seed+32+i*4104;put32(p,i*2);put32(p+4,lg_crc(lower_image+i*8192,4096,0));
        memset(p+8,0x70+i,4096);
    }
}
int main(void) {
    fixture();assert(!ufs_cow_init(1)&&cow_enabled&&root_cow.used==2&&heap_objects==3);
    assert(lithium_ufs_root_is_writable(123)&&!lithium_ufs_root_is_writable(124));
    uint8_t out[12300];uint32_t done;
    assert(!lc_read(&root_cow,0,12288,out,&done)&&done==12288);
    for(unsigned i=0;i<4096;i++){assert(out[i]==0x70&&out[i+8192]==0x71);assert(out[i+4096]==lower_image[i+4096]);}
    puts("PASS: actual seed adapter accepts measured CRCs; overlay/lower reads and exact root device gating");
    fixture();seed[0]^=1;assert(ufs_cow_init(1)==EINVAL&&!heap_objects&&!cow_enabled);
    fixture();seed_size--;assert(ufs_cow_init(1)==EINVAL&&!heap_objects);
    fixture();put32(seed+16,16385);assert(ufs_cow_init(1)==EINVAL&&!heap_objects);
    fixture();put32(seed+28,1);assert(ufs_cow_init(1)==EINVAL&&!heap_objects);
    fixture();seed[12]=8;assert(ufs_cow_init(1)==EINVAL&&!heap_objects);
    fixture();no_property=1;assert(ufs_cow_init(1)==EINVAL&&!heap_objects);
    puts("PASS: malformed magic/length/count/version/volume and missing property rejected");
    fixture();put32(seed+32+4104,0);assert(ufs_cow_init(1)==EINVAL&&!heap_objects&&!cow_enabled);
    fixture();put32(seed+32+4104,4194304);assert(ufs_cow_init(1)==EINVAL&&!heap_objects&&!cow_enabled);
    fixture();seed[32+4104+4]^=1;assert(ufs_cow_init(1)==EIO&&!heap_objects&&!cow_enabled);
    fixture();fail_lower=1;assert(ufs_cow_init(1)==EIO&&!heap_objects&&!cow_enabled);
    fixture();fail_heap=1;assert(ufs_cow_init(1)==ENOMEM&&!heap_objects&&!cow_enabled);
    puts("PASS: duplicate/out-of-volume sectors, CRC mismatch, lower I/O and allocation failures discard partial seed");
    fixture();uint8_t *large=malloc(BOUNCE_SIZE+4096);assert(large);
    assert(!ufs_lower_read(NULL,511,BOUNCE_SIZE+2048,large)&&read_calls==2);
    assert(!memcmp(large,lower_image+511,BOUNCE_SIZE+2048));
    assert(ufs_lower_read(NULL,UINT64_MAX,1,large)==EINVAL);
    free(large);ufs_cow_discard();assert(!heap_objects);
    puts("PASS: actual lower adapter batches unaligned >1MiB read and rejects integer overflow");
#ifdef TEST_DRIVER_GLUE
    fixture();struct test_buf b={.block=0,.count=4096,.data=(char*)out};
    lithium_ufs_strategy(&b);assert(b.error==EROFS&&b.resid==4096&&b.completed&&!b.mapped&&!held);
    assert(!ufs_cow_init(1));memset(out,0xa6,5000);
    b=(struct test_buf){.block=1,.count=5000,.data=(char*)out};lithium_ufs_strategy(&b);
    assert(!b.error&&!b.resid&&b.completed&&!b.mapped&&!held);
    memset(out,0,5000);b=(struct test_buf){.block=1,.count=5000,.flags=B_READ,.data=(char*)out};lithium_ufs_strategy(&b);
    assert(!b.error&&!b.resid);for(unsigned i=0;i<5000;i++)assert(out[i]==0xa6);
    b=(struct test_buf){.block=-1,.count=4096,.data=(char*)out};lithium_ufs_strategy(&b);assert(b.error==EINVAL&&b.resid==4096);
    b=(struct test_buf){.block=(LC_ROOT_BYTES/512)-1,.count=513,.data=(char*)out};lithium_ufs_strategy(&b);assert(b.error==EINVAL&&b.resid==513);
    b=(struct test_buf){.block=0,.count=4096,.map_error=1,.data=(char*)out};lithium_ufs_strategy(&b);assert(b.error==EIO&&b.resid==4096);
    root_cow.limit=root_cow.used;
    b=(struct test_buf){.block=23,.count=1024,.data=(char*)out};lithium_ufs_strategy(&b);assert(b.error==ENOSPC&&b.resid==512&&b.completed&&!b.mapped&&!held);
    su.ready=false;b=(struct test_buf){.block=0,.count=1,.data=(char*)out};lithium_ufs_strategy(&b);assert(b.error==ENXIO&&b.resid==1);
    su.ready=true;su.failed=true;b=(struct test_buf){.block=0,.count=1,.data=(char*)out};lithium_ufs_strategy(&b);assert(b.error==EIO&&b.resid==1);
    ufs_cow_discard();assert(!heap_objects);
    puts("PASS: actual kernel strategy read/write, residuals, readonly/ready/fault/mapping gates, overflow and 16GiB bound");
#endif
    return 0;
}
