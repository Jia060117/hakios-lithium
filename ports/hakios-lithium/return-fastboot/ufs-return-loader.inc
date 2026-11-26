/* SPDX-License-Identifier: MIT
 * UFS integration for the separate, explicit ufs_return=1 loader route.
 * Included only in HAKIOS_LITHIUM_UFS_PERF, after the existing seed globals.
 * Common wire definitions are owned by the kernel integrator, unchanged here.
 */
#include "ufs-return-profile.h"
#include "ufs-return-receipt.h"

static unsigned lithium_fastboot_return_ufs;
static uint8_t *lithium_fastboot_ufs_receipt;
static struct r39_warm_ufs_owner lithium_fastboot_ufs_owner;

static __attribute__((noreturn)) void
lithium_fastboot_ufs_retain(const char *klog, uint64_t bytes)
{
    /* This route is terminal even on failure. Never resume command dispatch,
     * recover a transfer list, dump logs to Block I/O, or use generic reset. */
    print("LITHIUM FASTBOOT status=retained mode=ufs storage_callbacks=0 retry=0\n");
    compose_logs(klog, bytes);
    lithium_return_screen(klog, bytes);
    for (;;) {
        BS->stall(100000);
        wdt_pet();
    }
}

static int
lithium_fastboot_ufs_seed(const char *args, uint32_t bytes, unsigned copies,
    uint64_t low, uint64_t high, uint64_t checkpoint)
{
    static const efi_guid guid = { LFB_PROTOCOL_GUID_VALUES };
    uint8_t *candidate = NULL, *receipt = NULL;
    unsigned matches = 0, receipts = 0;
    if (!args || !bytes || bytes > RFBT_ARGS_MAX || copies != 1 ||
        args[bytes - 1] != 0 || checkpoint || lithium_fastboot_once.used ||
        lithium_fastboot_owner_affinity != 0 || high <= low ||
        low > UINT64_MAX - (RFBT_RECORD_ALIGNMENT - 1) ||
        high - low < RFBT_WIRE_SIZE ||
        !rfbt_ufs_return_profile(args, bytes - 1, 0) ||
        rfbt_parse_args(args, bytes - 1, 1, &lithium_fastboot_requested) != RFBT_OK) {
        print("LITHIUM FASTBOOT status=blocked mode=ufs phase=seed reason=profile\n");
        return -1;
    }
    efi_status status = BS->locate_protocol(&guid, NULL,
        (void **)&lithium_fastboot_firmware);
    struct lithium_fastboot_protocol *f = lithium_fastboot_firmware;
    if (EFI_ERROR(status) || !f || f->revision != LFB_PROTOCOL_REVISION ||
        f->size != sizeof(*f) || f->profile != LFB_PROFILE_UFS ||
        f->capabilities != LFB_CAP_UFS_FINALIZED || !f->prepare || !f->commit) {
        lithium_fastboot_firmware = NULL;
        print("LITHIUM FASTBOOT status=blocked mode=ufs phase=seed reason=firmware_protocol\n");
        return -1;
    }
    const uint64_t first = (low + RFBT_RECORD_ALIGNMENT - 1) &
        ~((uint64_t)RFBT_RECORD_ALIGNMENT - 1);
    for (uint64_t p = first; p <= high - RFBT_WIRE_SIZE;
         p += RFBT_RECORD_ALIGNMENT) {
        if (*(const uint64_t *)(uintptr_t)p == RFBT_MAGIC &&
            rfbt_initial_check((const void *)(uintptr_t)p, RFBT_WIRE_SIZE, 0) == RFBT_OK) {
            candidate = (uint8_t *)(uintptr_t)p;
            if (++matches > 1) break;
        }
    }
    for (uint64_t p = first; p <= high - LUR_SIZE;
         p += RFBT_RECORD_ALIGNMENT) {
        if (*(const uint64_t *)(uintptr_t)p == LUR_MAGIC &&
            lur_initial((const void *)(uintptr_t)p, 0)) {
            receipt = (uint8_t *)(uintptr_t)p;
            if (++receipts > 1) break;
        }
    }
    if (matches != 1 || receipts != 1 ||
        rfbt_ranges_overlap(candidate, RFBT_WIRE_SIZE, receipt, LUR_SIZE)) {
        print("LITHIUM FASTBOOT status=blocked mode=ufs phase=seed records=%d receipts=%d\n",
            (uint64_t)matches, (uint64_t)receipts);
        return -1;
    }
    uint64_t cookie = READ_SYSREG(CNTVCT_EL0) ^
        (uint64_t)(uintptr_t)candidate ^ RFBT_MAGIC;
    if (!cookie) cookie = 1;
    if (!r39_bind_current_warm_owner(cookie, &lithium_fastboot_ufs_owner)) {
        print("LITHIUM FASTBOOT status=blocked mode=ufs phase=seed reason=warm_owner\n");
        return -1;
    }
    if (rfbt_seed_initial(candidate, RFBT_WIRE_SIZE, cookie) != RFBT_OK ||
        !lur_seed(receipt, cookie)) return -1;
    clean_dcache((uint64_t)(uintptr_t)candidate,
        (uint64_t)(uintptr_t)candidate + RFBT_WIRE_SIZE);
    clean_dcache((uint64_t)(uintptr_t)receipt,
        (uint64_t)(uintptr_t)receipt + LUR_SIZE);
    lithium_fastboot_handoff = candidate;
    lithium_fastboot_ufs_receipt = receipt;
    lithium_fastboot_cookie = cookie;
    lithium_fastboot_return_smp = 1;
    print("LITHIUM FASTBOOT status=seeded mode=ufs delay_seconds=%d receipt_magic=%x\n",
        (uint64_t)lithium_fastboot_requested.delay_seconds, (uint64_t)LUR_MAGIC);
    return 1;
}

