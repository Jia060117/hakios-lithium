/* SPDX-License-Identifier: BSD-2-Clause
 * Native host tests: actual stock bytes are read in memory only.
 * Actual pinned Mu headers, OpenSSL SHA256, fake ArmCallSmc; no SMC instruction.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
/* Mu Base.h defines its own NULL; keep host libc and real Mu headers intact. */
#undef NULL
#include <Base.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseCryptLib.h>
#include "g2-zap-mu.h"

static unsigned long checks, cases;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct mock {
    unsigned gates, effects, smcs, hashes, fail_gate, fail_effect, fail_hash;
    unsigned fail_smc, reply_kind;
    uint64_t reply;
    enum lg2_step steps[32];
    struct lg2_regs sent[8];
    uint8_t metadata[16384+32], code[16384+32];
};
static struct mock *active;
static struct lg2_image image;

BOOLEAN EFIAPI Sha256HashAll(CONST VOID *data, UINTN n, UINT8 *out) {
    CHECK(active != NULL);
    active->hashes++;
    if (active->fail_hash == active->hashes) return FALSE;
    return SHA256(data, (size_t)n, out) != NULL;
}
VOID ArmCallSmc(ARM_SMC_ARGS *a) {
    struct lg2_regs *r;
    CHECK(active != NULL && active->smcs < 8);
    r = &active->sent[active->smcs++];
    r->x[0]=a->Arg0; r->x[1]=a->Arg1; r->x[2]=a->Arg2; r->x[3]=a->Arg3;
    r->x[4]=a->Arg4; r->x[5]=a->Arg5; r->x[6]=a->Arg6; r->x[7]=a->Arg7;
    r->x[8]=a->Arg8; r->x[9]=a->Arg9; r->x[10]=a->Arg10; r->x[11]=a->Arg11;
    r->x[12]=a->Arg12; r->x[13]=a->Arg13; r->x[14]=a->Arg14; r->x[15]=a->Arg15;
    r->x[16]=a->Arg16; r->x[17]=a->Arg17;
    a->Arg0=0; a->Arg1=0;
    a->Arg2=0x22222222; a->Arg3=0x33333333;
    a->Arg4=4; a->Arg5=5; a->Arg6=6; a->Arg7=7;
    a->Arg8=8; a->Arg9=9; a->Arg10=10; a->Arg11=11;
    a->Arg12=12; a->Arg13=13; a->Arg14=14; a->Arg15=15;
    a->Arg16=16; a->Arg17=17;
    if (active->fail_smc == active->smcs) {
        if (active->reply_kind == 0) a->Arg0=active->reply;
        else a->Arg1=active->reply;
    }
}
static void reset_mock(struct mock *m) {
    memset(m, 0, sizeof(*m));
    memset(m->metadata, 0xa5, sizeof(m->metadata));
    memset(m->code, 0xa5, sizeof(m->code));
    active = m;
}
static int qualify(void *p, const struct lg2_layout *l, enum lg2_step step) {
    struct mock *m = p;
    CHECK(m == active && m->gates < 32 && l->cpu_hz == LG2_CPU_HZ);
    m->steps[m->gates++] = step;
    return m->fail_gate == m->gates ? -70 : 0;
}
static int effect(struct mock *m) {
    m->effects++;
    return m->fail_effect == m->effects ? -71 : 0;
}
static uint8_t *buffer(struct mock *m, const struct lg2_allocation *a,
                       uint64_t offset, size_t n) {
    CHECK(a->bytes <= 16384 && offset <= a->bytes && n <= a->bytes - offset);
    CHECK(a->owner_token == 11 || a->owner_token == 22);
    return (a->owner_token == 11 ? m->metadata : m->code) + 16 + offset;
}
static int write_blob(void *p, const struct lg2_allocation *a, uint64_t off,
                       const uint8_t *data, size_t n) {
    struct mock *m = p;
    uint8_t *b = buffer(m, a, off, n);
    if (effect(m)) return -71;
    memcpy(b, data, n);
    return 0;
}
static int zero_blob(void *p, const struct lg2_allocation *a, uint64_t off, size_t n) {
    struct mock *m = p;
    uint8_t *b = buffer(m, a, off, n);
    CHECK(a->owner_token == 22 && off == 0 && n == 8192);
    if (effect(m)) return -71;
    memset(b, 0, n);
    return 0;
}
static int sync_blob(void *p, const struct lg2_allocation *a, enum lg2_step step) {
    struct mock *m = p;
    CHECK((a->owner_token == 11 &&
           (step == LG2_METADATA_PUBLISH || step == LG2_METADATA_COMPLETE)) ||
          (a->owner_token == 22 && step == LG2_CODE_PUBLISH));
    return effect(m);
}
static int smc(void *p, struct lg2_regs *r) {
    struct mock *m = p;
    if (effect(m)) return -71;
    return lg2_mu_smc(p, r);
}
static struct lg2_ops ops(struct mock *m) {
    struct lg2_ops o = {m, lg2_mu_sha256, qualify, write_blob, zero_blob, sync_blob, smc};
    return o;
}
static struct lg2_layout layout(void) {
    /* Artificial host addresses/receipts ONLY. 0x92300000 is NOT proved free. */
    struct lg2_layout l = {0};
    l.metadata = (struct lg2_allocation){0x88000000,16384,4096,11,16384,LG2_NONCACHEABLE};
    l.code = (struct lg2_allocation){0x92300000,16384,0,22,16384,LG2_WRITE_COMBINING};
    l.qualifications = LG2_Q_REQUIRED;
    l.cpu_hz = LG2_CPU_HZ;
    l.abi = LG2_SMC64;
    return l;
}
static struct lg2_blob read_blob(const char *dir, const char *name) {
    char path[2048];
    FILE *f;
    long n;
    struct lg2_blob b;
    CHECK(snprintf(path, sizeof(path), "%s/%s", dir, name) > 0);
    f = fopen(path, "rb"); CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    n = ftell(f); CHECK(n > 0 && n < 1000000);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    b.size = (size_t)n; b.data = malloc(b.size); CHECK(b.data != NULL);
    CHECK(fread((void *)b.data, 1, b.size, f) == b.size);
    CHECK(fclose(f) == 0);
    return b;
}
static void pristine(struct mock *m) {
    CHECK(m->effects == 0 && m->smcs == 0);
    for (size_t n=0; n<sizeof(m->code); n++) CHECK(m->code[n] == 0xa5);
    for (size_t n=0; n<sizeof(m->metadata); n++) CHECK(m->metadata[n] == 0xa5);
}
static void structural(void) {
    struct lg2_info i;
    struct lg2_image bad;
    size_t n, f;
    CHECK(lg2_validate_image(&image, &i) == LG2_OK);
    CHECK(i.metadata_bytes == 6684 && i.header_bytes == 148 && i.hash_bytes == 6536);
    CHECK(i.code_bytes == 4900 && i.code_mem_bytes == 4900);
    CHECK(i.min_paddr == 0x5000 && i.aligned_end == 0x7000 && i.code_span == 8192);
    CHECK(i.elf_segment_alignment == 1048576);
    CHECK(lg2_validate_image(NULL, &i) == LG2_ARGUMENT);
    CHECK(lg2_validate_image(&image, NULL) == LG2_ARGUMENT);
    for (f=0; f<4; f++) {
        const struct lg2_blob *original = f ? &image.b[f-1] : &image.mdt;
        for (n=0; n<=original->size+1; n++) {
            struct lg2_blob *b;
            cases++; bad=image; b=f ? &bad.b[f-1] : &bad.mdt; b->size=n;
            CHECK(lg2_validate_image(&bad, &i) == (n == original->size ? LG2_OK : LG2_SEGMENT));
        }
        cases++; bad=image;
        (f ? &bad.b[f-1] : &bad.mdt)->data=NULL;
        CHECK(lg2_validate_image(&bad, &i) == LG2_ARGUMENT);
    }
    /* Every header byte, keeping the duplicate .b00 consistent, must reject. */
    for (n=0; n<148; n++) {
        uint8_t *a=(uint8_t *)image.mdt.data, *b=(uint8_t *)image.b[0].data;
        cases++; a[n]^=1; b[n]^=1;
        CHECK(lg2_validate_image(&image, &i) != LG2_OK);
        a[n]^=1; b[n]^=1;
    }
}
static void identities(void) {
    struct mock m;
    struct lg2_context c;
    struct lg2_layout l=layout();
    struct lg2_ops o;
    cases++; reset_mock(&m); o=ops(&m); memset(&c,0,sizeof(c));
    CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_OK);
    pristine(&m);
    for (size_t f=0; f<4; f++) {
        struct lg2_blob *b=f ? &image.b[f-1] : &image.mdt;
        for (size_t n=0; n<b->size; n++) {
            uint8_t *data=(uint8_t *)b->data;
            cases++; reset_mock(&m); o=ops(&m); memset(&c,0,sizeof(c));
            data[n]^=1;
            CHECK(lg2_prepare(&c,&o,&image,&l) != LG2_OK);
            CHECK(m.effects == 0 && m.smcs == 0 && !c.result.resources_pinned);
            data[n]^=1;
        }
    }
    for (unsigned n=1; n<=4; n++) {
        cases++; reset_mock(&m); m.fail_hash=n; o=ops(&m); memset(&c,0,sizeof(c));
        CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_DIGEST);
        pristine(&m);
    }
}
static void layouts(void) {
    struct lg2_layout l=layout(), b;
    struct lg2_info i;
    struct lg2_range live[LG2_MAX_LIVE];
    struct mock m;
    struct lg2_ops o;
    struct lg2_context c;
    CHECK(lg2_validate_image(&image,&i) == LG2_OK);
    CHECK(lg2_validate_layout(&l,&i) == LG2_OK);
    /* Both pool edges; every possible aligned start including the final page.
     * This is a range calculation, not a memory-availability determination. */
    for (uint64_t pa=LG2_POOL_START-16384; pa<=LG2_POOL_END+16384; pa+=4096) {
        cases++; b=l; b.code.pa=pa; b.code.bytes=8192; b.code.page_bytes=4096;
        CHECK(lg2_validate_layout(&b,&i) ==
              (pa>=LG2_POOL_START && pa<=LG2_POOL_END-8192 ? LG2_OK : LG2_LAYOUT));
    }
    for (unsigned bit=0; bit<14; bit++) {
        cases++; b=l; b.qualifications &= ~(UINT64_C(1)<<bit);
        reset_mock(&m); o=ops(&m); memset(&c,0,sizeof(c));
        CHECK(lg2_validate_layout(&b,&i) == LG2_OK); /* arithmetic != ownership */
        CHECK(lg2_prepare(&c,&o,&image,&b) == LG2_DEPENDENCY);
        pristine(&m);
    }
#define BAD(expr, expected) do { cases++; b=l; expr; \
    CHECK(lg2_validate_layout(&b,&i) == expected); } while (0)
    BAD(b.code.pa++,LG2_LAYOUT);
    BAD(b.code.pa=UINT64_MAX-4095,LG2_LAYOUT);
    BAD(b.code.bytes=UINT64_MAX-4095,LG2_LAYOUT);
    BAD(b.code.bytes=4096,LG2_LAYOUT);
    BAD(b.code.bytes=0,LG2_LAYOUT);
    BAD(b.code.data_offset=4096,LG2_LAYOUT);
    BAD(b.code.page_bytes=8192,LG2_LAYOUT);
    BAD(b.metadata.page_bytes=8192,LG2_LAYOUT);
    BAD(b.metadata.pa=UINT64_MAX-16383,LG2_LAYOUT);
    BAD(b.metadata.bytes=0,LG2_LAYOUT);
    BAD(b.metadata.data_offset=1,LG2_LAYOUT);
    BAD(b.metadata.data_offset=12288,LG2_LAYOUT);
    BAD(b.metadata.data_offset=UINT64_MAX,LG2_LAYOUT);
    BAD(b.metadata.pa=LG2_POOL_START-16384+4096,LG2_LAYOUT);
    BAD(b.metadata.pa=LG2_POOL_START,LG2_LAYOUT);
    BAD(b.metadata.pa=l.code.pa,LG2_LAYOUT);
    BAD(b.metadata.owner_token=22,LG2_LAYOUT);
    BAD(b.metadata.owner_token=0,LG2_LAYOUT);
    BAD(b.code.owner_token=0,LG2_LAYOUT);
    BAD(b.metadata.cache=LG2_WRITE_COMBINING,LG2_LAYOUT);
    BAD(b.code.cache=LG2_CACHE_UNVERIFIED,LG2_LAYOUT);
    BAD(b.cpu_hz=691200000,LG2_DEPENDENCY);
    BAD(b.abi=LG2_ABI_UNVERIFIED,LG2_DEPENDENCY);
    BAD(b.abi=(enum lg2_abi)3,LG2_DEPENDENCY);
    BAD(b.abi=LG2_SMC32; b.metadata.pa=UINT64_C(0x100000000),LG2_LAYOUT);
    BAD(b.metadata.pa=UINT64_C(0x100000000),LG2_OK); /* SMC64 */
    BAD(b.metadata.data_offset=0; b.metadata.page_bytes=4096; b.metadata.bytes=8192,LG2_OK);
    BAD(b.live_count=129,LG2_LAYOUT);
    BAD(b.live_count=1,LG2_LAYOUT);
    for (size_t n=0; n<LG2_MAX_LIVE; n++) live[n]=(struct lg2_range){0x40000000+n*16384,16384};
    b=l; b.live=live; b.live_count=LG2_MAX_LIVE;
    cases++; CHECK(lg2_validate_layout(&b,&i) == LG2_OK);
    for (unsigned n=0; n<2; n++) {
        cases++; live[0]=(struct lg2_range){n ? l.metadata.pa : l.code.pa+8192,4096};
        CHECK(lg2_validate_layout(&b,&i) == LG2_LAYOUT); /* whole 16KiB allocation */
    }
    live[0]=(struct lg2_range){UINT64_MAX,1};
    cases++; CHECK(lg2_validate_layout(&b,&i) == LG2_LAYOUT);
    live[0]=(struct lg2_range){0x40000000,0};
    cases++; CHECK(lg2_validate_layout(&b,&i) == LG2_LAYOUT);
    /* All eight-byte-aligned overlaps across both ends of an allocation. */
    b=l; b.live=live; b.live_count=1;
    for (uint64_t pa=l.code.pa-8192; pa<=l.code.pa+24576; pa+=8) {
        int overlap=pa<l.code.pa+16384 && pa+4096>l.code.pa;
        cases++; live[0]=(struct lg2_range){pa,4096};
        CHECK(lg2_validate_layout(&b,&i) == (overlap ? LG2_LAYOUT : LG2_OK));
    }
