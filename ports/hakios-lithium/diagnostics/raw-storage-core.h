/* R12 finite raw-I/O experiment. Shared verbatim by the kernel and host tests.
 * io is synchronous; op 0=READ, 2=WRITE_FAST, 3=FLUSH. No HFS calls here.
 * All scratch bytes are saved before the first write and restored on failure.
 * An interrupted restoration affects only the bounded out-of-image scratch.
 */
#define LR_N 100
#define LR_B 4096
#define LR_TESTS 7
enum { LR_BACKUP, LR_WRITE, LR_FLUSH, LR_DURABLE, LR_READ, LR_RESTORE, LR_VERIFY };
struct lr_stats { uint64_t n, errors, sum, min, max; };
struct lr_result {
    struct lr_stats wall[LR_TESTS], service[LR_TESTS];
    unsigned backed, touched, restored, verified, mismatches;
    int preflush_error, restore_flush_error, timed_out, complete;
};
struct lr_ops {
    void *ctx;
    uint64_t (*now_ns)(void *);
    int (*io)(void *, unsigned, uint64_t, uint8_t *, uint64_t *);
};
static void lr_add(struct lr_stats *s, uint64_t ns, int error) {
    if (error) { s->errors++; return; }
    if (!s->n || ns < s->min) s->min=ns;
    if (!s->n || ns > s->max) s->max=ns;
    s->n++; s->sum+=ns;
}
static void lr_pattern(uint8_t *p, unsigned block) {
    uint32_t x=0x9e3779b9u ^ (block+1)*0x85ebca6bu;
    for (unsigned j=0;j<LR_B;j++) { x^=x<<13;x^=x>>17;x^=x<<5;p[j]=(uint8_t)x; }
}
static int lr_measure(struct lr_ops *o, struct lr_result *r, unsigned test,
    unsigned op, uint64_t lba, uint8_t *buf) {
    uint64_t service=0, start=o->now_ns(o->ctx);
    int err=o->io(o->ctx,op,lba,buf,&service);
    lr_add(&r->wall[test],o->now_ns(o->ctx)-start,err);
    lr_add(&r->service[test],service,err);
    return err;
}
static void lr_run(struct lr_ops *o, struct lr_result *r, uint64_t first,
    uint8_t backup[LR_N][LR_B], uint8_t buf[LR_B], uint8_t expected[LR_B]) {
    uint64_t deadline=o->now_ns(o->ctx)+120000000000ULL, ignored=0;
    memset(r,0,sizeof(*r));
    for (unsigned i=0;i<LR_N;i++) {
        if (o->now_ns(o->ctx)>=deadline) { r->timed_out=1; return; }
        if (lr_measure(o,r,LR_BACKUP,0,first+i,backup[i])) return;
        r->backed++;
    }
    r->preflush_error=o->io(o->ctx,3,first,buf,&ignored);
    if (r->preflush_error) return;
    for (unsigned i=0;i<LR_N;i++) {
        if (o->now_ns(o->ctx)>=deadline) { r->timed_out=1; break; }
        lr_pattern(buf,i);
        uint64_t start=o->now_ns(o->ctx), service=r->service[LR_WRITE].sum+r->service[LR_FLUSH].sum;
        r->touched=i+1; /* Include failed writes: they can partially modify a block. */
        int err=lr_measure(o,r,LR_WRITE,2,first+i,buf);
        if (!err) err=lr_measure(o,r,LR_FLUSH,3,first+i,buf);
        lr_add(&r->wall[LR_DURABLE],o->now_ns(o->ctx)-start,err);
        lr_add(&r->service[LR_DURABLE],r->service[LR_WRITE].sum+r->service[LR_FLUSH].sum-service,err);
        if (err) break;
    }
    if (r->wall[LR_DURABLE].n==LR_N) {
        for (unsigned i=0;i<LR_N;i++) {
            if (o->now_ns(o->ctx)>=deadline) { r->timed_out=1; break; }
            if (lr_measure(o,r,LR_READ,0,first+i,buf)) break;
            lr_pattern(expected,i);
            if (memcmp(buf,expected,LR_B)) { r->mismatches++; break; }
        }
    }
    /* Restoration has its own allowance (the kernel's 10-minute return bound).
     * Do not abandon cleanup simply because the measurement budget expired. */
    for (unsigned i=0;i<r->touched;i++) {
        if (!lr_measure(o,r,LR_RESTORE,2,first+i,backup[i])) r->restored++;
    }
    if (r->touched) {
        r->restore_flush_error=o->io(o->ctx,3,first,buf,&ignored);
        for (unsigned i=0;i<r->touched;i++) {
            if (lr_measure(o,r,LR_VERIFY,0,first+i,buf)) continue;
            if (memcmp(buf,backup[i],LR_B)) r->mismatches++;
            else r->verified++;
        }
    }
    r->complete=r->wall[LR_READ].n==LR_N && r->wall[LR_DURABLE].n==LR_N &&
        r->restored==LR_N && r->verified==LR_N && !r->mismatches &&
        !r->restore_flush_error && !r->timed_out;
}
