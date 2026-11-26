/* SPDX-License-Identifier: BSD-2-Clause */
#include "g2-zap.h"

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int equal(const uint8_t *a, const uint8_t *b, size_t n) {
    size_t i;
    uint8_t different = 0;
    for (i = 0; i < n; i++) different |= a[i] ^ b[i];
    return different == 0;
}
static int end_of(uint64_t start, uint64_t bytes, uint64_t *end) {
    if (!bytes || start > UINT64_MAX - bytes) return 0;
    *end = start + bytes;
    return 1;
}
static int overlaps(uint64_t a, uint64_t ae, uint64_t b, uint64_t be) {
    return a < be && b < ae;
}

enum lg2_status lg2_validate_image(const struct lg2_image *im,
                                  struct lg2_info *out) {
    /* Stock8 profile, not a general-purpose permissive ELF loader.
     * ph0 header/nonload; ph1 HASH/nonload; ph2 relocatable PT_LOAD.
     * Keep the ELF p_align=1MiB as metadata: Linux relocates by min_paddr,
     * uses a 4KiB rounded span, and the Xiaomi reserved code region is 8KiB.
     */
    static const uint32_t ph[3][8] = {
        {0, 0, 0, 0, 148, 0, 0x07000000, 0},
        {0, 4096, 0x7000, 0x7000, 6536, 8192, 0x02200000, 4096},
        {1, 12288, 0x5000, 0x5000, 4900, 4900, 0x08000007, 1048576}
    };
    const uint8_t *p;
    size_t i, j;
    struct lg2_info info = {0};
    if (out) *out = info;
    if (!im || !out || !im->mdt.data || !im->b[0].data ||
        !im->b[1].data || !im->b[2].data) return LG2_ARGUMENT;
    if (im->mdt.size != LG2_METADATA_BYTES || im->b[0].size != 148 ||
        im->b[1].size != 6536 || im->b[2].size != LG2_CODE_BYTES)
        return LG2_SEGMENT;
    p = im->mdt.data;
    if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F' ||
        p[4] != 1 || p[5] != 1 || p[6] != 1) return LG2_ELF;
    for (i = 7; i < 16; i++) if (p[i]) return LG2_ELF;
    if (u16(p+16) != 2 || u16(p+18) != 164 || u32(p+20) != 1 ||
        u32(p+24) != 0x5000 || u32(p+28) != 52 || u32(p+32) ||
        u32(p+36) != 3 || u16(p+40) != 52 || u16(p+42) != 32 ||
        u16(p+44) != 3 || u16(p+46) || u16(p+48) || u16(p+50))
        return LG2_ELF;
    /* Exact counts/offsets above bound every read before touching a phdr. */
    for (i = 0; i < 3; i++)
        for (j = 0; j < 8; j++)
            if (u32(p + 52 + 32*i + 4*j) != ph[i][j]) return LG2_SEGMENT;
    if (!equal(p, im->b[0].data, 148) ||
        !equal(p+148, im->b[1].data, 6536)) return LG2_SEGMENT;
    info.header_bytes = ph[0][4];
    info.hash_bytes = ph[1][4];
    info.metadata_bytes = info.header_bytes + info.hash_bytes;
    info.code_bytes = ph[2][4];
    info.code_mem_bytes = ph[2][5];
    info.min_paddr = ph[2][3];
    info.aligned_end = (ph[2][3] + ph[2][5] + 4095u) & ~4095u;
    info.code_span = info.aligned_end - info.min_paddr;
    info.elf_segment_alignment = ph[2][7];
    *out = info;
    return LG2_OK;
}

static int allocation(const struct lg2_allocation *a, uint64_t need,
                      uint64_t *end) {
    if ((a->page_bytes != 4096 && a->page_bytes != 16384) ||
        (a->pa & (a->page_bytes-1)) || (a->bytes & (a->page_bytes-1)) ||
        !end_of(a->pa, a->bytes, end) || !a->owner_token ||
        (a->data_offset & 4095) || a->data_offset > a->bytes ||
        need > a->bytes - a->data_offset) return 0;
    return 1;
}

