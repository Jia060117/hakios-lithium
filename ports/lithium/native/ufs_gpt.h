/* Bounded GPT parsing shared by the native driver and its host validation.
 * Only the primary GPT on a 4096-byte logical-block disk is accepted.
 */
#ifndef LITHIUM_UFS_GPT_H
#define LITHIUM_UFS_GPT_H
struct lithium_gpt {
    uint64_t entries_lba, first_usable, last_usable;
    uint32_t count, bytes, entries_crc;
};
static uint32_t lg_le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t lg_le64(const uint8_t *p)
{ return (uint64_t)lg_le32(p) | (uint64_t)lg_le32(p+4)<<32; }
static uint32_t lg_crc(const uint8_t *p, uint32_t n, int header)
{
    uint32_t crc = 0xffffffffu;
    for (uint32_t i=0; i<n; ++i) {
        crc ^= (header && i>=16 && i<20) ? 0 : p[i];
        for (unsigned j=0; j<8; ++j) crc = (crc>>1) ^ (0xedb88320u & (0u-(crc&1)));
    }
    return ~crc;
}
static int lg_header(const uint8_t *b, uint64_t blocks, struct lithium_gpt *g)
{
    const uint8_t sig[8] = {'E','F','I',' ','P','A','R','T'};
    for (unsigned i=0;i<8;++i) if (b[i]!=sig[i]) return 1;
    uint32_t size=lg_le32(b+12), count=lg_le32(b+80);
    if (blocks<8 || lg_le32(b+8)!=0x10000 || size<92 || size>4096 ||
        lg_le32(b+20)!=0 || lg_crc(b,size,1)!=lg_le32(b+16) ||
        lg_le64(b+24)!=1 || lg_le64(b+32)!=blocks-1 ||
        lg_le32(b+84)!=128 || count==0 || count>(1u<<20)/128) return 2;
    g->count=count; g->bytes=count*128; g->entries_lba=lg_le64(b+72);
    g->first_usable=lg_le64(b+40); g->last_usable=lg_le64(b+48);
    g->entries_crc=lg_le32(b+88);
    uint64_t table_blocks=(g->bytes+4095u)/4096u;
    if (g->entries_lba<2 || g->entries_lba>=blocks ||
        table_blocks>blocks-g->entries_lba ||
        g->first_usable<g->entries_lba+table_blocks ||
        g->last_usable<g->first_usable ||
        g->last_usable>=blocks-1-table_blocks) return 3;
    return 0;
}
static int lg_used(const uint8_t *e)
{ uint8_t x=0; for(unsigned i=0;i<16;++i) x|=e[i]; return x!=0; }
static int lg_name(const uint8_t *e, const char *want)
{
    for(unsigned i=0;i<36;++i) {
        uint16_t ch=(uint16_t)e[56+2*i] | (uint16_t)e[57+2*i]<<8;
        if (ch!=(uint8_t)want[i]) return 0;
        if (!ch) return 1;
    }
    return 0;
}
static int lg_partition(const uint8_t *b, const struct lithium_gpt *g,
    const char *want, uint64_t *first, uint64_t *count)
{
    if (lg_crc(b,g->bytes,0)!=g->entries_crc) return 4;
    int found=-1;
    for(uint32_t i=0;i<g->count;++i) {
        const uint8_t *e=b+i*128;
        if (!lg_used(e)) continue;
        uint64_t a=lg_le64(e+32), z=lg_le64(e+40);
        if (a<g->first_usable || z<a || z>g->last_usable) return 5;
        if (lg_name(e,want)) {
            if(found>=0) return 6;
            found=(int)i; *first=a; *count=z-a+1;
        }
    }
    if(found<0) return 7;
    for(uint32_t i=0;i<g->count;++i) {
        const uint8_t *e=b+i*128;
        if((int)i==found || !lg_used(e)) continue;
        if(lg_le64(e+32)<*first+*count && lg_le64(e+40)>=*first) return 8;
    }
    return 0;
}
#endif