#undef BAD
}
static void requests(void) {
    static const uint64_t fn[5]={0x02000201,0x02000202,0x02000205,0x0200010a,0x02000206};
    static const uint64_t ai[5]={0x82,3,1,2,1};
    struct mock m;
    struct lg2_regs r;
    reset_mock(&m);
    for (unsigned abi=LG2_SMC32; abi<=LG2_SMC64; abi++) {
        for (unsigned cmd=0; cmd<5; cmd++) {
            cases++;
            CHECK(lg2_build_request((enum lg2_abi)abi,(enum lg2_command)cmd,
                                    0x88001000,0x92300000,8192,&r) == LG2_OK);
            CHECK(r.x[0] == (fn[cmd] | (abi==LG2_SMC64 ? UINT64_C(0x40000000) : 0)));
            CHECK(r.x[1] == ai[cmd]);
            CHECK(r.x[2] == (cmd==LG2_CMD_RESUME ? 0u : 13u));
            CHECK(r.x[3] == (cmd==LG2_CMD_INIT ? 0x88001000u :
                             cmd==LG2_CMD_MEM ? 0x92300000u :
                             cmd==LG2_CMD_RESUME ? 13u : 0u));
            CHECK(r.x[4] == (cmd==LG2_CMD_MEM ? 8192u : 0u));
            for (size_t n=5; n<18; n++) CHECK(r.x[n] == 0);
            CHECK(lg2_mu_smc(NULL,&r) == 0); /* full 18-register copy-back */
            CHECK(r.x[0] == 0 && r.x[1] == 0 && r.x[2] == 0x22222222 && r.x[3] == 0x33333333);
            for (size_t n=4; n<18; n++) CHECK(r.x[n] == n);
            m.smcs=0;
            /* Mutate every spare register; adapter rejects before fake SMC. */
            for (size_t n=5; n<18; n++) {
                CHECK(lg2_build_request((enum lg2_abi)abi,(enum lg2_command)cmd,
                                        0x88001000,0x92300000,8192,&r) == LG2_OK);
                r.x[n]=1; cases++;
                CHECK(lg2_mu_smc(NULL,&r) != 0 && m.smcs == 0);
            }
        }
    }
    CHECK(lg2_mu_smc(NULL,NULL) != 0);
    CHECK(lg2_build_request(LG2_ABI_UNVERIFIED,LG2_CMD_AUTH,0,0,0,&r) == LG2_DEPENDENCY);
    CHECK(lg2_build_request(LG2_SMC32,LG2_CMD_INIT,UINT64_C(0x100000000),0,0,&r) == LG2_LAYOUT);
    CHECK(lg2_build_request(LG2_SMC64,LG2_CMD_INIT,0x88001001,0,0,&r) == LG2_LAYOUT);
    CHECK(lg2_build_request(LG2_SMC64,LG2_CMD_MEM,0,LG2_POOL_END-4096,8192,&r) == LG2_LAYOUT);
    CHECK(lg2_build_request(LG2_SMC64,LG2_CMD_MEM,0,LG2_POOL_START,4900,&r) == LG2_LAYOUT);
    CHECK(lg2_build_request(LG2_SMC64,(enum lg2_command)99,0,0,0,&r) == LG2_ARGUMENT);
    CHECK(lg2_build_request(LG2_SMC64,LG2_CMD_AUTH,0,0,0,NULL) == LG2_ARGUMENT);
    for (unsigned n=0; n<4; n++) {
        cases++; CHECK(lg2_build_request(LG2_SMC64,LG2_CMD_AUTH,0,0,0,&r) == LG2_OK);
        if (n==0) r.x[0]|=UINT64_C(0x80000000);
        if (n==1) r.x[2]=14;
        if (n==2) r.x[1]=0x82;
        if (n==3) r.x[3]=1;
        CHECK(lg2_mu_smc(NULL,&r) != 0 && m.smcs == 0);
    }
}
static enum lg2_status run(struct lg2_context *c, struct mock *m, struct lg2_layout *l) {
    struct lg2_ops o=ops(m);
    enum lg2_status s;
    if ((s=lg2_prepare(c,&o,&image,l)) != LG2_OK) return s;
    if ((s=lg2_execute(c)) != LG2_OK) return s;
    if ((s=lg2_resume(c)) != LG2_OK) return s;
    return lg2_shutdown(c);
}
static void lifecycle(void) {
    static const enum lg2_step steps[]={
        LG2_CHECK,LG2_METADATA_COPY,LG2_METADATA_PUBLISH,LG2_INIT_IMAGE,
        LG2_METADATA_COMPLETE,LG2_MEM_SETUP,LG2_CODE_ZERO,LG2_CODE_COPY,
        LG2_CODE_PUBLISH,LG2_AUTH_RESET,LG2_RESUME,LG2_SHUTDOWN};
    struct lg2_context c={0};
    struct lg2_layout l=layout();
    struct lg2_ops o;
    struct mock m;
    reset_mock(&m); o=ops(&m);
    cases++; CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_OK);
    pristine(&m);
    CHECK(c.result.state == LG2_PREPARED && c.result.smc_calls == 0);
    CHECK(lg2_resume(&c) == LG2_STATE && lg2_shutdown(&c) == LG2_STATE);
    CHECK(lg2_execute(&c) == LG2_OK);
    CHECK(c.result.state == LG2_LOADED && c.result.resources_pinned && !c.result.code_release_allowed);
    CHECK(m.smcs == 3 && m.effects == 9);
    CHECK(m.sent[0].x[0] == 0x42000201 && m.sent[0].x[1] == 0x82);
    CHECK(m.sent[0].x[2] == 13 && m.sent[0].x[3] == 0x88001000);
    CHECK(m.sent[1].x[0] == 0x42000202 && m.sent[1].x[2] == 13);
    CHECK(m.sent[1].x[3] == 0x92300000 && m.sent[1].x[4] == 8192);
    CHECK(m.sent[2].x[0] == 0x42000205 && m.sent[2].x[2] == 13);
    CHECK(c.result.write_calls == 2 && c.result.zero_calls == 1 && c.result.sync_calls == 3);
    CHECK(memcmp(m.metadata+16+4096,image.mdt.data,6684) == 0);
    CHECK(memcmp(m.code+16,image.b[2].data,4900) == 0);
    for (size_t n=0; n<sizeof(m.metadata); n++)
        if (n<4112 || n>=4112+6684) CHECK(m.metadata[n] == 0xa5);
    for (size_t n=0; n<sizeof(m.code); n++) {
        if (n>=4916 && n<8208) CHECK(m.code[n] == 0);
        if (n<16 || n>=8208) CHECK(m.code[n] == 0xa5);
    }
    CHECK(lg2_execute(&c) == LG2_STATE && m.smcs == 3);
    CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_STATE);
    CHECK(lg2_resume(&c) == LG2_OK);
    CHECK(m.sent[3].x[0] == 0x4200010a && m.sent[3].x[1] == 2);
    CHECK(m.sent[3].x[2] == 0 && m.sent[3].x[3] == 13);
    CHECK(lg2_resume(&c) == LG2_STATE && m.smcs == 4);
    CHECK(lg2_shutdown(&c) == LG2_OK);
    CHECK(m.sent[4].x[0] == 0x42000206 && m.sent[4].x[2] == 13);
    CHECK(c.result.state == LG2_STOPPED && c.result.code_release_allowed && !c.result.resources_pinned);
    CHECK(lg2_shutdown(&c) == LG2_STATE && m.smcs == 5);
    CHECK(m.gates == sizeof(steps)/sizeof(steps[0]));
    for (size_t n=0; n<m.gates; n++) CHECK(m.steps[n] == steps[n]);
    /* Caller mutates retained firmware after prepare: reject before writes. */
    cases++; reset_mock(&m); o=ops(&m); memset(&c,0,sizeof(c));
    CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_OK);
    ((uint8_t *)image.b[2].data)[4000]^=1;
    CHECK(lg2_execute(&c) == LG2_DIGEST);
    ((uint8_t *)image.b[2].data)[4000]^=1;
    pristine(&m);
    /* Live ranges are copied, not borrowed from a transient caller array. */
    {
        struct lg2_range live={0x40000000,16384};
        cases++; reset_mock(&m); o=ops(&m); memset(&c,0,sizeof(c));
        l.live=&live; l.live_count=1;
        CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_OK);
        live.pa=l.code.pa;
        CHECK(c.layout.live == c.live && c.layout.live[0].pa == 0x40000000);
        CHECK(lg2_execute(&c) == LG2_OK);
        CHECK(lg2_shutdown(&c) == LG2_OK);
    }
}
static void failures(void) {
    struct lg2_context c;
    struct lg2_layout l=layout();
    struct mock m;
    static const uint64_t returns[]={
        1,2,UINT64_C(0xffffffff),UINT64_C(0xfffffffd),
        UINT64_C(0xfffffffffffffffd),UINT64_C(0xffffffffffffffa1),
        UINT64_C(0xffffffffffffffed),UINT64_C(0x100000000)};
    for (unsigned n=1; n<=12; n++) {
        cases++; reset_mock(&m); memset(&c,0,sizeof(c)); m.fail_gate=n;
        CHECK(run(&c,&m,&l) == LG2_DEPENDENCY);
        CHECK(c.result.state == LG2_FAILED && !c.result.code_release_allowed);
        CHECK(m.gates == n && m.effects == (n<3 ? 0 : n-2));
        CHECK(lg2_execute(&c) == LG2_STATE);
        CHECK(lg2_resume(&c) == LG2_STATE && lg2_shutdown(&c) == LG2_STATE);
    }
    for (unsigned n=1; n<=11; n++) {
        cases++; reset_mock(&m); memset(&c,0,sizeof(c)); m.fail_effect=n;
        CHECK(run(&c,&m,&l) == LG2_CALLBACK);
        CHECK(m.effects == n && c.result.resources_pinned && !c.result.code_release_allowed);
        CHECK(c.result.callback_status == -71 && c.result.state == LG2_FAILED);
        CHECK(lg2_execute(&c) == LG2_STATE && m.effects == n);
    }
    for (unsigned abi=LG2_SMC32; abi<=LG2_SMC64; abi++) {
        l.abi=(enum lg2_abi)abi;
        for (unsigned cmd=1; cmd<=5; cmd++)
            for (unsigned kind=0; kind<2; kind++)
                for (size_t n=0; n<sizeof(returns)/sizeof(returns[0]); n++) {
                    cases++; reset_mock(&m); memset(&c,0,sizeof(c));
                    m.fail_smc=cmd; m.reply_kind=kind; m.reply=returns[n];
                    CHECK(run(&c,&m,&l) == (kind ? LG2_SECURE : LG2_TRANSPORT));
                    CHECK(c.result.resources_pinned && !c.result.code_release_allowed);
                    CHECK(m.smcs == cmd && c.result.smc_calls == cmd);
                    CHECK(c.result.raw[kind] == returns[n]);
                    CHECK(c.result.raw[2] == 0x22222222 && c.result.raw[3] == 0x33333333);
                    CHECK(lg2_execute(&c) == LG2_STATE && lg2_resume(&c) == LG2_STATE);
                    CHECK(lg2_shutdown(&c) == LG2_STATE && m.smcs == cmd);
                }
    }
    /* Every callback is required; disabled/unwired interface does no work. */
    l=layout();
    for (unsigned n=0; n<6; n++) {
        struct lg2_ops o;
        cases++; reset_mock(&m); memset(&c,0,sizeof(c)); o=ops(&m);
        switch (n) {
        case 0:o.sha256=NULL;break; case 1:o.qualify=NULL;break;
        case 2:o.write=NULL;break; case 3:o.zero=NULL;break;
        case 4:o.sync=NULL;break; default:o.smc=NULL;break;
        }
        CHECK(lg2_prepare(&c,&o,&image,&l) == LG2_ARGUMENT);
        pristine(&m);
    }
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    image.mdt=read_blob(argv[1],"a530_zap.mdt");
    image.b[0]=read_blob(argv[1],"a530_zap.b00");
    image.b[1]=read_blob(argv[1],"a530_zap.b01");
    image.b[2]=read_blob(argv[1],"a530_zap.b02");
    structural(); identities(); layouts(); requests(); lifecycle(); failures();
    for (size_t n=0; n<3; n++) free((void *)image.b[n].data);
    free((void *)image.mdt.data);
    printf("PASS g2 stock8/PAS13: %lu cases, %lu checks; fake SMC only\n",cases,checks);
    return 0;
}
