/* SPDX-License-Identifier: MIT
 * Independent, current-boot UFS finalization receipt. This does not replace
 * the existing normal-return/USB/SMP contract. No reset permission by itself.
 * Little endian, 64 bytes, aligned 64. The loader seeds only an empty template.
 */
#ifndef LITHIUM_UFS_RETURN_RECEIPT_H
#define LITHIUM_UFS_RETURN_RECEIPT_H
#include "contract.h"
#define LUR_SIZE 64u
#define LUR_MAGIC UINT64_C(0x313054455246554c) /* "LUFRET01" */
#define LUR_VERSION 1u
#define LUR_UNMOUNTED 1u
#define LUR_JOURNAL_OK 2u
#define LUR_SEALED 4u
#define LUR_COMPLETE 7u
#define LUR_INITIALIZER { \
    [0]=0x4c,[1]=0x55,[2]=0x46,[3]=0x52,[4]=0x45,[5]=0x54,[6]=0x30,[7]=0x31, \
    [8]=1,[12]=64 }

struct lur_result {
    uint32_t flags, flush_request, data_dev, error;
    uint64_t cookie, generation;
};
static inline int
lur_initial(const void *wire, uint64_t cookie)
{
    if (!wire) return 0;
    const volatile uint8_t *p=wire;
    if (rfbt_get64(p)!=LUR_MAGIC || rfbt_get32(p+8)!=LUR_VERSION ||
        rfbt_get32(p+12)!=LUR_SIZE || rfbt_get64(p+24)!=cookie) return 0;
    for (unsigned i=16;i<LUR_SIZE;i++)
        if ((i<24 || i>=32) && p[i]!=0) return 0;
    return 1;
}
static inline int
lur_seed(void *wire, uint64_t cookie)
{
    if (!cookie || !lur_initial(wire,0)) return 0;
    rfbt_put64((volatile uint8_t *)wire+24,cookie);
    return 1;
}
static inline int
lur_finish(void *wire,uint64_t cookie,uint32_t dev,uint32_t request)
{
    if (!cookie || !request || dev==UINT32_MAX || !lur_initial(wire,cookie)) return 0;
    volatile uint8_t *p=wire;
    rfbt_put32(p+16,LUR_COMPLETE);
    rfbt_put32(p+20,request);
    rfbt_put64(p+32,cookie);
    rfbt_put32(p+40,dev);
    const uint32_t crc=rfbt_crc32(p,56);
    rfbt_put32(p+56,crc);
    rfbt_put32(p+60,~crc);
    return 1;
}
static inline int
lur_decode(const void *wire,uint64_t cookie,struct lur_result *out)
{
    if (!wire || !out || !cookie || rfbt_ranges_overlap(wire,LUR_SIZE,out,sizeof(*out))) return 0;
    const volatile uint8_t *p=wire;
    if (rfbt_get64(p)!=LUR_MAGIC || rfbt_get32(p+8)!=LUR_VERSION ||
        rfbt_get32(p+12)!=LUR_SIZE || rfbt_get32(p+16)!=LUR_COMPLETE ||
        !rfbt_get32(p+20) || rfbt_get64(p+24)!=cookie ||
        rfbt_get64(p+32)!=cookie || rfbt_get32(p+40)==UINT32_MAX ||
        rfbt_get32(p+44)!=0 || rfbt_get64(p+48)!=0 ||
        rfbt_get32(p+60)!=~rfbt_get32(p+56) ||
        rfbt_get32(p+56)!=rfbt_crc32(p,56)) return 0;
    out->flags=rfbt_get32(p+16);
    out->flush_request=rfbt_get32(p+20);
    out->cookie=rfbt_get64(p+24);
    out->generation=rfbt_get64(p+32);
    out->data_dev=rfbt_get32(p+40);
    out->error=rfbt_get32(p+44);
    return 1;
}
#endif
