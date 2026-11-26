/* SPDX-License-Identifier: BSD-2-Clause */
#include "g2-reserve.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p+4)<<32; }
static int extent(uint64_t pa, uint64_t pages, uint64_t *end) {
    if (!pages || pages>UINT64_MAX/LG2R_PAGE ||
        pa>UINT64_MAX-pages*LG2R_PAGE || (pa&(LG2R_PAGE-1))) return 0;
    *end=pa+pages*LG2R_PAGE;
    return 1;
}
static uint64_t rounded(uint64_t n) { return (n+LG2R_PAGE-1)&~(LG2R_PAGE-1); }
static int attributes_ok(uint64_t a) {
    /* Cache capabilities and execute-protect only. Runtime, RO/WP/RP,
     * persistent/special-purpose and unknown attributes fail closed.
     * This test does not establish any CPU cache mapping/coherency.
     */
    return !(a & ~UINT64_C(0x401f));
}
enum lg2r_status lg2r_parse_mdt(const uint8_t *mdt, size_t size, struct lg2r_image *im) {
    uint64_t min=UINT64_MAX,max=0;
    unsigned relocation=2;
    if (!mdt || !im) return LG2R_ARGUMENT;
    im->count=0; im->relocatable=0; im->min_paddr=0; im->span=0;
    if (size<52 || rd32(mdt)!=UINT32_C(0x464c457f) ||
        mdt[4]!=1 || mdt[5]!=1 || mdt[6]!=1 || rd16(mdt+16)!=2 ||
        rd16(mdt+18)!=164 || rd32(mdt+20)!=1 || rd16(mdt+40)!=52 ||
        rd16(mdt+42)!=32) return LG2R_ELF;
    unsigned count=rd16(mdt+44);
    uint64_t phoff=rd32(mdt+28);
    if (!count || count>LG2R_MAX_PHDR || phoff<52 || phoff>size ||
        (uint64_t)count*32>size-phoff) return LG2R_ELF;
    for (unsigned i=0;i<count;++i) {
        const uint8_t *p=mdt+(size_t)phoff+i*32;
        uint32_t flags=rd32(p+24),mem=rd32(p+20),file=rd32(p+16);
        if (rd32(p)!=1 || (flags&UINT32_C(0x07000000))==UINT32_C(0x02000000) || !mem) continue;
        uint64_t pa=rd32(p+12),end=pa+mem;
        uint32_t align=rd32(p+28);
        if (file>mem || end>UINT64_C(0x100000000) ||
            (align && (align&(align-1)))) return LG2R_RANGE;
        unsigned reloc=!!(flags&UINT32_C(0x08000000));
        if (relocation!=2 && relocation!=reloc) return LG2R_UNSUPPORTED;
        relocation=reloc;
        /* Reject overlapping loaded bytes independently of phdr ordering. */
        for (unsigned n=0;n<im->count;++n) {
            const struct lg2r_segment *s=&im->segments[n];
            if (pa<s->paddr+s->bytes && s->paddr<end) return LG2R_RANGE;
        }
        struct lg2r_segment *s=&im->segments[im->count++];
        s->paddr=pa;s->bytes=mem;s->file_bytes=file;s->flags=flags;
        s->elf_align=align;s->ph_index=i;
        if (pa<min) min=pa;
        if (end>max) max=end;
    }
    if (!im->count) return LG2R_ELF;
    if (relocation && (min&(LG2R_PAGE-1))) return LG2R_UNSUPPORTED;
    im->relocatable=relocation;im->min_paddr=min;im->span=rounded(max)-(min&~(LG2R_PAGE-1));
    if (im->span>LG2_POOL_END-LG2_POOL_START) return LG2R_RANGE;
    return LG2R_OK;
}