enum lg2_status lg2_validate_layout(const struct lg2_layout *l,
                                   const struct lg2_info *i) {
    uint64_t me, ce, le;
    size_t n;
    if (!l || !i) return LG2_ARGUMENT;
    if (i->metadata_bytes != LG2_METADATA_BYTES || i->code_span != LG2_CODE_SPAN ||
        i->code_bytes != LG2_CODE_BYTES || i->code_mem_bytes != LG2_CODE_BYTES)
        return LG2_LAYOUT;
    if (l->cpu_hz != LG2_CPU_HZ ||
        (l->abi != LG2_SMC32 && l->abi != LG2_SMC64)) return LG2_DEPENDENCY;
    if (l->live_count > LG2_MAX_LIVE || (l->live_count && !l->live))
        return LG2_LAYOUT;
    if (!allocation(&l->metadata, i->metadata_bytes, &me) ||
        !allocation(&l->code, i->code_span, &ce) || l->code.data_offset ||
        l->metadata.owner_token == l->code.owner_token ||
        l->metadata.cache != LG2_NONCACHEABLE ||
        (l->code.cache != LG2_NONCACHEABLE && l->code.cache != LG2_WRITE_COMBINING))
        return LG2_LAYOUT;
    if (l->code.pa < LG2_POOL_START || ce > LG2_POOL_END ||
        overlaps(l->metadata.pa, me, LG2_POOL_START, LG2_POOL_END) ||
        overlaps(l->metadata.pa, me, l->code.pa, ce)) return LG2_LAYOUT;
    if (l->abi == LG2_SMC32 && me > UINT64_C(0x100000000)) return LG2_LAYOUT;
    for (n = 0; n < l->live_count; n++) {
        if (!end_of(l->live[n].pa, l->live[n].bytes, &le) ||
            overlaps(l->metadata.pa, me, l->live[n].pa, le) ||
            overlaps(l->code.pa, ce, l->live[n].pa, le)) return LG2_LAYOUT;
    }
    return LG2_OK;
}

enum lg2_status lg2_build_request(enum lg2_abi abi, enum lg2_command cmd,
                                 uint64_t mp, uint64_t cp, uint64_t span,
                                 struct lg2_regs *r) {
    struct lg2_regs z = {{0}};
    uint64_t fn = UINT64_C(0x02000200);
    if (!r) return LG2_ARGUMENT;
    *r = z;
    if (abi != LG2_SMC32 && abi != LG2_SMC64) return LG2_DEPENDENCY;
    switch (cmd) {
    case LG2_CMD_INIT:
        if (!mp || (mp & 4095) ||
            (abi == LG2_SMC32 && mp > UINT32_MAX)) return LG2_LAYOUT;
        fn |= 1;
        r->x[1] = 0x82; /* SCM_ARGS(2, VAL, RW), NOT 2 plain VAL arguments */
        r->x[2] = 13; r->x[3] = mp;
        break;
    case LG2_CMD_MEM:
        if (cp < LG2_POOL_START || (cp & 4095) || span != LG2_CODE_SPAN ||
            cp > LG2_POOL_END - span) return LG2_LAYOUT;
        fn |= 2; r->x[1] = 3; r->x[2] = 13; r->x[3] = cp; r->x[4] = span;
        break;
    case LG2_CMD_AUTH:
        fn |= 5; r->x[1] = 1; r->x[2] = 13;
        break;
    case LG2_CMD_SHUTDOWN:
        fn |= 6; r->x[1] = 1; r->x[2] = 13;
        break;
    case LG2_CMD_RESUME:
        fn = UINT64_C(0x0200010a); r->x[1] = 2; r->x[2] = 0; r->x[3] = 13;
        break;
    default: *r = z; return LG2_ARGUMENT;
    }
    /* Standard/yielding call, not the atomic/fast bit31 form. Capability
     * discovery and interrupted-call continuation are platform prerequisites.
     * This one-shot core treats every nonzero transport result as terminal.
     */
    r->x[0] = fn | (abi == LG2_SMC64 ? UINT64_C(0x40000000) : 0);
    return LG2_OK;
}

/* Public SHA256 identities only; no signed image bytes embedded here.
 * Actual authentication is exclusively the PAS AUTH_AND_RESET result.
 */
