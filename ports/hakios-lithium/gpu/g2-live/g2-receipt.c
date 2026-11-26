/* SPDX-License-Identifier: BSD-2-Clause */
#include "g2-reservation.h"
static uint64_t read_le(const uint8_t *p,unsigned n)
{
    uint64_t v=0;
    for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(8u*i);
    return v;
}
int lg2l_par_normal_nc(uint64_t par,uint64_t pa)
{
    return !(par&1u) && (par>>56)==0x44u &&
        (par&UINT64_C(0x0000fffffffff000))==(pa&~UINT64_C(4095));
}
int lg2l_receipt_valid(const struct lg2l_receipt *r)
{
    return r && r->magic==LG2L_RECEIPT_MAGIC && r->version==1u &&
        r->bytes==sizeof(*r) && r->boot_cookie &&
        r->descriptor_version==1u && r->descriptor_bytes>=40u &&
        r->descriptor_bytes<=4096u && r->map_bytes &&
        r->map_bytes<=65536u && !(r->map_bytes%r->descriptor_bytes) &&
        r->code_pa==LG2L_CODE_PA && r->code_bytes==LG2L_CODE_BYTES &&
        r->kernel_pa==LG2L_KERNEL_PA && r->kernel_bytes==LG2L_KERNEL_BYTES &&
        r->flags==LG2L_RECEIPT_FLAGS &&
        lg2l_par_normal_nc(r->par[0],LG2L_CODE_PA) &&
        lg2l_par_normal_nc(r->par[1],LG2L_CODE_PA+4096u);
}
int lg2l_receipt_build(struct lg2l_receipt *r,const void *map,size_t bytes,
                       uint64_t key,size_t stride,uint32_t version,
                       uint64_t cookie,const uint64_t par[2])
{
    if(!r)return 0;
    *r=(struct lg2l_receipt){0};
    if(!map || !par || !cookie || version!=1u || stride<40u ||
       stride>4096u || !bytes || bytes>65536u || bytes%stride ||
       !lg2l_par_normal_nc(par[0],LG2L_CODE_PA) ||
       !lg2l_par_normal_nc(par[1],LG2L_CODE_PA+4096u))return 0;
    const uint8_t *m=map;
    unsigned code=0,kernel=0;
    uint64_t fingerprint=UINT64_C(14695981039346656037);
    for(size_t i=0;i<bytes;++i) {
        fingerprint^=m[i]; fingerprint*=UINT64_C(1099511628211);
    }
    for(size_t off=0;off<bytes;off+=stride) {
        uint64_t type=read_le(m+off,4),pa=read_le(m+off+8,8);
        uint64_t pages=read_le(m+off+24,8);
        if(!pages || pages>UINT64_MAX/4096u || pa>UINT64_MAX-pages*4096u)
            return 0;
        uint64_t end=pa+pages*4096u;
        if(pa<LG2L_CODE_PA+LG2L_CODE_BYTES && LG2L_CODE_PA<end) {
            if(type!=0u || pa!=LG2L_CODE_PA || end!=LG2L_CODE_PA+LG2L_CODE_BYTES)
                return 0;
            ++code;
        }
        if(pa<LG2L_KERNEL_PA+LG2L_KERNEL_BYTES && LG2L_KERNEL_PA<end) {
            if(type!=2u || pa!=LG2L_KERNEL_PA || end!=LG2L_KERNEL_PA+LG2L_KERNEL_BYTES)
                return 0;
            ++kernel;
        }
    }
    if(code!=1u || kernel!=1u)return 0;
    *r=(struct lg2l_receipt){LG2L_RECEIPT_MAGIC,1u,sizeof(*r),cookie,
        key,bytes,stride,version,LG2L_CODE_PA,LG2L_CODE_BYTES,
        LG2L_KERNEL_PA,LG2L_KERNEL_BYTES,fingerprint,{par[0],par[1]},
        LG2L_RECEIPT_FLAGS};
    return lg2l_receipt_valid(r);
}