static enum lg2r_status read_map(struct lg2r_context *c, const struct lg2r_ops *o,
                                void *buffer, size_t capacity, uint64_t *key) {
    size_t bytes=capacity,stride=0;
    uint32_t version=0;
    c->map_calls++;
    c->callback_status=o->get_map(o->platform,buffer,&bytes,key,&stride,&version);
    if (c->callback_status) return LG2R_MAP_CALLBACK;
    if (version!=1 || stride<40 || stride>capacity || !bytes || bytes>capacity ||
        bytes%stride || bytes/stride>LG2R_MAX_DESCRIPTORS) return LG2R_BAD_MAP;
    c->descriptor_count=(unsigned)(bytes/stride);
    for (unsigned n=0;n<c->descriptor_count;++n) {
        const uint8_t *p=(const uint8_t *)buffer+n*stride;
        struct lg2r_descriptor d={rd64(p+8),rd64(p+24),rd64(p+32),rd32(p)};
        uint64_t end;
        if (!extent(d.pa,d.pages,&end)) return LG2R_BAD_MAP;
        unsigned pos=n;
        while (pos && c->descriptors[pos-1].pa>d.pa) {
            c->descriptors[pos]=c->descriptors[pos-1];--pos;
        }
        c->descriptors[pos]=d;
    }
    for (unsigned n=1;n<c->descriptor_count;++n) {
        const struct lg2r_descriptor *p=&c->descriptors[n-1];
        if (p->pa+p->pages*LG2R_PAGE>c->descriptors[n].pa) return LG2R_BAD_MAP;
    }
    return LG2R_OK;
}
static int covered(const struct lg2r_context *c, const struct lg2r_range *r, uint32_t type) {
    uint64_t end;
    if (!extent(r->pa,r->pages,&end) || r->pa<LG2_POOL_START || end>LG2_POOL_END) return 0;
    uint64_t cursor=r->pa;
    for (unsigned n=0;n<c->descriptor_count && cursor<end;++n) {
        const struct lg2r_descriptor *d=&c->descriptors[n];
        uint64_t de=d->pa+d->pages*LG2R_PAGE;
        if (de<=cursor) continue;
        if (d->pa>cursor || d->type!=type || !attributes_ok(d->attributes)) return 0;
        cursor=de<end?de:end;
    }
    return cursor==end;
}
static enum lg2r_status plan_at(struct lg2r_context *c, uint64_t base) {
    struct lg2r_plan *p=&c->plan;
    const struct lg2r_image *im=&c->image;
    p->range_count=0;p->segment_count=im->count;p->relocatable=im->relocatable;
    p->relocation_base=im->relocatable?base:0;p->pas_span=im->span;
    for (unsigned n=0;n<im->count;++n) {
        const struct lg2r_segment *s=&im->segments[n];
        uint64_t offset=im->relocatable?s->paddr-im->min_paddr:0;
        if (im->relocatable && base>UINT64_MAX-offset) return LG2R_RANGE;
        uint64_t pa=im->relocatable?base+offset:s->paddr;
        if (pa>UINT64_MAX-s->bytes || pa+s->bytes>UINT64_MAX-(LG2R_PAGE-1)) return LG2R_RANGE;
        uint64_t end=rounded(pa+s->bytes),start=pa&~(LG2R_PAGE-1);
        if (start<LG2_POOL_START || end>LG2_POOL_END) {
            c->blocked_pa=start;
            return im->relocatable?LG2R_RANGE:LG2R_FIXED_UNAVAILABLE;
        }
        p->segment_pa[n]=pa;
        struct lg2r_range r={start,(end-start)/LG2R_PAGE};
        unsigned pos=p->range_count++;
        while (pos && p->ranges[pos-1].pa>start) { p->ranges[pos]=p->ranges[pos-1];--pos; }
        p->ranges[pos]=r;
    }
    /* Union shared/adjacent allocation pages, never fill unowned holes. */
    unsigned used=0;
    for (unsigned n=0;n<p->range_count;++n) {
        struct lg2r_range r=p->ranges[n];
        if (used) {
            struct lg2r_range *prev=&p->ranges[used-1];
            uint64_t end=prev->pa+prev->pages*LG2R_PAGE,re=r.pa+r.pages*LG2R_PAGE;
            if (r.pa<=end) {
                if (re>end) prev->pages=(re-prev->pa)/LG2R_PAGE;
                continue;
            }
        }
        p->ranges[used++]=r;
    }
    p->range_count=used;
    if (im->relocatable && (used!=1 || p->ranges[0].pages*LG2R_PAGE!=im->span))
        return LG2R_UNSUPPORTED; /* no undocumented PAS ownership of gaps */
    for (unsigned n=0;n<used;++n)
        if (!covered(c,&p->ranges[n],LG2R_EFI_CONVENTIONAL)) {
            c->blocked_pa=p->ranges[n].pa;
            return im->relocatable?LG2R_NO_AVAILABLE:LG2R_FIXED_UNAVAILABLE;
        }
    return LG2R_OK;
}
static enum lg2r_status make_plan(struct lg2r_context *c) {
    if (!c->image.relocatable) return plan_at(c,0);
    /* Search only explicit conventional descriptors. Absent/unknown/reserved
     * memory and all other pages of the stock window remain untouched.
     */
    for (unsigned n=0;n<c->descriptor_count;++n) {
        const struct lg2r_descriptor *d=&c->descriptors[n];
        if (d->type!=LG2R_EFI_CONVENTIONAL || !attributes_ok(d->attributes)) continue;
        uint64_t de=d->pa+d->pages*LG2R_PAGE;
        uint64_t start=d->pa>LG2_POOL_START?d->pa:LG2_POOL_START;
        uint64_t limit=de<LG2_POOL_END?de:LG2_POOL_END;
        for (uint64_t pa=start;pa<limit && pa<=LG2_POOL_END-c->image.span;pa+=LG2R_PAGE) {
            enum lg2r_status s=plan_at(c,pa);
            if (s==LG2R_OK || s==LG2R_UNSUPPORTED) return s;
        }
    }
    return LG2R_NO_AVAILABLE;
}
static enum lg2r_status finish(struct lg2r_context *c, const struct lg2r_ops *o, enum lg2r_status s) {
    enum lg2r_state state=s==LG2R_OK?LG2R_RESERVED:
        (c->allocated_count?LG2R_PINNED_FAILURE:LG2R_FAILED);
    c->status=s;
    /* Keep BUSY through callback; final state is explicit in notification. */
    o->policy.end(o->policy.cookie,state);
    c->state=state;
    return s;
}
enum lg2r_status lg2r_reserve_mdt(struct lg2r_context *c, const struct lg2r_ops *o,
                                const struct lg2r_config *cfg, const uint8_t *mdt, size_t size,
                                void *buffer, size_t capacity) {
    if (!c || !o || !cfg || !buffer || capacity<40 ||
        !o->get_map || !o->allocate_exact || !o->policy.begin || !o->policy.end ||
        !o->policy.authorize || !o->policy.prove_owned ||
        (cfg->memory_type!=LG2R_EFI_LOADER_DATA && cfg->memory_type!=LG2R_EFI_BOOT_DATA))
        return LG2R_ARGUMENT;
    if (c->state!=LG2R_EMPTY) return LG2R_STATE;
    enum lg2r_status s=lg2r_parse_mdt(mdt,size,&c->image);
    if (s!=LG2R_OK) { c->status=s;c->state=LG2R_FAILED;return s; }
    c->state=LG2R_BUSY;
    c->callback_status=o->policy.begin(o->policy.cookie);
    if (c->callback_status) { c->state=LG2R_FAILED;c->status=LG2R_AUTHORIZATION;return c->status; }
    s=read_map(c,o,buffer,capacity,&c->initial_map_key);
    if (s!=LG2R_OK) return finish(c,o,s);
    s=make_plan(c);
    if (s!=LG2R_OK) return finish(c,o,s);
    c->blocked_pa=0;
    c->callback_status=o->policy.authorize(o->policy.cookie,&c->plan,c->initial_map_key,&c->authorization);
    if (c->callback_status || !c->authorization.token ||
        (c->authorization.facts&LG2R_AUTH_REQUIRED)!=LG2R_AUTH_REQUIRED)
        return finish(c,o,LG2R_AUTHORIZATION);
    uint64_t key=0;
    s=read_map(c,o,buffer,capacity,&key);
    if (s!=LG2R_OK) return finish(c,o,s);
    if (key!=c->initial_map_key) return finish(c,o,LG2R_STALE_MAP);
    for (unsigned n=0;n<c->plan.range_count;++n)
        if (!covered(c,&c->plan.ranges[n],LG2R_EFI_CONVENTIONAL)) return finish(c,o,LG2R_STALE_MAP);
    for (unsigned n=0;n<c->plan.range_count;++n) {
        const struct lg2r_range *r=&c->plan.ranges[n];
        struct lg2r_allocation *a=&c->allocations[n];
        a->requested_pa=r->pa;a->pages=r->pages;
        uint64_t actual=r->pa;
        c->allocation_calls++;
        c->callback_status=o->allocate_exact(o->platform,cfg->memory_type,r->pages,&actual);
        if (c->callback_status) return finish(c,o,LG2R_ALLOCATE);
        a->allocated=1;a->actual_pa=actual;c->allocated_count++;
        if (actual!=r->pa) return finish(c,o,LG2R_WRONG_ADDRESS);
    }
    s=read_map(c,o,buffer,capacity,&c->final_map_key);
    if (s!=LG2R_OK) return finish(c,o,s);
    for (unsigned n=0;n<c->plan.range_count;++n)
        if (!covered(c,&c->plan.ranges[n],cfg->memory_type)) return finish(c,o,LG2R_POST_MAP);
    c->callback_status=o->policy.prove_owned(o->policy.cookie,&c->plan,c->final_map_key,
                                           c->authorization.token,c->proofs);
    if (c->callback_status) return finish(c,o,LG2R_OWNERSHIP);
    for (unsigned n=0;n<c->plan.range_count;++n) {
        const struct lg2r_proof *p=&c->proofs[n];
        if (!p->token || (p->facts&LG2R_OWN_REQUIRED)!=LG2R_OWN_REQUIRED)
            return finish(c,o,LG2R_OWNERSHIP);
        for (unsigned j=0;j<n;++j)
            if (c->proofs[j].token==p->token) return finish(c,o,LG2R_OWNERSHIP);
        c->allocations[n].ownership=*p;
    }
    return finish(c,o,LG2R_OK);
}
enum lg2r_status lg2r_reserve_stock(struct lg2r_context *c, const struct lg2r_ops *o,
                                  const struct lg2r_config *cfg, const struct lg2_image *im,
                                  void *buffer, size_t capacity) {
    struct lg2_info info;
    if (lg2_validate_image(im,&info)!=LG2_OK) return LG2R_ELF;
    return lg2r_reserve_mdt(c,o,cfg,im->mdt.data,im->mdt.size,buffer,capacity);
}
enum lg2r_status lg2r_export_code(const struct lg2r_context *c, struct lg2_allocation *out) {
    if (!c || !out) return LG2R_ARGUMENT;
    if (c->state!=LG2R_RESERVED) return LG2R_STATE;
    if (c->plan.range_count!=1 || c->allocations[0].pages*LG2R_PAGE!=LG2_CODE_SPAN ||
        !c->plan.relocatable) return LG2R_UNSUPPORTED;
    struct lg2_allocation a={0};
    a.pa=c->allocations[0].actual_pa;a.bytes=c->allocations[0].pages*LG2R_PAGE;
    a.owner_token=c->allocations[0].ownership.token;a.page_bytes=4096;
    a.cache=LG2_CACHE_UNVERIFIED;
    *out=a;
    return LG2R_OK;
}
