#include "smmu.h"

static void clear_bytes(void *p, size_t n) {
    unsigned char *b=(unsigned char *)p;
    while (n--) *b++=0;
}
static int aligned(uint64_t v) { return !(v & (GS_PAGE-1u)); }
static int within(uint64_t v, uint64_t n, uint64_t base, uint64_t bytes) {
    return n && n<=bytes && v>=base && v-base<=bytes-n;
}
static int fits(uint64_t pa, uint64_t bytes, unsigned bits) {
    return within(pa,bytes,0,UINT64_C(1)<<bits);
}
static int pa_width(unsigned bits) {
    return bits==32 || bits==36 || bits==40 || bits==42 || bits==44 || bits==48;
}
static int overlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) {
    /* Only used after bounded, non-overflowing PA ranges were validated. */
    return a<b+bn && b<a+an;
}
static unsigned slot(uint64_t va, unsigned level) {
    return (unsigned)((va >> (12u+9u*(3u-level))) & 511u);
}
static uint64_t table_address(const struct gs_domain *d, unsigned index) {
    return d->cfg.table_pa+(uint64_t)index*GS_PAGE;
}
static int table_index(const struct gs_domain *d, uint64_t desc, unsigned *out) {
    uint64_t pa=desc & GS_PTE_ADDRESS;
    if ((desc & 3u)!=3u || pa<d->cfg.table_pa) return 0;
    uint64_t n=(pa-d->cfg.table_pa)/GS_PAGE;
    if (n>=d->tables_used) return 0;
    *out=(unsigned)n;
    return 1;
}
static uint64_t *leaf(struct gs_domain *d, uint64_t va, unsigned *table_out) {
    unsigned table=0;
    for (unsigned l=d->start_level;l<3;++l) {
        unsigned child;
        if (!table_index(d,d->arena->pte[table][slot(va,l)],&child)) return NULL;
        table=child;
    }
    if (table_out) *table_out=table;
    return &d->arena->pte[table][slot(va,3)];
}
static const uint64_t *const_leaf(const struct gs_domain *d, uint64_t va) {
    unsigned table=0;
    for (unsigned l=d->start_level;l<3;++l) {
        unsigned child;
        if (!table_index(d,d->arena->pte[table][slot(va,l)],&child)) return NULL;
        table=child;
    }
    return &d->arena->pte[table][slot(va,3)];
}
static int physical_busy(const struct gs_domain *d, unsigned p) {
    return (int)((d->physical_used[p/64] >> (p%64)) & 1u);
}
static void physical_set(struct gs_domain *d, unsigned p, int used) {
    uint64_t mask=UINT64_C(1)<<(p%64);
    if (used) d->physical_used[p/64]|=mask;
    else d->physical_used[p/64]&=~mask;
}
static int permissions_ok(unsigned p) {
    /* Write-only cannot be expressed with S1 AP; require explicit READ. */
    return (p & GS_READ) && !(p & ~(GS_READ|GS_WRITE|GS_CACHE|GS_PRIVILEGED));
}
static int callbacks_ok(const struct gs_callbacks *cb) {
    return cb && cb->begin && cb->data_sync && cb->table_clean &&
        cb->write_barrier && cb->invalidate_context && cb->tlb_sync && cb->end;
}
static uint64_t page_desc(uint64_t pa, unsigned permissions) {
    uint64_t desc=pa|3u|(UINT64_C(1)<<10)|(UINT64_C(1)<<11)|(UINT64_C(3)<<53);
    if (!(permissions & GS_WRITE)) desc|=UINT64_C(1)<<7;
    if (!(permissions & GS_PRIVILEGED)) desc|=UINT64_C(1)<<6;
    if (permissions & GS_CACHE) desc|=(UINT64_C(1)<<2)|(UINT64_C(3)<<8);
    else desc|=UINT64_C(2)<<8;
    return desc;
}
static enum gs_status ready(const struct gs_domain *d) {
    if (!d) return GS_INVALID;
    if (d->state==GS_QUARANTINED) return GS_POISONED;
    return d->state==GS_READY ? GS_OK : GS_BAD_STATE;
}
static int range_ok(const struct gs_domain *d, uint64_t iova, unsigned pages) {
    return aligned(iova) && pages && pages<=GS_POOL_PAGES &&
        within(iova,(uint64_t)pages*GS_PAGE,d->cfg.iova_base,GS_POOL_BYTES);
}
static enum gs_status begin(struct gs_domain *d, enum gs_op op, uint64_t va, uint64_t bytes) {
    d->state=GS_BUSY; /* Prevent synchronous reentry from any callback. */
    if (d->cb.begin(d->cb.cookie,op,va,bytes)) {
        d->state=(op==GS_PUBLISH)?GS_OFFLINE:GS_READY;
        return GS_CALLBACK;
    }
    return GS_OK;
}
static enum gs_status end(struct gs_domain *d, enum gs_op op, int committed, int changed) {
    /* Keep BUSY during end() too. Any post-PTE failure has unknown visibility. */
    d->cb.end(d->cb.cookie,op,committed);
    if (!committed && changed) { d->state=GS_QUARANTINED; return GS_POISONED; }
    d->state=(op==GS_PUBLISH && !committed)?GS_OFFLINE:GS_READY;
    return committed?GS_OK:GS_CALLBACK;
}
static int synchronize(struct gs_domain *d, uint32_t dirty, uint64_t va, uint64_t bytes) {
    for (unsigned t=0;t<d->tables_used;++t)
        if ((dirty & (UINT32_C(1)<<t)) &&
            d->cb.table_clean(d->cb.cookie,d->arena->pte[t],table_address(d,t),GS_PAGE))
            return 0;
    return !d->cb.write_barrier(d->cb.cookie) &&
           !d->cb.invalidate_context(d->cb.cookie,va,bytes) &&
           !d->cb.tlb_sync(d->cb.cookie);
}
enum gs_status gs_init(struct gs_domain *d, struct gs_arena *arena,
                       const struct gs_config *cfg, const struct gs_callbacks *cb) {
    if (!d || !arena || !cfg) return GS_INVALID;
    if (d->state!=GS_EMPTY) return GS_BAD_STATE;
    if (((uintptr_t)arena & (GS_PAGE-1u)) || !aligned(cfg->iova_base) ||
        !aligned(cfg->pool_pa) || !aligned(cfg->table_pa) ||
        cfg->iova_bits<25 || cfg->iova_bits>48 || !pa_width(cfg->pa_bits) ||
        !cfg->table_capacity || cfg->table_capacity>GS_TABLES ||
        (cb && !callbacks_ok(cb)))
        return GS_INVALID;
    uint64_t table_bytes=(uint64_t)cfg->table_capacity*GS_PAGE;
    if (!fits(cfg->iova_base,GS_POOL_BYTES,cfg->iova_bits) ||
        !fits(cfg->pool_pa,GS_POOL_BYTES,cfg->pa_bits) ||
        !fits(cfg->table_pa,table_bytes,cfg->pa_bits) ||
        overlap(cfg->pool_pa,GS_POOL_BYTES,cfg->table_pa,table_bytes)) return GS_RANGE;
    /* Permit configuration supplied from a zero-state domain's own fields. */
    struct gs_config config_copy=*cfg;
    struct gs_callbacks callbacks_copy={0};
    if (cb) callbacks_copy=*cb;
    clear_bytes(d,sizeof(*d));
    clear_bytes(arena,sizeof(*arena));
    d->cfg=config_copy; d->cb=callbacks_copy; d->arena=arena; d->tables_used=1;
    cfg=&d->cfg;
    unsigned levels=(cfg->iova_bits-12u+8u)/9u;
    d->start_level=4u-levels;
    for (unsigned p=0;p<GS_POOL_PAGES;++p) {
        uint64_t va=cfg->iova_base+(uint64_t)p*GS_PAGE;
        unsigned table=0;
        for (unsigned l=d->start_level;l<3;++l) {
            uint64_t *entry=&arena->pte[table][slot(va,l)];
            if (!*entry) {
                if (d->tables_used==cfg->table_capacity) {
                    clear_bytes(d,sizeof(*d)); /* Partial tables remain offline/unpublished. */
                    return GS_NO_TABLES;
                }
                unsigned child=d->tables_used++;
                *entry=table_address(d,child)|3u;
                table=child;
            } else if (!table_index(d,*entry,&table)) {
                clear_bytes(d,sizeof(*d)); return GS_CORRUPT;
            }
        }
    }
    d->state=GS_OFFLINE;
    return GS_OK;
}
enum gs_status gs_build_pool(struct gs_domain *d, struct gs_arena *arena,
                            const struct gs_config *cfg, unsigned permissions) {
    if (!permissions_ok(permissions)) return GS_INVALID;
    enum gs_status rc=gs_init(d,arena,cfg,NULL);
    if (rc!=GS_OK) return rc;
    for (unsigned p=0;p<GS_POOL_PAGES;++p) {
        uint64_t *entry=leaf(d,d->cfg.iova_base+(uint64_t)p*GS_PAGE,NULL);
        /* gs_init constructed every path; no external callback can change it. */
        *entry=page_desc(d->cfg.pool_pa+(uint64_t)p*GS_PAGE,permissions);
        physical_set(d,p,1);
    }
    d->mapped_pages=GS_POOL_PAGES;
    return GS_OK;
}
enum gs_status gs_bind_callbacks(struct gs_domain *d, const struct gs_callbacks *cb) {
    if (!d || !callbacks_ok(cb)) return GS_INVALID;
    if (d->state==GS_QUARANTINED) return GS_POISONED;
    if (d->state!=GS_OFFLINE) return GS_BAD_STATE;
    d->cb=*cb;
    return GS_OK;
}
enum gs_status gs_layout(const struct gs_domain *d, struct gs_layout *out) {
    if (!d || !out) return GS_INVALID;
    if (d->state==GS_EMPTY || d->state==GS_BUSY) return GS_BAD_STATE;
    out->root_pa=d->cfg.table_pa; out->mair=GS_MAIR;
    out->start_level=d->start_level; out->levels=4u-d->start_level; out->t0sz=64u-d->cfg.iova_bits;
    out->tables_used=d->tables_used; out->mapped_pages=d->mapped_pages;
    out->table_bytes=(uint64_t)d->tables_used*GS_PAGE;
    switch (d->cfg.pa_bits) {
    case 32: out->ips=0; break; case 36: out->ips=1; break; case 40: out->ips=2; break;
    case 42: out->ips=3; break; case 44: out->ips=4; break; default: out->ips=5; break;
    }
    return GS_OK;
}
enum gs_status gs_table_view(const struct gs_domain *d, unsigned index, struct gs_table_view *out) {
    if (!d || !out) return GS_INVALID;
    if (d->state==GS_EMPTY || d->state==GS_BUSY) return GS_BAD_STATE;
    if (index>=d->tables_used) return GS_RANGE;
    out->cpu=d->arena->pte[index]; out->pa=table_address(d,index); out->bytes=GS_PAGE;
    return GS_OK;
}
enum gs_status gs_publish(struct gs_domain *d) {
    if (!d) return GS_INVALID;
    if (d->state==GS_QUARANTINED) return GS_POISONED;
    if (d->state!=GS_OFFLINE) return GS_BAD_STATE;
    if (!callbacks_ok(&d->cb)) return GS_BAD_STATE;
    enum gs_status rc=begin(d,GS_PUBLISH,d->cfg.iova_base,GS_POOL_BYTES);
    if (rc!=GS_OK) return rc;
    /* Prebuilt leaves have never been published. Complete their CPU data
     * maintenance before any table visibility. Failure here is still retryable:
     * no table_clean/barrier/TLB callback has run and no PTE was changed.
     */
    for (unsigned p=0;p<GS_POOL_PAGES;) {
        if (!physical_busy(d,p)) { ++p; continue; }
        unsigned first=p++;
        while (p<GS_POOL_PAGES && physical_busy(d,p)) ++p;
        if (d->cb.data_sync(d->cb.cookie,d->cfg.pool_pa+(uint64_t)first*GS_PAGE,
                           (uint64_t)(p-first)*GS_PAGE,GS_TO_DEVICE))
            return end(d,GS_PUBLISH,0,0);
    }
    int ok=synchronize(d,(UINT32_C(1)<<d->tables_used)-1u,d->cfg.iova_base,GS_POOL_BYTES);
    /* Even publish failure is quarantined: callbacks may have made tables visible. */
    return end(d,GS_PUBLISH,ok,1);
}
enum gs_status gs_map(struct gs_domain *d, uint64_t va, unsigned pool_page,
                     unsigned pages, unsigned permissions) {
    enum gs_status rc=ready(d);
    if (rc!=GS_OK) return rc;
    if (!permissions_ok(permissions)) return GS_INVALID;
    if (!range_ok(d,va,pages) || pool_page>=GS_POOL_PAGES || pages>GS_POOL_PAGES-pool_page) return GS_RANGE;
    for (unsigned p=0;p<pages;++p) {
        uint64_t *entry=leaf(d,va+(uint64_t)p*GS_PAGE,NULL);
        if (!entry) return GS_CORRUPT;
        if (*entry) return GS_EXISTS;
        if (physical_busy(d,pool_page+p)) return GS_PHYS_BUSY;
    }
    uint64_t bytes=(uint64_t)pages*GS_PAGE, pa=d->cfg.pool_pa+(uint64_t)pool_page*GS_PAGE;
    rc=begin(d,GS_MAP,va,bytes); if (rc!=GS_OK) return rc;
    if (d->cb.data_sync(d->cb.cookie,pa,bytes,GS_TO_DEVICE)) return end(d,GS_MAP,0,0);
    uint32_t dirty=0;
    for (unsigned p=0;p<pages;++p) {
        unsigned table=0;
        uint64_t *entry=leaf(d,va+(uint64_t)p*GS_PAGE,&table);
        *(volatile uint64_t *)entry=page_desc(pa+(uint64_t)p*GS_PAGE,permissions);
        dirty|=UINT32_C(1)<<table;
    }
    if (!synchronize(d,dirty,va,bytes)) return end(d,GS_MAP,0,1);
    for (unsigned p=0;p<pages;++p) physical_set(d,pool_page+p,1);
    d->mapped_pages+=pages;
    return end(d,GS_MAP,1,1);
}
enum gs_status gs_lookup(const struct gs_domain *d, uint64_t va, uint64_t *pa, uint64_t *descriptor) {
    if (!d || !pa || !descriptor) return GS_INVALID;
    if (d->state==GS_EMPTY || d->state==GS_BUSY) return GS_BAD_STATE;
    if (!within(va,1,d->cfg.iova_base,GS_POOL_BYTES)) return GS_RANGE;
    const uint64_t *entry=const_leaf(d,va);
    if (!entry) return GS_CORRUPT;
    if (!*entry) return GS_NOT_MAPPED;
    if ((*entry & 3u)!=3u) return GS_CORRUPT;
    uint64_t address=*entry & GS_PTE_ADDRESS;
    if (!within(address,GS_PAGE,d->cfg.pool_pa,GS_POOL_BYTES)) return GS_CORRUPT;
    *descriptor=*entry; *pa=address+(va & (GS_PAGE-1u));
    return GS_OK;
}
enum gs_status gs_unmap(struct gs_domain *d, uint64_t va, unsigned pages) {
    enum gs_status rc=ready(d);
    if (rc!=GS_OK) return rc;
    if (!range_ok(d,va,pages)) return GS_RANGE;
    for (unsigned p=0;p<pages;++p) {
        const uint64_t *entry=const_leaf(d,va+(uint64_t)p*GS_PAGE);
        if (!entry) return GS_CORRUPT;
        if (!*entry) return GS_NOT_MAPPED;
        if ((*entry&3u)!=3u) return GS_CORRUPT;
        uint64_t pa=*entry&GS_PTE_ADDRESS;
        if (!within(pa,GS_PAGE,d->cfg.pool_pa,GS_POOL_BYTES)) return GS_CORRUPT;
        unsigned phys=(unsigned)((pa-d->cfg.pool_pa)/GS_PAGE);
        if (!physical_busy(d,phys)) return GS_CORRUPT;
        d->retiring[p]=(uint16_t)phys;
    }
    uint64_t bytes=(uint64_t)pages*GS_PAGE;
    rc=begin(d,GS_UNMAP,va,bytes); if (rc!=GS_OK) return rc;
    uint32_t dirty=0;
    for (unsigned p=0;p<pages;++p) {
        unsigned table=0;
        uint64_t *entry=leaf(d,va+(uint64_t)p*GS_PAGE,&table);
        *(volatile uint64_t *)entry=0; dirty|=UINT32_C(1)<<table;
    }
    if (!synchronize(d,dirty,va,bytes)) return end(d,GS_UNMAP,0,1);
    for (unsigned p=0;p<pages;++p)
        if (d->cb.data_sync(d->cb.cookie,
            d->cfg.pool_pa+(uint64_t)d->retiring[p]*GS_PAGE,GS_PAGE,GS_TO_CPU))
            return end(d,GS_UNMAP,0,1);
    for (unsigned p=0;p<pages;++p) physical_set(d,d->retiring[p],0);
    d->mapped_pages-=pages;
    return end(d,GS_UNMAP,1,1);
}
enum gs_status gs_map_pool(struct gs_domain *d, unsigned permissions) {
    if (!d) return GS_INVALID;
    return gs_map(d,d->cfg.iova_base,0,GS_POOL_PAGES,permissions);
}
enum gs_status gs_alloc(struct gs_domain *d, unsigned pages, unsigned permissions,
                       uint64_t *iova, unsigned *pool_page) {
    enum gs_status rc=ready(d);
    if (rc!=GS_OK) return rc;
    if (!iova || !pool_page || !permissions_ok(permissions)) return GS_INVALID;
    if (!pages || pages>GS_POOL_PAGES) return GS_RANGE;
    unsigned va_start=0, pa_start=0, va_run=0, pa_run=0;
    for (unsigned p=0;p<GS_POOL_PAGES && va_run<pages;++p) {
        uint64_t *entry=leaf(d,d->cfg.iova_base+(uint64_t)p*GS_PAGE,NULL);
        if (!entry) return GS_CORRUPT;
        if (*entry) va_run=0; else if (!va_run++) va_start=p;
    }
    for (unsigned p=0;p<GS_POOL_PAGES && pa_run<pages;++p) {
        if (physical_busy(d,p)) pa_run=0; else if (!pa_run++) pa_start=p;
    }
    if (va_run<pages || pa_run<pages) return GS_PHYS_BUSY;
    uint64_t address=d->cfg.iova_base+(uint64_t)va_start*GS_PAGE;
    rc=gs_map(d,address,pa_start,pages,permissions);
    if (rc==GS_OK) { *iova=address; *pool_page=pa_start; }
    return rc;
}
void gs_fault_decode(uint32_t fsr, uint32_t syn, uint32_t syn1, uint32_t cb,
                     uint64_t far, struct gs_fault *f) {
    if (!f) return;
    clear_bytes(f,sizeof(*f));
    f->fsr=fsr; f->fsynr0=syn; f->fsynr1=syn1; f->cbfrsynra=cb; f->far=far;
    f->sid=(uint16_t)cb; f->level=(uint8_t)(syn&3u);
    f->s1_context=(uint8_t)(syn>>16); f->format=(uint8_t)((fsr>>9)&3u);
#define FLAG(field,value,bit) f->field=(uint8_t)(((value)>>(bit))&1u)
    FLAG(translation,fsr,1); FLAG(access_flag,fsr,2); FLAG(permission,fsr,3);
    FLAG(external,fsr,4); FLAG(tlb_multiconflict,fsr,5); FLAG(tlb_lock,fsr,6);
    FLAG(address_size,fsr,7); FLAG(unsupported_upstream,fsr,8);
    FLAG(stalled,fsr,30); FLAG(multiple,fsr,31);
    FLAG(write,syn,4); FLAG(privileged,syn,5); FLAG(instruction,syn,6);
    FLAG(nonsecure,syn,8); FLAG(table_walk,syn,10); FLAG(afr,syn,11);
#undef FLAG
    f->unknown_fsr=fsr & ~UINT32_C(0xc00007fe);
    f->has_fault=(uint8_t)!!(fsr & UINT32_C(0xc00001fe));
}
