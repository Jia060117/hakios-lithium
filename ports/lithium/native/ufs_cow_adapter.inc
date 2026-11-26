/* Called with the UFS mutex, or before the device is exposed. */
static int
ufs_lower_read(__unused void *ctx, uint64_t off, uint32_t count, void *dst)
{
    uint64_t bytes = su.nblocks * UFS_BLOCK;
    if (off > bytes || count > bytes - off) return EINVAL;
    for (uint32_t done = 0; done < count;) {
        uint64_t pos = off + done;
        uint32_t skip = pos % UFS_BLOCK;
        uint32_t want = MIN(count - done, BOUNCE_SIZE - skip);
        uint32_t blocks = (skip + want + UFS_BLOCK - 1) / UFS_BLOCK;
        int error = ufs_read_blocks(su.first_block + pos / UFS_BLOCK, blocks);
        if (error) return error;
        memcpy((uint8_t *)dst + done, (const void *)(su.dma + DMA_BOUNCE + skip), want);
        done += want;
    }
    return 0;
}
static void *ufs_cow_alloc(__unused void *ctx)
{
    /* NOWAIT cannot expand an empty zone. Permit expansion using currently
     * available pages, but never wait for paging from this disk's I/O path. */
    return kheap_alloc(KHEAP_DATA_BUFFERS, LC_BLOCK, Z_NOPAGEWAIT);
}
static void ufs_cow_free(__unused void *ctx, void *p)
{
    kheap_free(KHEAP_DATA_BUFFERS, p, LC_BLOCK);
}
static void ufs_cow_discard(void)
{
    if (root_cow.table) {
        for (uint32_t i = 0; i < root_cow.slots; i++)
            if (root_cow.table[i].key) ufs_cow_free(NULL, root_cow.table[i].data);
        kheap_free(KHEAP_DEFAULT, root_cow.table, LC_SLOTS * sizeof(struct lc_entry));
    }
    bzero(&root_cow, sizeof(root_cow));
    cow_enabled = false;
}
static int __attribute__((noinline))
ufs_cow_init(DTEntry node)
{
    const uint8_t *seed = NULL;
    unsigned int size = 0;
    if (SecureDTGetProperty(node, "ram-cow-seed", (const void **)&seed, &size) != kSuccess ||
        size < 32 || memcmp(seed, "LCOWSE01", 8) ||
        OSReadLittleInt64(seed, 8) != LC_ROOT_BYTES ||
        OSReadLittleInt32(seed, 20) != LC_BLOCK + 8 ||
        OSReadLittleInt32(seed, 24) != 1 || OSReadLittleInt32(seed, 28)) return EINVAL;
    uint32_t n = OSReadLittleInt32(seed, 16);
    if (!n || n > LC_LIMIT || size != 32 + n * (LC_BLOCK + 8)) return EINVAL;
    root_cow.table = kheap_alloc(KHEAP_DEFAULT, LC_SLOTS * sizeof(struct lc_entry), Z_WAITOK | Z_ZERO);
    if (!root_cow.table) return ENOMEM;
    root_cow.slots = LC_SLOTS;
    root_cow.limit = LC_LIMIT;
    root_cow.bytes = LC_ROOT_BYTES;
    root_cow.alloc = ufs_cow_alloc;
    root_cow.release = ufs_cow_free;
    root_cow.read_lower = ufs_lower_read;
    uint32_t prior = 0;
    int error = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *r = seed + 32 + i * (LC_BLOCK + 8);
        uint32_t block = OSReadLittleInt32(r, 0), expected = OSReadLittleInt32(r, 4), done;
        if (block >= LC_ROOT_BYTES / LC_BLOCK || (i && block <= prior)) { error = EINVAL; goto fail; }
        prior = block;
        error = ufs_read_blocks(su.first_block + block, 1);
        if (error) goto fail;
        if (lg_crc((const uint8_t *)(su.dma + DMA_BOUNCE), LC_BLOCK, 0) != expected) {
            printf("LITHIUM COW: base sector %u CRC rejected\n", block);
            error = EIO; goto fail;
        }
        error = lc_write(&root_cow, (uint64_t)block * LC_BLOCK, LC_BLOCK, r + 8, &done);
        if (error || done != LC_BLOCK) { if (!error) error = EIO; goto fail; }
    }
    cow_enabled = true;
    printf("LITHIUM COW: %u seed blocks verified; 64 MiB volatile limit; physical UFS read-only\n", n);
    return 0;
fail:
    ufs_cow_discard();
    return error;
}
int lithium_ufs_root_is_writable(dev_t dev);
int lithium_ufs_root_is_writable(dev_t dev)
{
    return su.ready && cow_enabled && dev == su.bdev;
}
