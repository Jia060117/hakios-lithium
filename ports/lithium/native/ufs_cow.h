/* Bounded volatile block overlay. Caller serializes all operations.
 * No lower write callback exists. Successful writes survive for this boot
 * only; ENOSPC/ENOMEM and lower read failures retain exact partial progress.
 * The caller provides a zeroed power-of-two table with >= 2*limit entries.
 */
#ifndef LITHIUM_UFS_COW_H
#define LITHIUM_UFS_COW_H
#define LC_BLOCK 4096u
#define LC_ROOT_BYTES (16ULL << 30)
#define LC_LIMIT 16384u
#define LC_SLOTS (LC_LIMIT * 2u)
struct lc_entry { uint32_t key; uint8_t *data; };
struct lc_cow {
    struct lc_entry *table;
    uint32_t slots, limit, used;
    uint64_t bytes;
    void *ctx;
    void *(*alloc)(void *);
    void (*release)(void *, void *);
    int (*read_lower)(void *, uint64_t, uint32_t, void *);
};
static struct lc_entry *lc_find(struct lc_cow *c, uint32_t block)
{
    uint32_t key = block + 1, i = (block * 2654435761u) & (c->slots - 1);
    for (uint32_t n = 0; n < c->slots; n++, i = (i + 1) & (c->slots - 1)) {
        if (!c->table[i].key || c->table[i].key == key) return &c->table[i];
    }
    return NULL;
}
static int lc_valid(struct lc_cow *c, uint64_t off, uint32_t len)
{
    return c->table && c->slots && !(c->slots & (c->slots - 1)) &&
        c->limit && c->limit <= c->slots / 2 && c->bytes &&
        !(c->bytes % LC_BLOCK) && c->bytes <= LC_ROOT_BYTES &&
        off <= c->bytes && len <= c->bytes - off;
}
static int __attribute__((noinline)) lc_read(struct lc_cow *c, uint64_t off, uint32_t len,
    void *dst, uint32_t *done)
{
    *done = 0;
    if (!lc_valid(c, off, len)) return EINVAL;
    while (*done < len) {
        uint64_t pos = off + *done;
        uint32_t block = (uint32_t)(pos / LC_BLOCK), skip = pos % LC_BLOCK;
        uint32_t n = LC_BLOCK - skip;
        if (n > len - *done) n = len - *done;
        struct lc_entry *e = lc_find(c, block);
        if (!e) return EIO;
        if (e->key) {
            memcpy((uint8_t *)dst + *done, e->data + skip, n);
        } else {
            /* Coalesce lower reads, retaining the driver's 1 MiB transfers. */
            while (n < len - *done) {
                uint32_t next = (uint32_t)((pos + n) / LC_BLOCK);
                struct lc_entry *q = lc_find(c, next);
                if (!q || q->key) break;
                uint32_t step = len - *done - n;
                if (step > LC_BLOCK) step = LC_BLOCK;
                n += step;
            }
            int err = c->read_lower(c->ctx, pos, n, (uint8_t *)dst + *done);
            if (err) return err;
        }
        *done += n;
    }
    return 0;
}
static int __attribute__((noinline)) lc_write(struct lc_cow *c, uint64_t off, uint32_t len,
    const void *src, uint32_t *done)
{
    *done = 0;
    if (!lc_valid(c, off, len)) return EINVAL;
    while (*done < len) {
        uint64_t pos = off + *done;
        uint32_t block = (uint32_t)(pos / LC_BLOCK), skip = pos % LC_BLOCK;
        uint32_t n = LC_BLOCK - skip;
        if (n > len - *done) n = len - *done;
        struct lc_entry *e = lc_find(c, block);
        if (!e) return ENOSPC;
        if (!e->key) {
            if (c->used >= c->limit) return ENOSPC;
            uint8_t *data = c->alloc(c->ctx);
            if (!data) return ENOMEM;
            if (skip || n != LC_BLOCK) {
                int err = c->read_lower(c->ctx, (uint64_t)block * LC_BLOCK, LC_BLOCK, data);
                if (err) { c->release(c->ctx, data); return err; }
            }
            memcpy(data + skip, (const uint8_t *)src + *done, n);
            e->data = data;
            e->key = block + 1;
            c->used++;
        } else {
            memcpy(e->data + skip, (const uint8_t *)src + *done, n);
        }
        *done += n;
    }
    return 0;
}
#endif