/* Only after all three XNU CPUs are independently confirmed OFF and rfbt
 * verifies normal return, current cookie, UFS_SYNC_OK, and BOTH USB drains.
 * No storage callbacks may follow this check; protocol snapshot only reads
 * controller registers and firmware state. The mailbox itself stays sealed. */
static int
lithium_fastboot_ufs_validate(const struct rfbt_record *record)
{
    struct lur_result receipt = {0};
    struct r39_warm_ufs_owner current = {0};
    uint8_t *wire = lithium_fastboot_ufs_receipt;
    if (!wire || record->storage != RFBT_STORAGE_UFS_SYNC_OK) return 0;
    flush_dcache((uint64_t)(uintptr_t)wire,
        (uint64_t)(uintptr_t)wire + LUR_SIZE);
    if (!lur_decode(wire, lithium_fastboot_cookie, &receipt) ||
        !r39_bind_current_warm_owner(lithium_fastboot_cookie, &current) ||
        current.boot_cookie != lithium_fastboot_ufs_owner.boot_cookie ||
        current.hc_pa != lithium_fastboot_ufs_owner.hc_pa ||
        current.utrl_pa != lithium_fastboot_ufs_owner.utrl_pa ||
        current.part_bio != lithium_fastboot_ufs_owner.part_bio ||
        current.media_id != lithium_fastboot_ufs_owner.media_id) {
        print("LITHIUM FASTBOOT status=retained mode=ufs reason=receipt_or_owner\n");
        return 0;
    }
    enum r39_ufs_proof_result error = r39_ufs_return_validate(
        lithium_fastboot_cookie, receipt.flush_request, &current, r39_warm_ufs_mmio_read32);
    if (error != R39_UFS_PROOF_OK) {
        print("LITHIUM FASTBOOT status=retained mode=ufs reason=mailbox_or_controller error=%d\n",
            (uint64_t)error);
        return 0;
    }
    return 1;
}
