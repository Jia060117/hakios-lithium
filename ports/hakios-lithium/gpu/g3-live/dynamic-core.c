/* SPDX-License-Identifier: GPL-2.0-only
 * Reuse the actual gs table walk/PTE encoding helpers in this private TU.
 * Rename PUBLIC FUNCTIONS/types consistently BEFORE its header is included;
 * this does not redefine callback member names begin/end or alter gs sources.
 * No original fixed-pool map/publish/lookup API is used for scatter mappings.
 */
#define gs_init lg3d_seed_init
#define gs_build_pool lg3d_seed_build_pool
#define gs_bind_callbacks lg3d_seed_bind_callbacks
#define gs_layout lg3d_seed_layout
#define gs_table_view lg3d_seed_table_view
#define gs_publish lg3d_seed_publish
#define gs_map lg3d_seed_map
#define gs_unmap lg3d_seed_unmap
#define gs_map_pool lg3d_seed_map_pool
#define gs_alloc lg3d_seed_alloc
#define gs_lookup lg3d_seed_lookup
#define gs_fault_decode lg3d_seed_fault_decode
#include <gpu/smmu/smmu.c>
#undef gs_init
#undef gs_build_pool
#undef gs_bind_callbacks
#undef gs_layout
#undef gs_table_view
#undef gs_publish
#undef gs_map
#undef gs_unmap
#undef gs_map_pool
#undef gs_alloc
#undef gs_lookup
#undef gs_fault_decode
#include "dynamic-core.h"
_Static_assert(LG3D_PAGE==GS_PAGE && LG3D_PAGES==GS_POOL_PAGES,"gs geometry");
#define LG3D_HASH 16384u
#define LG3D_TOMB 65535u
struct lg3d_record { struct lg3d_mapping map; unsigned first,pages,permissions; };
struct lg3d_domain {
    struct gs_domain tree; /* cfg.pool_pa is UNUSED, never a data aperture. */
    struct lg3d_ops ops;
    enum lg3d_state state;
    uint64_t generation,next_token,pa[LG3D_PAGES];
    uint16_t owner[LG3D_PAGES],hash[LG3D_HASH];
    unsigned outstanding;
    struct lg3d_record record[LG3D_MAX_MAPS];
};
size_t lg3d_domain_bytes(void) { return sizeof(struct lg3d_domain); }
size_t lg3d_arena_bytes(void) { return sizeof(struct gs_arena); }
static unsigned lg3d_hash(uint64_t pa)
{ pa>>=12;pa^=pa>>17;pa*=UINT64_C(0x9e3779b97f4a7c15);return (unsigned)(pa^(pa>>32))&(LG3D_HASH-1u); }
static int lg3d_reserve_pa(struct lg3d_domain *d,unsigned index)
{
    unsigned h=lg3d_hash(d->pa[index]),tomb=LG3D_HASH;
    for(unsigned n=0;n<LG3D_HASH;++n,h=(h+1u)&(LG3D_HASH-1u)) {
        unsigned v=d->hash[h];
        if(v==LG3D_TOMB) {if(tomb==LG3D_HASH)tomb=h;continue;}
        if(!v) {d->hash[tomb==LG3D_HASH?h:tomb]=(uint16_t)(index+1u);return 1;}
        if(d->pa[v-1u]==d->pa[index])return 0;
    }
    if(tomb<LG3D_HASH) {d->hash[tomb]=(uint16_t)(index+1u);return 1;}
    return 0;
}
static void lg3d_release_pa(struct lg3d_domain *d,unsigned index)
{
    unsigned h=lg3d_hash(d->pa[index]);
    for(unsigned n=0;n<LG3D_HASH;++n,h=(h+1u)&(LG3D_HASH-1u)) {
        if(!d->hash[h])break;
        if(d->hash[h]==index+1u) {d->hash[h]=LG3D_TOMB;break;}
    }
    d->pa[index]=0;d->owner[index]=0;
}
static int lg3d_ops_ok(const struct lg3d_ops *o)
{ return o&&o->data_sync&&o->table_clean&&o->barrier&&o->invalidate_asid&&o->tlb_sync; }
enum lg3d_status lg3d_init(struct lg3d_domain *d,void *arena,
    const struct lg3d_config *c,const struct lg3d_ops *o)
{
    if(!d||!arena||!c||!lg3d_ops_ok(o)||((uintptr_t)arena&(GS_PAGE-1u))||
       !c->generation||!c->iova_base||!aligned(c->iova_base)||!aligned(c->table_pa)||
       !c->table_pa||!pa_width(c->pa_bits))return LG3D_ARGUMENT;
    if(d->state!=LG3D_EMPTY)return LG3D_STATE;
    if(!fits(c->iova_base,LG3D_BYTES,32)||!fits(c->table_pa,sizeof(struct gs_arena),c->pa_bits))return LG3D_RANGE;
    clear_bytes(d,sizeof(*d));clear_bytes(arena,sizeof(struct gs_arena));
    d->ops=*o;d->generation=c->generation;d->next_token=1;
    struct gs_domain *t=&d->tree;
    t->arena=arena;t->cfg.iova_base=c->iova_base;t->cfg.table_pa=c->table_pa;
    t->cfg.iova_bits=32;t->cfg.pa_bits=c->pa_bits;t->cfg.table_capacity=GS_TABLES;
    t->start_level=1;t->tables_used=1;
    /* Same offline skeleton as gs_init; data pool range/bitmap are not used.
     * All 8192 invalid leaves exist before the root can be installed.
     */
    for(unsigned p=0;p<LG3D_PAGES;++p) {
        uint64_t va=c->iova_base+(uint64_t)p*GS_PAGE;unsigned table=0;
        for(unsigned l=t->start_level;l<3;++l) {
            uint64_t *entry=&t->arena->pte[table][slot(va,l)];
            if(!*entry) {
                if(t->tables_used==GS_TABLES)return LG3D_FULL;
                unsigned child=t->tables_used++;*entry=table_address(t,child)|3u;table=child;
            } else if(!table_index(t,*entry,&table))return LG3D_STATE;
        }
    }
    t->state=GS_OFFLINE;d->state=LG3D_OFFLINE;return LG3D_OK;
}
enum lg3d_status lg3d_get_layout(const struct lg3d_domain *d,struct lg3d_layout *l)
{
    if(!d||!l||d->state==LG3D_EMPTY||d->state==LG3D_BUSY_STATE)return LG3D_STATE;
    unsigned ips=0;const unsigned widths[]={32,36,40,42,44,48};
    while(ips<6&&widths[ips]!=d->tree.cfg.pa_bits)++ips;
    *l=(struct lg3d_layout){d->tree.cfg.table_pa,GS_MAIR,
        (uint64_t)d->tree.tables_used*GS_PAGE,d->tree.tables_used,1,32,ips};return LG3D_OK;
}
static int lg3d_synchronize(struct lg3d_domain *d,uint32_t dirty)
{
    for(unsigned n=0;n<d->tree.tables_used;++n)if(dirty&(1u<<n))
        if(d->ops.table_clean(d->ops.cookie,d->tree.arena->pte[n],table_address(&d->tree,n),GS_PAGE))return 0;
    return !d->ops.barrier(d->ops.cookie)&&!d->ops.invalidate_asid(d->ops.cookie)&&
        !d->ops.tlb_sync(d->ops.cookie)&&!d->ops.barrier(d->ops.cookie);
}
enum lg3d_status lg3d_publish(struct lg3d_domain *d)
{
    if(!d||d->state!=LG3D_OFFLINE)return LG3D_STATE;
    d->state=LG3D_BUSY_STATE;
    if(!lg3d_synchronize(d,(1u<<d->tree.tables_used)-1u)) {
        d->state=LG3D_QUARANTINED;return LG3D_POISONED;
    }
    d->state=LG3D_READY;return LG3D_OK;
}
static enum lg3d_status lg3d_ready(const struct lg3d_domain *d)
{
    if(!d)return LG3D_ARGUMENT;
    if(d->state==LG3D_QUARANTINED)return LG3D_POISONED;
    return d->state==LG3D_READY?LG3D_OK:LG3D_STATE;
}
static struct lg3d_record *lg3d_find(struct lg3d_domain *d,const struct lg3d_mapping *m)
{
    if(!m||!m->token||m->generation!=d->generation)return NULL;
    for(unsigned n=0;n<LG3D_MAX_MAPS;++n) {
        struct lg3d_record *r=&d->record[n];
        if(r->map.state&&r->map.token==m->token&&r->map.iova==m->iova&&
           r->map.bytes==m->bytes)return r;
    }
    return NULL;
}
enum lg3d_status lg3d_map(struct lg3d_domain *d,const uint64_t *pa,unsigned pages,
    unsigned permissions,struct lg3d_mapping *out)
{
    if(out)clear_bytes(out,sizeof(*out));
    if(!pa||!out||!pages||pages>LG3D_PAGES||!(permissions&LG3D_READ)||
       (permissions&~(LG3D_READ|LG3D_WRITE)))return LG3D_ARGUMENT;
    enum lg3d_status e=lg3d_ready(d);if(e)return e;
    if(!d->next_token)return LG3D_FULL;
    unsigned ri=0,first=0,run=0;
    while(ri<LG3D_MAX_MAPS&&d->record[ri].map.state)++ri;
    for(unsigned p=0;p<LG3D_PAGES&&run<pages;++p)
        if(d->owner[p])run=0;else {if(!run)first=p;++run;}
    if(ri==LG3D_MAX_MAPS||run<pages)return LG3D_FULL;
    for(unsigned n=0;n<pages;++n) {
        if(!pa[n]||!aligned(pa[n])||!fits(pa[n],GS_PAGE,d->tree.cfg.pa_bits)||
           overlap(pa[n],GS_PAGE,d->tree.cfg.table_pa,sizeof(struct gs_arena)))return LG3D_RANGE;
        const uint64_t *pte=const_leaf(&d->tree,d->tree.cfg.iova_base+(uint64_t)(first+n)*GS_PAGE);
        if(!pte||*pte)return LG3D_STATE;
    }
    d->state=LG3D_BUSY_STATE;unsigned reserved=0;
    for(;reserved<pages;++reserved) {
        unsigned at=first+reserved;d->pa[at]=pa[reserved];d->owner[at]=(uint16_t)(ri+1u);
        if(!lg3d_reserve_pa(d,at)) {d->pa[at]=0;d->owner[at]=0;e=LG3D_ALIAS;goto rollback;}
    }
    for(unsigned n=0;n<pages;++n)
        if(d->ops.data_sync(d->ops.cookie,pa[n],GS_PAGE,LG3D_TO_GPU)) {e=LG3D_IO;goto rollback;}
    struct lg3d_record *r=&d->record[ri];
    r->map=(struct lg3d_mapping){d->next_token++,d->tree.cfg.iova_base+(uint64_t)first*GS_PAGE,
        (uint64_t)pages*GS_PAGE,d->generation,LG3D_UNCERTAIN};
    r->first=first;r->pages=pages;r->permissions=permissions;++d->outstanding;
    *out=r->map;uint32_t dirty=0;
    for(unsigned n=0;n<pages;++n) {
        unsigned table=0;uint64_t *pte=leaf(&d->tree,r->map.iova+(uint64_t)n*GS_PAGE,&table);
        *(volatile uint64_t *)pte=page_desc(pa[n],permissions);dirty|=1u<<table;
    }
    if(!lg3d_synchronize(d,dirty)) {d->state=LG3D_QUARANTINED;return LG3D_POISONED;}
    r->map.state=LG3D_MAPPED;*out=r->map;d->state=LG3D_READY;return LG3D_OK;
rollback:
    while(reserved)lg3d_release_pa(d,first+--reserved);
    d->state=LG3D_READY;return e;
}
enum lg3d_status lg3d_unmap(struct lg3d_domain *d,const struct lg3d_mapping *m)
{
    enum lg3d_status e=lg3d_ready(d);if(e)return e;
    struct lg3d_record *r=lg3d_find(d,m);if(!r)return LG3D_NOT_FOUND;
    d->state=LG3D_BUSY_STATE;r->map.state=LG3D_UNCERTAIN;uint32_t dirty=0;
    for(unsigned n=0;n<r->pages;++n) {
        unsigned table=0;uint64_t *pte=leaf(&d->tree,r->map.iova+(uint64_t)n*GS_PAGE,&table);
        if(!pte||(*pte&GS_PTE_ADDRESS)!=d->pa[r->first+n]||(*pte&3u)!=3u)goto poisoned;
    }
    for(unsigned n=0;n<r->pages;++n) {
        unsigned table=0;uint64_t *pte=leaf(&d->tree,r->map.iova+(uint64_t)n*GS_PAGE,&table);
        *(volatile uint64_t *)pte=0;dirty|=1u<<table;
    }
    if(!lg3d_synchronize(d,dirty))goto poisoned;
    for(unsigned n=0;n<r->pages;++n)
        if(d->ops.data_sync(d->ops.cookie,d->pa[r->first+n],GS_PAGE,LG3D_TO_CPU))goto poisoned;
    if(d->ops.barrier(d->ops.cookie))goto poisoned;
    for(unsigned n=0;n<r->pages;++n)lg3d_release_pa(d,r->first+n);
    clear_bytes(r,sizeof(*r));--d->outstanding;d->state=LG3D_READY;return LG3D_OK;
poisoned:
    d->state=LG3D_QUARANTINED;return LG3D_POISONED;
}
enum lg3d_status lg3d_sync(struct lg3d_domain *d,const struct lg3d_mapping *m,
    enum lg3d_direction dir,unsigned access)
{
    enum lg3d_status e=lg3d_ready(d);if(e)return e;
    struct lg3d_record *r=lg3d_find(d,m);if(!r)return LG3D_NOT_FOUND;
    if((dir!=LG3D_TO_GPU&&dir!=LG3D_TO_CPU)||!access||
       (access&~r->permissions))return LG3D_ARGUMENT;
    d->state=LG3D_BUSY_STATE;
    for(unsigned n=0;n<r->pages;++n)
        if(d->ops.data_sync(d->ops.cookie,d->pa[r->first+n],GS_PAGE,dir)) {
            d->state=LG3D_READY;return LG3D_IO;
        }
    int failed=d->ops.barrier(d->ops.cookie);d->state=LG3D_READY;return failed?LG3D_IO:LG3D_OK;
}
enum lg3d_status lg3d_lookup(const struct lg3d_domain *d,uint64_t va,uint64_t *pa,uint64_t *pte)
{
    if(!d||!pa||!pte||!within(va,1,d->tree.cfg.iova_base,LG3D_BYTES)||
       d->state==LG3D_EMPTY||d->state==LG3D_BUSY_STATE)return LG3D_ARGUMENT;
    const uint64_t *p=const_leaf(&d->tree,va);if(!p)return LG3D_STATE;
    if(!*p)return LG3D_NOT_FOUND;
    unsigned at=(unsigned)((va-d->tree.cfg.iova_base)/GS_PAGE);
    if((*p&3u)!=3u||!d->owner[at]||(*p&GS_PTE_ADDRESS)!=d->pa[at])return LG3D_STATE;
    *pte=*p;*pa=(*p&GS_PTE_ADDRESS)+(va&(GS_PAGE-1u));return LG3D_OK;
}
enum lg3d_status lg3d_query(const struct lg3d_domain *d,uint64_t token,struct lg3d_mapping *out)
{
    if(!d||!out)return LG3D_ARGUMENT;
    for(unsigned n=0;n<LG3D_MAX_MAPS;++n)
        if(d->record[n].map.state&&d->record[n].map.token==token) {*out=d->record[n].map;return LG3D_OK;}
    return LG3D_NOT_FOUND;
}
unsigned lg3d_outstanding(const struct lg3d_domain *d) { return d?d->outstanding:0; }
enum lg3d_state lg3d_get_state(const struct lg3d_domain *d) { return d?d->state:LG3D_EMPTY; }
enum lg3d_status lg3d_collect(lg3d_segment_fn query,void *cookie,uint64_t bytes,
    unsigned bits,uint64_t *out,unsigned capacity,unsigned *count)
{
    if(count)*count=0;
    if(!query||!out||!count||!bytes||bytes>LG3D_BYTES||(bytes&(LG3D_VM_PAGE-1u))||
       !pa_width(bits)||capacity<bytes/GS_PAGE)return LG3D_ARGUMENT;
    unsigned n=0;uint64_t off=0;
    while(off<bytes) {
        uint64_t length=0,pa=query(cookie,off,&length);
        if(!pa||!length||(pa&(LG3D_VM_PAGE-1u)))return LG3D_RANGE;
        if(length>bytes-off)length=bytes-off;
        if((length&(LG3D_VM_PAGE-1u))||!fits(pa,length,bits))return LG3D_RANGE;
        for(uint64_t p=0;p<length;p+=GS_PAGE)out[n++]=pa+p;
        off+=length;
    }
    *count=n;return LG3D_OK;
}