static const uint8_t hashes[4][32] = {
    {0x0a,0xc7,0x43,0x41,0x12,0xce,0xf8,0xfd,0xa2,0x45,0x39,0xbc,0xf5,0xb7,0x2e,0x55,
     0xe1,0x0a,0xc7,0x09,0xdc,0x23,0x58,0x54,0x7a,0x83,0xbf,0x9a,0x06,0x85,0x6e,0x73},
    {0x1f,0x38,0x4b,0x70,0xd6,0x88,0x63,0x40,0x7d,0x66,0xf2,0xba,0x32,0x8b,0x9b,0x77,
     0x29,0xab,0x14,0x9b,0x43,0xbb,0xb5,0xd5,0xf7,0xda,0x9e,0x21,0xf9,0x22,0x1d,0xe2},
    {0xd3,0x36,0x40,0xfa,0x05,0xe9,0x11,0x10,0x07,0xc2,0xf7,0x1e,0x35,0xde,0x5c,0x41,
     0x65,0x8c,0x80,0x7c,0xd8,0x37,0x50,0x1a,0xf1,0xb5,0xd2,0x02,0xa5,0x77,0x1a,0x14},
    {0xbe,0xf9,0x8d,0x70,0x92,0x27,0xd3,0xa4,0x5d,0x04,0x64,0x39,0xc1,0xdc,0x6a,0x7a,
     0xf5,0xce,0x60,0x7d,0xf6,0xba,0x96,0x0f,0x99,0x41,0x7f,0xb5,0x69,0xf5,0x70,0x0f}
};
static enum lg2_status digest(struct lg2_context *c) {
    const struct lg2_blob *b;
    uint8_t hash[32];
    size_t n;
    for (n = 0; n < 4; n++) {
        b = n == 0 ? &c->image.mdt : &c->image.b[n-1];
        c->result.callback_status = c->ops.sha256(c->ops.ctx, b->data, b->size, hash);
        if (c->result.callback_status || !equal(hash, hashes[n], 32))
            return LG2_DIGEST;
    }
    return LG2_OK;
}
static enum lg2_status fail(struct lg2_context *c, enum lg2_status s) {
    c->result.status = s;
    c->result.state = LG2_FAILED;
    c->result.code_release_allowed = 0;
    return s;
}
static enum lg2_status gate(struct lg2_context *c, enum lg2_step step) {
    enum lg2_status s;
    c->result.step = step;
    s = lg2_validate_layout(&c->layout, &c->info);
    if (s != LG2_OK) return fail(c, s);
    if ((c->layout.qualifications & LG2_Q_REQUIRED) != LG2_Q_REQUIRED)
        return fail(c, LG2_DEPENDENCY);
    c->result.callback_status = c->ops.qualify(c->ops.ctx, &c->layout, step);
    if (c->result.callback_status) return fail(c, LG2_DEPENDENCY);
    return LG2_OK;
}

enum lg2_status lg2_prepare(struct lg2_context *c, const struct lg2_ops *o,
                            const struct lg2_image *im, const struct lg2_layout *l) {
    enum lg2_status s;
    struct lg2_context zero = {0};
    size_t n;
    if (!c || !o || !im || !l) return LG2_ARGUMENT;
    if (c->result.state != LG2_EMPTY || c->result.resources_pinned) return LG2_STATE;
    *c = zero;
    if (!o->sha256 || !o->qualify || !o->write || !o->zero || !o->sync || !o->smc)
        return fail(c, LG2_ARGUMENT);
    c->ops = *o; c->image = *im; c->layout = *l;
    s = lg2_validate_image(im, &c->info);
    if (s != LG2_OK) return fail(c, s);
    s = lg2_validate_layout(l, &c->info);
    if (s != LG2_OK) return fail(c, s);
    for (n = 0; n < l->live_count; n++) c->live[n] = l->live[n];
    c->layout.live = c->live;
    if ((s = digest(c)) != LG2_OK) return fail(c, s);
    if ((s = gate(c, LG2_CHECK)) != LG2_OK) return s;
    c->result.state = LG2_PREPARED;
    return LG2_OK;
}

