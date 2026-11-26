/* SPDX-License-Identifier: BSD-2-Clause */
#include "g2-meta.h"
static uint64_t lg2m_read_le(const uint8_t *p,unsigned n)
{
    uint64_t value=0;
    for(unsigned i=0;i<n;++i)value|=(uint64_t)p[i]<<(i*8u);
    return value;
}
int lg2m_receipt_valid(const struct lg2m_receipt *r,const struct lg2l_receipt *code)
{
    if(!r || !lg2l_receipt_valid(code) || r->magic!=LG2M_MAGIC ||
       r->version!=1u || r->bytes!=sizeof(*r) ||
       r->boot_cookie!=code->boot_cookie ||
       r->map_fingerprint!=code->map_fingerprint ||
       r->metadata_pa!=LG2M_META_PA || r->metadata_bytes!=LG2M_META_BYTES ||
       r->flags!=LG2M_FLAGS || LG2M_META_PA<code->kernel_pa+code->kernel_bytes)
        return 0;
    for(unsigned i=0;i<4u;++i)
        if(r->reserved[i] || !lg2l_par_normal_nc(r->par[i],LG2M_META_PA+4096u*i))
            return 0;
    return 1;
}
int lg2m_receipt_build_diagnostic(struct lg2m_receipt *r,const struct lg2l_receipt *code,
                      const void *map,size_t bytes,const uint64_t par[4],
                      struct lg2m_diagnostic *diagnostic)
{
    struct lg2m_diagnostic local={.magic=LG2M_DIAG_MAGIC,.version=1u,
        .bytes=sizeof(local),.stage=LG2M_DIAG_CODE,.cover_type=UINT64_MAX,
        .cover_pa=UINT64_MAX,.cover_pages=UINT64_MAX,.cover_attribute=UINT64_MAX};
    struct lg2m_diagnostic *d=diagnostic?diagnostic:&local;
    *d=local;
    if(r)*r=(struct lg2m_receipt){0};
    if(code)d->boot_cookie=code->boot_cookie;
    if(par)for(unsigned i=0;i<4u;++i) {
        d->par[i]=par[i];
        if(lg2l_par_normal_nc(par[i],LG2M_META_PA+4096u*i))d->detail|=LG2M_D_PAR(i);
    }
    if(!r || !lg2l_receipt_valid(code))return 0;
    d->detail|=LG2M_D_CODE_VALID;d->stage=LG2M_DIAG_MAP;
    if(!map || !par || bytes!=code->map_bytes)return 0;
    d->detail|=LG2M_D_MAP_BOUNDED;d->stage=LG2M_DIAG_FINGERPRINT;
    const uint8_t *p=map;
    uint64_t fingerprint=UINT64_C(14695981039346656037);
    for(size_t i=0;i<bytes;++i){fingerprint^=p[i];fingerprint*=UINT64_C(1099511628211);}
    d->map_fingerprint=fingerprint;
    unsigned fingerprint_valid=fingerprint==code->map_fingerprint;
    if(fingerprint_valid)d->detail|=LG2M_D_FINGERPRINT;
    d->stage=LG2M_DIAG_DESCRIPTOR;
    unsigned matches=0;
    for(size_t off=0;off<bytes;off+=(size_t)code->descriptor_bytes) {
        uint64_t type=lg2m_read_le(p+off,4),pa=lg2m_read_le(p+off+8,8);
        uint64_t pages=lg2m_read_le(p+off+24,8);
        if(!pages || pages>UINT64_MAX/4096u || pa>UINT64_MAX-pages*4096u) {
            if(!fingerprint_valid)d->stage=LG2M_DIAG_FINGERPRINT;
            return 0;
        }
        uint64_t end=pa+pages*4096u;
        if(pa<LG2M_META_PA+LG2M_META_BYTES && LG2M_META_PA<end) {
            if(!matches) {
                d->cover_type=type;d->cover_pa=pa;d->cover_pages=pages;
                d->cover_attribute=lg2m_read_le(p+off+32,8);
                d->detail|=LG2M_D_COVER_CAPTURED;
            }
            ++matches;
            d->overlaps=matches;
        }
    }
    d->detail|=LG2M_D_MAP_SCANNED;
    if(!fingerprint_valid){d->stage=LG2M_DIAG_FINGERPRINT;return 0;}
    d->stage=LG2M_DIAG_OVERLAPS;
    if(matches!=1u)return 0;
    d->stage=LG2M_DIAG_COVER;
    /* Mu DXE merges adjacent type0 descriptors with identical GCD capability
     * attributes. Only the two source-owned exact shapes are accepted.
     * The extra union range remains XNUP-owned; metadata ownership stays16K.
     */
    if(d->cover_type!=0u ||
       !((d->cover_pa==LG2M_META_PA && d->cover_pages==4u) ||
         (d->cover_pa==LG2M_UNION_PA && d->cover_pages==LG2M_UNION_PAGES)))return 0;
    d->detail|=LG2M_D_COVER_ALLOWED;
    for(unsigned i=0;i<4u;++i)if(!(d->detail&LG2M_D_PAR(i))) {
        d->stage=LG2M_DIAG_PAR0+i;return 0;
    }
    d->stage=LG2M_DIAG_RECEIPT;
    *r=(struct lg2m_receipt){.magic=LG2M_MAGIC,.version=1u,.bytes=sizeof(*r),
        .boot_cookie=code->boot_cookie,.map_fingerprint=code->map_fingerprint,
        .metadata_pa=LG2M_META_PA,.metadata_bytes=LG2M_META_BYTES,.flags=LG2M_FLAGS};
    for(unsigned i=0;i<4u;++i)r->par[i]=par[i];
    if(!lg2m_receipt_valid(r,code)){*r=(struct lg2m_receipt){0};return 0;}
    d->stage=LG2M_DIAG_OK;
    return 1;
}
int lg2m_receipt_build(struct lg2m_receipt *r,const struct lg2l_receipt *code,
                      const void *map,size_t bytes,const uint64_t par[4])
{ return lg2m_receipt_build_diagnostic(r,code,map,bytes,par,0); }
