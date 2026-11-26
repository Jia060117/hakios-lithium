/* Host-only table export example. All addresses/capabilities are explicit model
 * inputs, never read from hardware. The same gs_build_pool API is freestanding.
 */
#include "smmu.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static struct gs_domain domain;
static struct gs_arena arena;

static int number(const char *text, uint64_t *out) {
    if (!text || !*text || *text=='-') return 0;
    char *end=NULL;
    errno=0;
    unsigned long long n=strtoull(text,&end,0);
    if (errno || !end || *end) return 0;
    *out=(uint64_t)n;
    return 1;
}
int main(int argc, char **argv) {
    if (argc!=8) {
        fprintf(stderr,"usage: tablegen IOVA_BASE POOL_PA TABLE_PA IOVA_BITS PA_BITS PERMISSIONS OUT_BIN\n");
        return 2;
    }
    uint64_t n[6];
    for (unsigned i=0;i<6;++i)
        if (!number(argv[i+1],&n[i])) { fprintf(stderr,"invalid numeric argument %u\n",i+1); return 2; }
    if (n[3]>UINT_MAX || n[4]>UINT_MAX || n[5]>UINT_MAX) return 2;
    struct gs_config cfg={n[0],n[1],n[2],(unsigned)n[3],(unsigned)n[4],GS_TABLES};
    enum gs_status rc=gs_build_pool(&domain,&arena,&cfg,(unsigned)n[5]);
    if (rc!=GS_OK) { fprintf(stderr,"offline build failed: status=%u\n",(unsigned)rc); return 1; }
    struct gs_layout layout;
    rc=gs_layout(&domain,&layout);
    if (rc!=GS_OK) return 1;
    FILE *out=fopen(argv[7],"wb");
    if (!out) { perror("open table image"); return 1; }
    for (unsigned t=0;t<layout.tables_used;++t) {
        struct gs_table_view view;
        if (gs_table_view(&domain,t,&view)!=GS_OK) { (void)fclose(out); return 1; }
        const uint64_t *entries=(const uint64_t *)view.cpu;
        unsigned char page[GS_PAGE];
        for (unsigned p=0;p<512;++p)
            for (unsigned b=0;b<8;++b) page[p*8+b]=(unsigned char)(entries[p]>>(b*8));
        if (fwrite(page,1,sizeof(page),out)!=sizeof(page)) {
            perror("write table image"); (void)fclose(out); return 1;
        }
    }
    if (fclose(out)) { perror("close table image"); return 1; }
    printf("{\"status\":\"offline-model\",\"state\":\"GS_OFFLINE\","
           "\"iova_base\":\"0x%" PRIx64 "\",\"pool_pa\":\"0x%" PRIx64 "\","
           "\"table_pa\":\"0x%" PRIx64 "\",\"iova_bits\":%u,\"pa_bits\":%u,"
           "\"permissions\":%u,\"root_pa\":\"0x%" PRIx64 "\",\"mair\":\"0x%" PRIx64 "\","
           "\"start_level\":%u,\"t0sz\":%u,\"ips\":%u,\"tables_used\":%u,"
           "\"table_bytes\":%" PRIu64 ",\"mapped_pages\":%u,"
           "\"byte_order\":\"little\",\"callback_calls\":0,\"mmio_calls\":0,"
           "\"device_calls\":0,\"context_owned\":false,\"sid_bound\":false}\n",
           cfg.iova_base,cfg.pool_pa,cfg.table_pa,cfg.iova_bits,cfg.pa_bits,(unsigned)n[5],
           layout.root_pa,layout.mair,layout.start_level,layout.t0sz,layout.ips,
           layout.tables_used,layout.table_bytes,layout.mapped_pages);
    return 0;
}