static enum lg2_status write_blob(struct lg2_context *c, enum lg2_step step,
                                  const struct lg2_allocation *a,
                                  const struct lg2_blob *b) {
    if (gate(c, step) != LG2_OK) return c->result.status;
    c->result.resources_pinned = 1; /* set BEFORE a possibly partial callback */
    c->result.write_calls++;
    c->result.callback_status = c->ops.write(c->ops.ctx, a, a->data_offset, b->data, b->size);
    if (c->result.callback_status) return fail(c, LG2_CALLBACK);
    return LG2_OK;
}
static enum lg2_status sync_allocation(struct lg2_context *c, enum lg2_step step,
                                       const struct lg2_allocation *a) {
    if (gate(c, step) != LG2_OK) return c->result.status;
    c->result.sync_calls++;
    c->result.callback_status = c->ops.sync(c->ops.ctx, a, step);
    if (c->result.callback_status) return fail(c, LG2_CALLBACK);
    return LG2_OK;
}
static enum lg2_status call(struct lg2_context *c, enum lg2_step step,
                            enum lg2_command cmd) {
    struct lg2_regs r;
    enum lg2_status s;
    size_t n;
    if (gate(c, step) != LG2_OK) return c->result.status;
    s = lg2_build_request(c->layout.abi, cmd,
                         c->layout.metadata.pa + c->layout.metadata.data_offset,
                         c->layout.code.pa, c->info.code_span, &r);
    if (s != LG2_OK) return fail(c, s);
    c->result.resources_pinned = 1;
    c->result.smc_calls++;
    c->result.callback_status = c->ops.smc(c->ops.ctx, &r);
    for (n = 0; n < 4; n++) c->result.raw[n] = r.x[n];
    if (c->result.callback_status) return fail(c, LG2_CALLBACK);
    if (r.x[0]) return fail(c, LG2_TRANSPORT); /* includes +1 INTERRUPTED and BUSY */
    if (r.x[1]) return fail(c, LG2_SECURE); /* no EOPNOTSUPP/ENODEV bypass */
    return LG2_OK;
}

enum lg2_status lg2_execute(struct lg2_context *c) {
    enum lg2_status s;
    if (!c) return LG2_ARGUMENT;
    if (c->result.state != LG2_PREPARED) return LG2_STATE;
    /* Revalidate borrowed image bytes immediately before any side effect.
     * Caller must prevent concurrent mutation until all callbacks return.
     */
    if ((s = lg2_validate_image(&c->image, &c->info)) != LG2_OK ||
        (s = digest(c)) != LG2_OK) return fail(c, s);
    c->result.state = LG2_LOADING;
    if (write_blob(c, LG2_METADATA_COPY, &c->layout.metadata, &c->image.mdt) ||
        sync_allocation(c, LG2_METADATA_PUBLISH, &c->layout.metadata) ||
        call(c, LG2_INIT_IMAGE, LG2_CMD_INIT) ||
        sync_allocation(c, LG2_METADATA_COMPLETE, &c->layout.metadata) ||
        call(c, LG2_MEM_SETUP, LG2_CMD_MEM)) return c->result.status;
    if (gate(c, LG2_CODE_ZERO) != LG2_OK) return c->result.status;
    c->result.zero_calls++;
    /* Clear the entire 8KiB load span, including its unused trailing padding.
     * This stock segment has no BSS; hash/header bytes never enter code RAM.
     */
    c->result.callback_status = c->ops.zero(c->ops.ctx, &c->layout.code, 0, c->info.code_span);
    if (c->result.callback_status) return fail(c, LG2_CALLBACK);
    if (write_blob(c, LG2_CODE_COPY, &c->layout.code, &c->image.b[2]) ||
        sync_allocation(c, LG2_CODE_PUBLISH, &c->layout.code) ||
        call(c, LG2_AUTH_RESET, LG2_CMD_AUTH)) return c->result.status;
    c->result.state = LG2_LOADED;
    return LG2_OK;
}

enum lg2_status lg2_resume(struct lg2_context *c) {
    if (!c) return LG2_ARGUMENT;
    if (c->result.state != LG2_LOADED || c->result.resume_used) return LG2_STATE;
    c->result.resume_used = 1;
    return call(c, LG2_RESUME, LG2_CMD_RESUME);
}
enum lg2_status lg2_shutdown(struct lg2_context *c) {
    if (!c) return LG2_ARGUMENT;
    if (c->result.state != LG2_LOADED || c->result.shutdown_used) return LG2_STATE;
    c->result.shutdown_used = 1;
    if (call(c, LG2_SHUTDOWN, LG2_CMD_SHUTDOWN) != LG2_OK) return c->result.status;
    c->result.state = LG2_STOPPED;
    c->result.resources_pinned = 0;
    c->result.code_release_allowed = 1;
    return LG2_OK;
}
