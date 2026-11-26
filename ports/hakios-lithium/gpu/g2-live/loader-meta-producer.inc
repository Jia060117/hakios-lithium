/* SPDX-License-Identifier: BSD-2-Clause
 * Include AFTER loader-producer.inc. Reuse exactly its captured raw map;
 * no allocation, new GetMemoryMap, code/metadata data access, or SMC.
 */
#include "g2-meta.h"
#include "g2-meta.c"
static struct lg2m_receipt lg2m_loader_receipt;
static struct lg2m_diagnostic lg2m_loader_diagnostic={
    .magic=LG2M_DIAG_MAGIC,.version=1u,.bytes=sizeof(struct lg2m_diagnostic),
    .stage=LG2M_DIAG_NOT_ATTEMPTED,.cover_type=UINT64_MAX,.cover_pa=UINT64_MAX,
    .cover_pages=UINT64_MAX,.cover_attribute=UINT64_MAX};
static unsigned lg2m_loader_attempted;
static int lg2m_loader_capture(unsigned enabled)
{
    if(!enabled || lg2m_loader_attempted)return 0;
    lg2m_loader_attempted=1u;
    uint64_t par[4];
    for(unsigned i=0;i<4u;++i)par[i]=lg2l_loader_par(LG2M_META_PA+4096u*i);
    if(!lg2m_receipt_build_diagnostic(&lg2m_loader_receipt,&lg2l_loader_receipt,
        lg2l_loader_map,(size_t)lg2l_loader_receipt.map_bytes,par,&lg2m_loader_diagnostic)) {
        print("LITHIUM GPU g2 metadata declined stage=%x detail=%x cover_type=%x cover_pa=%x cover_pages=%x overlaps=%x fingerprint=%x par0=%x par1=%x par2=%x par3=%x\n",
            lg2m_loader_diagnostic.stage,lg2m_loader_diagnostic.detail,
            lg2m_loader_diagnostic.cover_type,lg2m_loader_diagnostic.cover_pa,
            lg2m_loader_diagnostic.cover_pages,lg2m_loader_diagnostic.overlaps,
            lg2m_loader_diagnostic.map_fingerprint,par[0],par[1],par[2],par[3]);
        return 0;
    }
    print("LITHIUM GPU g2 metadata cookie=%x pa=%x bytes=%x fingerprint=%x par0=%x par1=%x par2=%x par3=%x\n",
        lg2m_loader_receipt.boot_cookie,LG2M_META_PA,LG2M_META_BYTES,
        lg2m_loader_receipt.map_fingerprint,par[0],par[1],par[2],par[3]);
    return 1;
}
/* Immediately after successful lg2l_loader_capture: lg2m_loader_capture(1u).
 * In /chosen publish LG2M_PROPERTY only if it succeeded.
 * In /chosen/memory-map publish "GPU-ZAP-Metadata" as two u64 PA/bytes.
 * Do not publish any metadata property when capture declined.
 */
