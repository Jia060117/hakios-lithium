/* MIX userdata contract. No GPT changes. Runtime writes reach Data only.
 * Synchronized with config/hakios-lithium-storage.json and the loader/kernel.
 * The GPT is read and checked at DXE entry before writes can be enabled.
 */
#ifndef LITHIUM_STORAGE_H
#define LITHIUM_STORAGE_H
#define LITHIUM_STORAGE_FIRST 98304ULL
#define LITHIUM_STORAGE_BLOCKS 61208543ULL
#define LITHIUM_DATA_FIRST 2042368ULL
#define LITHIUM_IMAGE_BLOCKS 5242880ULL
#define LITHIUM_WRITE_FIRST (LITHIUM_STORAGE_FIRST + LITHIUM_DATA_FIRST)
#define LITHIUM_WRITE_END (LITHIUM_STORAGE_FIRST + LITHIUM_IMAGE_BLOCKS)
/* R12 diagnostic only: 100 blocks after a 1 MiB gap outside the installed image.
 * No partition or filesystem is extended. Loader opt-in and kernel pre-launch
 * checks are independent gates; ordinary Data I/O retains its original bounds.
 */
#define LITHIUM_SCRATCH_FIRST (LITHIUM_IMAGE_BLOCKS + 256ULL)
#define LITHIUM_SCRATCH_BLOCKS 100ULL
static inline int lithium_scratch_range(unsigned long long lba, unsigned long long blocks) {
  return blocks != 0 && lba >= LITHIUM_SCRATCH_FIRST &&
    lba < LITHIUM_SCRATCH_FIRST + LITHIUM_SCRATCH_BLOCKS &&
    blocks <= LITHIUM_SCRATCH_FIRST + LITHIUM_SCRATCH_BLOCKS - lba;
}
static inline int lithium_write_range(unsigned long long lba, unsigned long long blocks) {
  return (blocks != 0 && lba >= LITHIUM_WRITE_FIRST && lba < LITHIUM_WRITE_END &&
    blocks <= LITHIUM_WRITE_END - lba) ||
    (lba >= LITHIUM_STORAGE_FIRST && lithium_scratch_range(lba - LITHIUM_STORAGE_FIRST, blocks));
}
#endif
