#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../native/ufs_cow.h"

#define TEST_BYTES (256u * LC_BLOCK)
static uint8_t original[TEST_BYTES], oracle[TEST_BYTES];
static struct lc_entry table[1024];
static uint32_t calls, allocs, frees;
static int fail_read, fail_alloc;
static void *alloc_block(void *p) { (void)p; if(fail_alloc)return NULL;allocs++;return malloc(LC_BLOCK); }
static void free_block(void *p,void *q) { (void)p;frees++;free(q); }
static int lower(void *p,uint64_t off,uint32_t len,void *data) {
    (void)p;calls++;if(fail_read)return EIO;
    if(off>TEST_BYTES || len>TEST_BYTES-off)return EINVAL;
    memcpy(data,original+off,len);return 0;
}
static struct lc_cow c;
static void reset(uint32_t limit) {
    for(unsigned i=0;i<1024;i++) if(table[i].key)free_block(NULL,table[i].data);
    memset(table,0,sizeof(table));
    c=(struct lc_cow){.table=table,.slots=1024,.limit=limit,.bytes=TEST_BYTES,
        .alloc=alloc_block,.release=free_block,.read_lower=lower};
    fail_read=fail_alloc=0;calls=0;memcpy(oracle,original,TEST_BYTES);
}
static uint32_t rng=0x71d8ae31;
static uint32_t random32(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
int main(void) {
    for(uint32_t i=0;i<TEST_BYTES;i++)original[i]=(uint8_t)(i*37u+(i>>9));
    uint8_t *out=malloc(TEST_BYTES),*input=malloc(TEST_BYTES);assert(out&&input);
    for(uint32_t i=0;i<TEST_BYTES;i++)input[i]=(uint8_t)(i*19u+0x73);
    uint32_t done;
    reset(256);
    assert(!lc_read(&c,0,TEST_BYTES,out,&done)&&done==TEST_BYTES&&calls==1&&!memcmp(out,original,TEST_BYTES));
    assert(!lc_write(&c,TEST_BYTES,0,input,&done)&&!done);
    assert(lc_write(&c,TEST_BYTES,1,input,&done)==EINVAL&&!done);
    assert(lc_read(&c,UINT64_MAX,1,out,&done)==EINVAL&&!done);
    assert(lc_write(&c,UINT64_MAX-1,UINT32_MAX,input,&done)==EINVAL&&!done);
    puts("PASS: coalesced lower read, zero length, end and integer bounds");
    for(uint32_t i=0;i<12000;i++) {
        uint32_t len=1+random32()%16384,off=random32()%(TEST_BYTES-len+1);
        if(i&1) {
            for(uint32_t j=0;j<len;j++)input[j]=(uint8_t)random32();
            assert(!lc_write(&c,off,len,input,&done)&&done==len);
            memcpy(oracle+off,input,len);
        } else {
            assert(!lc_read(&c,off,len,out,&done)&&done==len&&!memcmp(out,oracle+off,len));
        }
    }
    assert(!lc_read(&c,0,TEST_BYTES,out,&done)&&done==TEST_BYTES&&!memcmp(out,oracle,TEST_BYTES));
    for(uint32_t i=0;i<TEST_BYTES;i++)assert(original[i]==(uint8_t)(i*37u+(i>>9)));
    assert(c.used<=c.limit);
    puts("PASS: 12000 deterministic overlapping/nonaligned operations against byte-array oracle; lower image unchanged");
    reset(1);
    assert(lc_write(&c,LC_BLOCK-1,3,input,&done)==ENOSPC&&done==1&&c.used==1);
    assert(!lc_read(&c,LC_BLOCK-1,3,out,&done)&&done==3);
    assert(out[0]==input[0]&&out[1]==original[LC_BLOCK]&&out[2]==original[LC_BLOCK+1]);
    assert(!lc_write(&c,0,LC_BLOCK,input,&done)&&done==LC_BLOCK&&c.used==1);
    fail_read=1;calls=0;
    assert(!lc_read(&c,0,LC_BLOCK,out,&done)&&done==LC_BLOCK&&calls==0);
    assert(lc_read(&c,0,LC_BLOCK+1,out,&done)==EIO&&done==LC_BLOCK);
    puts("PASS: capacity limit, exact partial progress, overwrite without growth, cached read independent of lower device");
    reset(256);fail_alloc=1;
    assert(lc_write(&c,0,1,input,&done)==ENOMEM&&!done&&!c.used);
    fail_alloc=0;fail_read=1;
    uint32_t before_alloc=allocs,before_free=frees;
    assert(lc_write(&c,1,1,input,&done)==EIO&&!done&&!c.used&&allocs==before_alloc+1&&frees==before_free+1);
    calls=0;
    assert(!lc_write(&c,0,LC_BLOCK,input,&done)&&done==LC_BLOCK&&calls==0&&c.used==1);
    puts("PASS: allocation failure and partial-block read failure publish no dirty entry; full-block write needs no lower read");
    reset(256);c.slots=3;
    assert(lc_write(&c,0,1,input,&done)==EINVAL&&!done);
    c.slots=1024;c.bytes=LC_ROOT_BYTES+LC_BLOCK;
    assert(lc_read(&c,0,1,out,&done)==EINVAL&&!done);
    reset(256);
    /* The same deployed 16GiB bound accepts its last block and rejects beyond. */
    c.bytes=LC_ROOT_BYTES;
    assert(!lc_write(&c,LC_ROOT_BYTES-LC_BLOCK,LC_BLOCK,input,&done)&&done==LC_BLOCK);
    assert(!lc_read(&c,LC_ROOT_BYTES-LC_BLOCK,LC_BLOCK,out,&done)&&!memcmp(out,input,LC_BLOCK));
    assert(lc_write(&c,LC_ROOT_BYTES-1,2,input,&done)==EINVAL&&!done);
    reset(256);assert(allocs==frees);
    free(out);free(input);
    puts("PASS: malformed configuration, actual 16GiB last block, overrun and allocation accounting");
    return 0;
}
