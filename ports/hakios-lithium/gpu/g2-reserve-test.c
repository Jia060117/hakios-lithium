/* SPDX-License-Identifier: BSD-2-Clause
 * Native synthetic EFI services + ACTUAL Mu UEFI types, no device calls.
 */
#include "g2-reserve-mu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static unsigned checks,cases;
#define CHECK(x) do { ++checks;if (!(x)) { \
    fprintf(stderr,"FAIL line %u: %s (cases=%u checks=%u)\n",__LINE__,#x,cases,checks);exit(1); \
} } while (0)
#define CASE(name) do { ++cases;printf("PASS %s\n",name); } while (0)
static struct lg2_image stock;
static uint8_t synthetic[148];

struct fixture {
    struct lg2r_context context;
    struct lg2r_mu mu;
    struct lg2r_config config;
    EFI_BOOT_SERVICES boot;
    EFI_MEMORY_DESCRIPTOR desc[LG2R_MAX_DESCRIPTORS+4];
    _Alignas(8) uint8_t buffer[(LG2R_MAX_DESCRIPTORS+4)*64];
    char sequence[128];
    unsigned events,count,maps,allocations,requested_calls,ends;
    uint64_t key,authorized_key,auth_facts,own_facts,allocation_pages;
    uint64_t denied_pa;
    size_t stride;
    uint32_t version;
    unsigned fail_map_call,fail_allocate_call,stale_key,stale_coverage;
    unsigned deny_begin,deny_auth,deny_proof,wrong_address,bad_post,duplicate_tokens;
    unsigned reenter,post_attribute,warning,zero_token;
    enum lg2r_state end_state;
};
static struct fixture f;
static void event(char ch) { CHECK(f.events+1<sizeof f.sequence);f.sequence[f.events++]=ch;f.sequence[f.events]=0; }
static void check_reentry(void) {
    if (!f.reenter) return;
    struct lg2_allocation allocation={0};
    CHECK(lg2r_export_code(&f.context,&allocation)==LG2R_STATE);
    CHECK(lg2r_mu_reserve_stock(&f.context,&f.mu,&f.config,&stock,f.buffer,sizeof f.buffer)==LG2R_STATE);
}
static EFI_STATUS EFIAPI map_service(UINTN *bytes,EFI_MEMORY_DESCRIPTOR *out,UINTN *key,
                                     UINTN *stride,UINT32 *version) {
    event('M');++f.maps;check_reentry();
    if (f.maps==f.fail_map_call) return EFI_DEVICE_ERROR;
    if (f.warning) return EFI_WARN_UNKNOWN_GLYPH;
    UINTN need=(UINTN)f.count*f.stride;
    *stride=(UINTN)f.stride;*version=f.version;
    if (*bytes<need) { *bytes=need;return EFI_BUFFER_TOO_SMALL; }
    *bytes=need;*key=(UINTN)(f.key+(f.maps==2 && f.stale_key));
    memset(out,0,need);
    for (unsigned n=0;n<f.count;++n) {
        EFI_MEMORY_DESCRIPTOR d=f.desc[n];
        if (f.maps==2 && f.stale_coverage && d.Type==EfiConventionalMemory) d.Type=EfiReservedMemoryType;
        if (f.maps==3 && f.bad_post && d.Type==f.config.memory_type) d.Type=EfiReservedMemoryType;
        if (f.maps==3 && f.post_attribute && d.Type==f.config.memory_type) d.Attribute=EFI_MEMORY_RUNTIME;
        memcpy((uint8_t *)out+n*f.stride,&d,sizeof d);
    }
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI allocate_service(EFI_ALLOCATE_TYPE kind,EFI_MEMORY_TYPE type,
                                          UINTN pages,EFI_PHYSICAL_ADDRESS *address) {
    event('P');check_reentry();++f.requested_calls;
    CHECK(kind==AllocateAddress && (uint32_t)type==f.config.memory_type);
    CHECK(pages && *address>=LG2_POOL_START && *address+(uint64_t)pages*4096<=LG2_POOL_END);
    if (f.requested_calls==f.fail_allocate_call) return EFI_NOT_FOUND;
    uint64_t pa=*address+(f.wrong_address?4096:0),end=pa+(uint64_t)pages*4096;
    for (unsigned n=0;n<f.count;++n) {
        EFI_MEMORY_DESCRIPTOR old=f.desc[n];
        uint64_t oe=old.PhysicalStart+old.NumberOfPages*4096;
        if (old.Type!=EfiConventionalMemory || pa<old.PhysicalStart || end>oe) continue;
        unsigned before=pa>old.PhysicalStart,after=end<oe,extra=before+after;
        CHECK(f.count+extra<LG2R_MAX_DESCRIPTORS+4);
        memmove(f.desc+n+1+extra,f.desc+n+1,(f.count-n-1)*sizeof f.desc[0]);
        unsigned pos=n;
        if (before) { f.desc[pos]=old;f.desc[pos++].NumberOfPages=(pa-old.PhysicalStart)/4096; }
        f.desc[pos]=old;f.desc[pos].PhysicalStart=pa;f.desc[pos].NumberOfPages=pages;f.desc[pos++].Type=type;
        if (after) { f.desc[pos]=old;f.desc[pos].PhysicalStart=end;f.desc[pos].NumberOfPages=(oe-end)/4096; }
        f.count+=extra;++f.allocations;++f.key;f.allocation_pages+=pages;*address=pa;
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}
static int begin(void *cookie) { CHECK(cookie==&f);event('B');check_reentry();return (int)f.deny_begin; }
static void end(void *cookie,enum lg2r_state state) {
    CHECK(cookie==&f);event('E');check_reentry();++f.ends;f.end_state=state;
}
static int authorize(void *cookie,const struct lg2r_plan *p,uint64_t key,struct lg2r_proof *proof) {
    CHECK(cookie==&f && p->range_count>0 && key==f.key);event('A');check_reentry();
    f.authorized_key=key;proof->token=f.zero_token?0:0x53013;proof->facts=f.auth_facts;
    for (unsigned n=0;n<p->range_count;++n)
        if (p->ranges[n].pa==f.denied_pa) return 1;
    return (int)f.deny_auth;
}
static int prove(void *cookie,const struct lg2r_plan *p,uint64_t key,uint64_t auth,struct lg2r_proof *proofs) {
    CHECK(cookie==&f && key==f.key && auth==0x53013);event('O');check_reentry();
    for (unsigned n=0;n<p->range_count;++n) {
        proofs[n].token=f.zero_token?0:0x100+n*(f.duplicate_tokens?0:1);
        proofs[n].facts=f.own_facts;
    }
    return (int)f.deny_proof;
}
static void descriptor(unsigned n,uint32_t type,uint64_t pa,uint64_t pages,uint64_t attributes) {
    f.desc[n]=(EFI_MEMORY_DESCRIPTOR){type,pa,0,pages,attributes};
    if (f.count<=n) f.count=n+1;
}
static void reset_fixture(void) {
    memset(&f,0,sizeof f);
    f.boot.GetMemoryMap=map_service;f.boot.AllocatePages=allocate_service;
    f.mu.boot=&f.boot;f.mu.policy=(struct lg2r_policy){&f,begin,end,authorize,prove};
    f.config.memory_type=LG2R_EFI_LOADER_DATA;
    f.stride=48;f.version=1;f.key=91;
    f.auth_facts=LG2R_AUTH_REQUIRED;f.own_facts=LG2R_OWN_REQUIRED;
    descriptor(0,EfiReservedMemoryType,LG2_POOL_START,(LG2_POOL_END-LG2_POOL_START)/4096,0);
}
static void available(uint64_t pa,uint64_t pages) {
    f.count=0;
    if (pa>LG2_POOL_START) descriptor(f.count,EfiReservedMemoryType,LG2_POOL_START,(pa-LG2_POOL_START)/4096,0);
    descriptor(f.count,EfiConventionalMemory,pa,pages,EFI_MEMORY_WB);
    uint64_t end=pa+pages*4096;
    if (end<LG2_POOL_END) descriptor(f.count,EfiReservedMemoryType,end,(LG2_POOL_END-end)/4096,0);
}
static enum lg2r_status reserve_stock(void) {
    return lg2r_mu_reserve_stock(&f.context,&f.mu,&f.config,&stock,f.buffer,sizeof f.buffer);
}
/* Generic core tests keep the actual EFI callback API and translate its types. */
static int core_map(void *unused,void *buffer,size_t *bytes,uint64_t *key,size_t *stride,uint32_t *version) {
    (void)unused;UINTN n=*bytes,k=0,s=0;UINT32 v=0;
    EFI_STATUS status=map_service(&n,buffer,&k,&s,&v);
    *bytes=n;*key=k;*stride=s;*version=v;
    return status==EFI_SUCCESS?0:-1;
}
static int core_allocate(void *unused,uint32_t type,uint64_t pages,uint64_t *pa) {
    (void)unused;EFI_PHYSICAL_ADDRESS address=*pa;
    EFI_STATUS status=allocate_service(AllocateAddress,(EFI_MEMORY_TYPE)type,(UINTN)pages,&address);
    *pa=address;return status==EFI_SUCCESS?0:-1;
}
static enum lg2r_status reserve_synthetic(void) {
    struct lg2r_ops ops={NULL,core_map,core_allocate,f.mu.policy};
    return lg2r_reserve_mdt(&f.context,&ops,&f.config,synthetic,sizeof synthetic,f.buffer,sizeof f.buffer);
}
static void wr32(uint8_t *p,uint32_t n) { for (unsigned b=0;b<4;++b) p[b]=(uint8_t)(n>>(8*b)); }
static void fixed_image(uint32_t pa) {
    memcpy(synthetic,stock.mdt.data,sizeof synthetic);
    wr32(synthetic+52+64+12,pa);wr32(synthetic+52+64+24,7);
}
static void fixed_two(uint32_t a,uint32_t b) {
    fixed_image(a);
    memcpy(synthetic+52,synthetic+52+64,32);
    wr32(synthetic+52+12,b);
}
static struct lg2_blob read_blob(const char *dir,const char *name) {
    char path[1024];CHECK(snprintf(path,sizeof path,"%s/%s",dir,name)>0);
    FILE *file=fopen(path,"rb");CHECK(file!=NULL);CHECK(!fseek(file,0,SEEK_END));
    long n=ftell(file);CHECK(n>0 && !fseek(file,0,SEEK_SET));
    uint8_t *data=(uint8_t *)malloc((size_t)n);CHECK(data!=NULL);
    CHECK(fread(data,1,(size_t)n,file)==(size_t)n && !fclose(file));
    return (struct lg2_blob){data,(size_t)n};
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    stock.mdt=read_blob(argv[1],"a530_zap.mdt");
    stock.b[0]=read_blob(argv[1],"a530_zap.b00");
    stock.b[1]=read_blob(argv[1],"a530_zap.b01");
    stock.b[2]=read_blob(argv[1],"a530_zap.b02");
    struct lg2r_image image;
    CHECK(lg2r_parse_mdt(stock.mdt.data,stock.mdt.size,&image)==LG2R_OK);
    CHECK(image.count==1 && image.relocatable && image.min_paddr==0x5000 && image.span==8192);
    CHECK(image.segments[0].ph_index==2 && image.segments[0].bytes==4900);
    CHECK(image.segments[0].file_bytes==4900 && image.segments[0].elf_align==1048576);
    CASE("actual stock MDT excludes header/hash and plans relocatable 2-page b02");

    for (unsigned type=2;type<=4;type+=2) {
        reset_fixture();available(0x90003000,2);f.config.memory_type=type;f.reenter=1;
        CHECK(reserve_stock()==LG2R_OK);
        CHECK(f.context.state==LG2R_RESERVED && f.context.allocated_count==1);
        CHECK(f.context.plan.segment_pa[0]==0x90003000 && f.context.plan.pas_span==8192);
        CHECK(f.requested_calls==1 && f.allocation_pages==2 && f.allocations==1);
        CHECK(!strcmp(f.sequence,"BMAMPMOE") && f.end_state==LG2R_RESERVED);
        struct lg2_allocation allocation={0};CHECK(lg2r_export_code(&f.context,&allocation)==LG2R_OK);
        CHECK(allocation.pa==0x90003000 && allocation.bytes==8192 && allocation.page_bytes==4096);
        CHECK(allocation.owner_token && !allocation.data_offset && allocation.cache==LG2_CACHE_UNVERIFIED);
        CHECK(reserve_stock()==LG2R_STATE);
    }
    CASE("real Mu adapter requests AllocateAddress exact pages, post-map and owned/excluded proof");

    for (unsigned type=0;type<16;++type) {
        if (type==EfiConventionalMemory) continue;
        reset_fixture();f.desc[0].Type=type;
        CHECK(reserve_stock()==LG2R_NO_AVAILABLE && !f.requested_calls);
    }
    reset_fixture();f.count=1;descriptor(0,EfiConventionalMemory,0x100000000,100,EFI_MEMORY_WB);
    CHECK(reserve_stock()==LG2R_NO_AVAILABLE && !f.requested_calls);
    reset_fixture();f.count=1;descriptor(0,EfiConventionalMemory,0x41600000,100,EFI_MEMORY_WB);
    CHECK(reserve_stock()==LG2R_NO_AVAILABLE && !f.requested_calls);
    CASE("no-map pool absent, reserved/TZ/firmware/runtime types never adopted");

    for (uint64_t attr=1;attr;attr<<=1) {
        if (!(attr&~UINT64_C(0x401f))) continue;
        reset_fixture();available(0x90000000,2);
        for (unsigned n=0;n<f.count;++n) if (f.desc[n].Type==EfiConventionalMemory) f.desc[n].Attribute|=attr;
        CHECK(reserve_stock()==LG2R_NO_AVAILABLE && !f.requested_calls);
    }
    CASE("runtime/protected/nonvolatile/special-purpose/unknown available attributes denied");

    for (size_t stride=40;stride<=64;stride+=8) {
        reset_fixture();f.stride=stride;f.count=0;
        descriptor(0,EfiConventionalMemory,0x90001000,1,EFI_MEMORY_WB);
        descriptor(1,EfiConventionalMemory,0x90000000,1,EFI_MEMORY_WB);
        /* EFI implementation is permitted to reject an allocation spanning
         * distinct internal descriptors; that failure must stay closed. */
        CHECK(reserve_stock()==LG2R_ALLOCATE && f.requested_calls==1 && !f.allocations);
        CHECK(f.context.state==LG2R_FAILED);
    }
    reset_fixture();f.count=0;
    descriptor(0,EfiConventionalMemory,0x90000000,1,EFI_MEMORY_WB);
    descriptor(1,EfiReservedMemoryType,0x90001000,1,0);
    descriptor(2,EfiConventionalMemory,0x90002000,1,EFI_MEMORY_WB);
    CHECK(reserve_stock()==LG2R_NO_AVAILABLE && !f.requested_calls);
    CASE("descriptor stride/order and fragmented noncontiguous pages; no fallback allocation");

    reset_fixture();available(0x90000000,2);fixed_image(0x90000000);
    CHECK(reserve_synthetic()==LG2R_OK && !f.context.plan.relocatable);
    CHECK(f.context.plan.segment_pa[0]==0x90000000);
    reset_fixture();available(0x90000000,2);fixed_image(0x92300000);
    CHECK(reserve_synthetic()==LG2R_FIXED_UNAVAILABLE && !f.requested_calls);
    CHECK(f.context.blocked_pa==0x92300000);
    reset_fixture();available(0x90000000,2);fixed_image(0x5000);
    CHECK(reserve_synthetic()==LG2R_FIXED_UNAVAILABLE && f.context.blocked_pa==0x5000 && !f.requested_calls);
    printf("BLOCKER model: fixed p_paddr=0x5000 lies outside stock no-map window; no relocation fallback\n");
    CASE("fixed PA retained exactly; unavailable/out-of-pool stock-style fixed demand blocks closed");

    reset_fixture();available(0x90000000,10);fixed_two(0x90000000,0x90008000);
    CHECK(reserve_synthetic()==LG2R_OK && f.context.plan.range_count==2);
    CHECK(f.allocation_pages==4 && f.requested_calls==2); /* 6-page hole never allocated */
    reset_fixture();available(0x90000000,10);fixed_two(0x90000000,0x90008000);
    f.fail_allocate_call=2;
    CHECK(reserve_synthetic()==LG2R_ALLOCATE && f.context.state==LG2R_PINNED_FAILURE);
    CHECK(f.context.allocated_count==1 && f.allocation_pages==2 && f.requested_calls==2);
    struct lg2_allocation output={0};CHECK(lg2r_export_code(&f.context,&output)==LG2R_STATE);
    CASE("multiple fixed segment pages only; partial allocation pins without reclaim/rollback");

    reset_fixture();available(0x90000000,10);fixed_two(0x90000000,0x90008000);
    f.duplicate_tokens=1;
    CHECK(reserve_synthetic()==LG2R_OWNERSHIP && f.context.state==LG2R_PINNED_FAILURE);
    for (unsigned missing=0;missing<2;++missing) {
        reset_fixture();available(0x90000000,2);f.auth_facts&=~(UINT64_C(1)<<missing);
        CHECK(reserve_stock()==LG2R_AUTHORIZATION && !f.requested_calls);
    }
    for (unsigned missing=0;missing<4;++missing) {
        reset_fixture();available(0x90000000,2);f.own_facts&=~(UINT64_C(1)<<missing);
        CHECK(reserve_stock()==LG2R_OWNERSHIP && f.context.state==LG2R_PINNED_FAILURE);
        CHECK(f.context.allocated_count==1);
    }
    reset_fixture();available(0x90000000,2);f.zero_token=1;
    CHECK(reserve_stock()==LG2R_AUTHORIZATION && !f.requested_calls);
    CASE("ownership authorization, unique token and persistent firmware/XNU exclusions mandatory");

    reset_fixture();available(0x90000000,2);f.deny_begin=1;
    CHECK(reserve_stock()==LG2R_AUTHORIZATION && !f.requested_calls && !f.ends);
    reset_fixture();available(0x90000000,2);f.deny_auth=1;
    CHECK(reserve_stock()==LG2R_AUTHORIZATION && !f.requested_calls && f.ends==1);
    reset_fixture();available(0x90000000,2);f.deny_proof=1;
    CHECK(reserve_stock()==LG2R_OWNERSHIP && f.context.state==LG2R_PINNED_FAILURE);
    for (unsigned call=1;call<=3;++call) {
        reset_fixture();available(0x90000000,2);f.fail_map_call=call;
        CHECK(reserve_stock()==LG2R_MAP_CALLBACK && f.maps==call);
        CHECK(f.context.state==(call==3?LG2R_PINNED_FAILURE:LG2R_FAILED));
    }
    reset_fixture();available(0x90000000,2);f.warning=1;
    CHECK(reserve_stock()==LG2R_MAP_CALLBACK && !f.requested_calls);
    CASE("callback failures and EFI warnings fail closed without retry");

    for (unsigned which=0;which<2;++which) {
        reset_fixture();available(0x90000000,2);
        if (which) f.stale_coverage=1;else f.stale_key=1;
        CHECK(reserve_stock()==LG2R_STALE_MAP && !f.requested_calls);
    }
    reset_fixture();available(0x90000000,4);f.wrong_address=1;
    CHECK(reserve_stock()==LG2R_WRONG_ADDRESS && f.context.state==LG2R_PINNED_FAILURE);
    CHECK(f.context.allocations[0].requested_pa==0x90000000 && f.context.allocations[0].actual_pa==0x90001000);
    for (unsigned which=0;which<2;++which) {
        reset_fixture();available(0x90000000,2);
        if (which) f.post_attribute=1;else f.bad_post=1;
        CHECK(reserve_stock()==LG2R_POST_MAP && f.context.state==LG2R_PINNED_FAILURE);
    }
    CASE("map-key/coverage changes, wrong returned PA and bad post-map quarantine exact records");

    reset_fixture();f.version=2;
    CHECK(reserve_stock()==LG2R_BAD_MAP && !f.requested_calls);
    reset_fixture();f.count=0;
    CHECK(reserve_stock()==LG2R_BAD_MAP && !f.requested_calls);
    reset_fixture();f.count=LG2R_MAX_DESCRIPTORS+1;
    for (unsigned n=0;n<f.count;++n) descriptor(n,EfiReservedMemoryType,0x10000000+(uint64_t)n*4096,1,0);
    CHECK(reserve_stock()==LG2R_BAD_MAP && !f.requested_calls);
    for (unsigned which=0;which<4;++which) {
        reset_fixture();
        if (which==0) f.desc[0].NumberOfPages=UINT64_MAX;
        if (which==1) f.desc[0].PhysicalStart=UINT64_MAX-4095;
        if (which==2) f.desc[0].PhysicalStart++;
        if (which==3) f.desc[0].NumberOfPages=0;
        CHECK(reserve_stock()==LG2R_BAD_MAP && !f.requested_calls);
    }
    reset_fixture();available(0x90000000,2);
    descriptor(f.count,EfiReservedMemoryType,0x90001000,1,0);
    CHECK(reserve_stock()==LG2R_BAD_MAP && !f.requested_calls);
    CASE("map version/count, descriptor overflow/alignment/zero pages and reserved overlap rejected");

    for (unsigned which=0;which<7;++which) {
        memcpy(synthetic,stock.mdt.data,sizeof synthetic);
        if (which==0) wr32(synthetic+28,UINT32_MAX);
        if (which==1) { synthetic[44]=17;synthetic[45]=0; }
        if (which==2) wr32(synthetic+52+64+12,0xfffff000);
        if (which==3) wr32(synthetic+52+64+20,1);
        if (which==4) wr32(synthetic+52+64+28,3);
        if (which==5) wr32(synthetic+52+64,0);
        if (which==6) synthetic[5]=2;
        CHECK(lg2r_parse_mdt(synthetic,sizeof synthetic,&image)!=LG2R_OK);
    }
    fixed_two(0x90000000,0x90008000);wr32(synthetic+52+24,0x08000007);
    CHECK(lg2r_parse_mdt(synthetic,sizeof synthetic,&image)==LG2R_UNSUPPORTED);
    reset_fixture();available(0x90000000,10);fixed_two(0x5000,0xd000);
    wr32(synthetic+52+24,0x08000007);wr32(synthetic+52+64+24,0x08000007);
    CHECK(reserve_synthetic()==LG2R_UNSUPPORTED && !f.requested_calls);
    CASE("ELF table/address/file-size overflow, mixed relocation and unexplained relocatable gaps rejected");

    /* Independent page bitmap oracle for fragmentation. Only a small island
     * is described; absence of the rest of the no-map window stays occupied.
     */
    uint64_t seed=0x530135a;
    for (unsigned round=0;round<2000;++round) {
        reset_fixture();f.count=0;
        unsigned free_page[32],expected=32;
        for (unsigned n=0;n<32;++n) {
            seed=seed*UINT64_C(6364136223846793005)+1;
            free_page[n]=(unsigned)((seed>>32)&1);
            if (n && free_page[n] && free_page[n-1] && expected==32) expected=n-1;
        }
        unsigned start=0;
        while (start<32) {
            unsigned finish=start+1;
            while (finish<32 && free_page[finish]==free_page[start]) ++finish;
            descriptor(f.count,free_page[start]?EfiConventionalMemory:EfiReservedMemoryType,
                       0x91000000+(uint64_t)start*4096,finish-start,EFI_MEMORY_WB);
            start=finish;
        }
        enum lg2r_status s=reserve_stock();
        if (expected==32) CHECK(s==LG2R_NO_AVAILABLE && !f.requested_calls);
        else CHECK(s==LG2R_OK && f.context.plan.segment_pa[0]==0x91000000+(uint64_t)expected*4096 &&
                   f.allocation_pages==2 && f.requested_calls==1);
    }
    CASE("2000 deterministic fragmentation maps checked against independent per-page oracle");
    free((void *)stock.mdt.data);
    for (unsigned n=0;n<3;++n) free((void *)stock.b[n].data);
    printf("PASS G2 reserve: %u cases, %u checks; host synthetic services; actual stock metadata; no device/SMC/MMIO\n",cases,checks);
    return 0;
}
