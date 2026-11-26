/* SPDX-License-Identifier: MIT
 * Included by the complete HakiOS loader after its firmware/cache helpers.
 * Return reset I/O requires an explicit option and a valid kernel proof.
 * The separate RAM-only CPU experiment runs before XNU only after this
 * loader has admitted the isolated profile and seeded its return record.
 */
#include "profile.h"
#include "ram-smp-profile.h"
#define LFB_UINT32 uint32_t
#define LFB_UINT64 uint64_t
#define LFB_STATUS efi_status
#define LFB_EFIAPI EFIAPI
#include "firmware-protocol.h"
#undef LFB_UINT32
#undef LFB_UINT64
#undef LFB_STATUS
#undef LFB_EFIAPI

static struct rfbt_config lithium_fastboot_requested;
static struct rfbt_once lithium_fastboot_once;
static uint8_t *lithium_fastboot_handoff;
static uint64_t lithium_fastboot_cookie;
static struct lithium_fastboot_protocol *lithium_fastboot_firmware;
static unsigned lithium_fastboot_return_smp;
static uint64_t lithium_fastboot_owner_affinity;
#if defined(HAKIOS_LITHIUM_RAM_DIAGNOSTIC) && defined(HAKIOS_LITHIUM_UFS_PERF)
#error "CPU initialization profiles must be built separately"
#endif
#if defined(HAKIOS_LITHIUM_RAM_DIAGNOSTIC) || defined(HAKIOS_LITHIUM_UFS_PERF)
#include "cpu-perf-uefi-r38.inc"
#endif

#if defined(HAKIOS_LITHIUM_UFS_PERF)
#include "ufs-return-loader.inc"
#endif

static void
lithium_fastboot_seed(const char *args, uint32_t bytes, unsigned copies,
    uint64_t low, uint64_t high, uint64_t checkpoint)
{
    static const efi_guid guid = { LFB_PROTOCOL_GUID_VALUES };
    uint8_t *candidate = NULL;
    unsigned matches = 0;
    lithium_fastboot_handoff = NULL;
    lithium_fastboot_firmware = NULL;
    lithium_fastboot_cookie = 0;
    lithium_fastboot_return_smp = 0;
    lithium_fastboot_owner_affinity = READ_SYSREG(MPIDR_EL1) & 0xff00ffffffULL;
    rfbt_config_default(&lithium_fastboot_requested);
#if defined(HAKIOS_LITHIUM_UFS_PERF)
    /* Select before any admission failure: this explicit route must never
     * fall into controller recovery or the legacy logdump on return. */
    lithium_fastboot_return_ufs = args && bytes && bytes <= RFBT_ARGS_MAX &&
        args[bytes - 1] == 0 && rfbt_ufs_return_profile(args, bytes - 1, 0);
    lithium_fastboot_ufs_receipt = NULL;
    lithium_fastboot_ufs_owner = (struct r39_warm_ufs_owner){0};
    if (lithium_fastboot_return_ufs &&
        lithium_fastboot_ufs_seed(args, bytes, copies, low, high, checkpoint) < 0)
        lithium_fastboot_ufs_retain(NULL, 0);
    if (lithium_cpu_perf_ufs_prepare(args, bytes, copies, checkpoint) < 0) {
        print("LITHIUM PERF UEFI retained launch_aborted=1; use power and volume down for Fastboot\n");
        for (;;) {
            BS->stall(100000);
            wdt_pet();
        }
    }
#endif
#if defined(HAKIOS_LITHIUM_UFS_PERF)
    if (lithium_fastboot_return_ufs) return;
#endif
    if (!args || !bytes || bytes > RFBT_ARGS_MAX || copies != 1 || args[bytes - 1] != 0)
        return;
    if (rfbt_parse_args(args, bytes - 1, 1, &lithium_fastboot_requested) != RFBT_OK ||
        !lithium_fastboot_requested.enabled) return;
    int single_profile = rfbt_ram_profile(args, bytes - 1, 0);
    int smp_profile = rfbt_ram_smp_profile(args, bytes - 1, 0);
    if (lithium_fastboot_once.used || checkpoint ||
        (single_profile == smp_profile) ||
        block_enabled || block_write_requested || hc_requested || high <= low ||
        high - low < RFBT_WIRE_SIZE) {
        print("LITHIUM FASTBOOT status=blocked phase=seed reason=profile_or_consumed\n");
        return;
    }
    efi_status status = BS->locate_protocol(&guid, NULL, (void **)&lithium_fastboot_firmware);
    struct lithium_fastboot_protocol *f = lithium_fastboot_firmware;
    if (EFI_ERROR(status) || !f || f->revision != LFB_PROTOCOL_REVISION ||
        f->size != sizeof(*f) || f->profile != LFB_PROFILE_RAM_ISOLATED ||
        f->capabilities != LFB_CAP_NO_PHYSICAL_UFS || !f->prepare || !f->commit) {
        lithium_fastboot_firmware = NULL;
        print("LITHIUM FASTBOOT status=blocked phase=seed reason=firmware_protocol\n");
        return;
    }
    /* Scan only aligned, complete INITIAL candidates. Multiple candidates are
     * rejected; an instruction literal or a stale encoded record is not one. */
    for (uint64_t p = (low + RFBT_RECORD_ALIGNMENT - 1) &
            ~((uint64_t)RFBT_RECORD_ALIGNMENT - 1);
         p <= high - RFBT_WIRE_SIZE; p += RFBT_RECORD_ALIGNMENT) {
        if (*(const uint64_t *)(uintptr_t)p == RFBT_MAGIC &&
            rfbt_initial_check((const void *)(uintptr_t)p, RFBT_WIRE_SIZE, 0) == RFBT_OK) {
            candidate = (uint8_t *)(uintptr_t)p;
            if (++matches > 1) break;
        }
    }
    if (matches != 1) {
        print("LITHIUM FASTBOOT status=blocked phase=seed candidates=%d\n", (uint64_t)matches);
        return;
    }
    uint64_t cookie = READ_SYSREG(CNTVCT_EL0) ^ (uint64_t)(uintptr_t)candidate ^ RFBT_MAGIC;
    if (!cookie) cookie = 1; /* Per-invocation discriminator, not a security secret. */
    if (rfbt_seed_initial(candidate, RFBT_WIRE_SIZE, cookie) != RFBT_OK) return;
    clean_dcache((uint64_t)(uintptr_t)candidate,
        (uint64_t)(uintptr_t)candidate + RFBT_WIRE_SIZE);
    lithium_fastboot_handoff = candidate;
    lithium_fastboot_cookie = cookie;
    lithium_fastboot_return_smp = (unsigned)smp_profile;
    print("LITHIUM FASTBOOT status=seeded mode=isolated_ram delay_seconds=%d\n",
        (uint64_t)lithium_fastboot_requested.delay_seconds);
#if defined(HAKIOS_LITHIUM_RAM_DIAGNOSTIC)
    if (smp_profile &&
        lithium_cpu_perf_uefi_r38(args, bytes - 1, cookie) < 0) {
        print("LITHIUM PERF UEFI retained launch_aborted=1; use power and volume down for Fastboot\n");
        for (;;) {
            BS->stall(100000);
            wdt_pet();
        }
    }
#endif
}

static void
lithium_fastboot_after_return(uint64_t reason, uint64_t syndrome)
{
    struct rfbt_record result;
    struct lithium_fastboot_proof proof = {0};
    struct lithium_fastboot_result hardware = {0};
    uint8_t *wire = lithium_fastboot_handoff;
    if (!lithium_fastboot_requested.enabled || !wire || !lithium_fastboot_firmware) return;
    if (lithium_fastboot_return_smp &&
        (READ_SYSREG(MPIDR_EL1) & 0xff00ffffffULL) != lithium_fastboot_owner_affinity) {
        print("LITHIUM FASTBOOT status=retained reason=smp_return_affinity\n");
        return;
    }
    if (lithium_fastboot_return_smp) {
        /* run_on_other_cpu resumes this original loader core. The XNU boot
         * core sets other_cpu_done before its CPU_OFF call, so independently
         * confirm that it and the two secondaries have actually powered off. */
        static const uint64_t kernel_affinities[3] = {1, 0x100, 0x101};
        uint64_t hz = READ_SYSREG(CNTFRQ_EL0), begin = READ_SYSREG(CNTVCT_EL0);
        if (!hz || hz > 0xffffffffULL) return;
        for (unsigned cpu = 0; cpu < 3; ++cpu) {
            int off = 0;
            for (unsigned poll = 0; poll < 1000; ++poll) {
                int64_t state = psci_call(PSCI_AFFINITY_INFO, kernel_affinities[cpu], 0, 0);
                if (state == 1) { off = 1; break; }
                uint64_t now = READ_SYSREG(CNTVCT_EL0);
                if (state < 0 || now < begin || now - begin >= hz) break;
                BS->stall(1000);
            }
            if (!off) {
                print("LITHIUM FASTBOOT status=retained reason=kernel_cpu_not_off affinity=%x\n",
                    kernel_affinities[cpu]);
                return;
            }
        }
        print("LITHIUM FASTBOOT status=smp_return_verified owner=%x kernel_cpus_off=3\n",
            lithium_fastboot_owner_affinity);
    }
    /* XNU cleaned its final record before branching; invalidate the loader's
     * view after the independent returned-core/context handshake. */
    flush_dcache((uint64_t)(uintptr_t)wire, (uint64_t)(uintptr_t)wire + RFBT_WIRE_SIZE);
    uint32_t error = rfbt_decode(wire, RFBT_WIRE_SIZE, &result);
    if (error == RFBT_OK) error = rfbt_eligible(&result, &lithium_fastboot_requested,
        lithium_fastboot_cookie, reason, syndrome);
    if (error != RFBT_OK) {
        print("LITHIUM FASTBOOT status=retained reason=kernel_proof error=%d ctx=%x\n",
            (uint64_t)error, reason);
        return;
    }
    uint64_t freq = READ_SYSREG(CNTFRQ_EL0), start = READ_SYSREG(CNTVCT_EL0), now = start;
    if (!freq || freq > 0xffffffffULL) {
        print("LITHIUM FASTBOOT status=retained reason=timer_frequency\n");
        return;
    }
    uint64_t ticks = (uint64_t)result.delay_seconds * freq;
    unsigned polls = 0, limit = result.delay_seconds * 20u + 20u;
    print("LITHIUM FASTBOOT status=countdown seconds=%d primary_target=%d primary_tail=%d\n",
        (uint64_t)result.delay_seconds, result.primary_target, result.kernel_tail);
    print("LITHIUM FASTBOOT final_target=%d final_tail=%d host_persistence=unverified\n",
        result.final_target, result.tail_tail);
    while (now >= start && now - start < ticks && polls++ < limit) {
        BS->stall(100000);
        wdt_pet();
        now = READ_SYSREG(CNTVCT_EL0);
    }
    if (now < start || now - start < ticks) {
        print("LITHIUM FASTBOOT status=retained reason=countdown_clock\n");
        return;
    }
#if defined(HAKIOS_LITHIUM_UFS_PERF)
    if (lithium_fastboot_return_ufs && !lithium_fastboot_ufs_validate(&result)) return;
#endif
    uint64_t elapsed = now - start;
    uint64_t waited_ms = (elapsed / freq) * 1000 + ((elapsed % freq) * 1000) / freq;
    error = rfbt_loader_claim(&lithium_fastboot_once, wire, RFBT_WIRE_SIZE,
        &lithium_fastboot_requested, lithium_fastboot_cookie, reason, syndrome, waited_ms);
    clean_dcache((uint64_t)(uintptr_t)wire, (uint64_t)(uintptr_t)wire + RFBT_WIRE_SIZE);
    if (error != RFBT_OK) {
        print("LITHIUM FASTBOOT status=retained reason=claim error=%d\n", (uint64_t)error);
        return;
    }
    proof.revision = LFB_PROTOCOL_REVISION;
    proof.size = sizeof(proof);
    proof.profile = LFB_PROFILE_RAM_ISOLATED;
    proof.storage = LFB_STORAGE_RAM;
    proof.flags = LFB_PROOF_RAM_FLAGS;
#if defined(HAKIOS_LITHIUM_UFS_PERF)
    if (lithium_fastboot_return_ufs) {
        proof.profile = LFB_PROFILE_UFS;
        proof.storage = LFB_STORAGE_UFS;
        proof.flags = LFB_PROOF_UFS_FLAGS;
    }
#endif
    proof.boot_cookie = result.boot_cookie;
    proof.generation = result.boot_cookie;
    proof.primary_target = result.primary_target;
    proof.final_target = result.final_target;
    proof.tail_tail = result.tail_tail;
    hardware.revision = LFB_PROTOCOL_REVISION;
    hardware.size = sizeof(hardware);
    efi_status status = lithium_fastboot_firmware->prepare(lithium_fastboot_firmware,
        &proof, &hardware);
    print("LITHIUM FASTBOOT phase=firmware_prepare status=%x state=%d flags=%x detail=%x\n",
        status, (uint64_t)hardware.state, (uint64_t)hardware.flags, hardware.detail);
    print("LITHIUM FASTBOOT imem_before=%x imem_after=%x pmic_before=%x pmic_after=%x\n",
        (uint64_t)hardware.imem_before, (uint64_t)hardware.imem_after,
        (uint64_t)hardware.pmic_before, (uint64_t)hardware.pmic_after);
    if (!EFI_ERROR(status) && hardware.revision == LFB_PROTOCOL_REVISION &&
        hardware.size == sizeof(hardware) && hardware.state == LFB_STATE_PREPARED &&
        hardware.backend == LFB_BACKEND_LITHIUM && hardware.ticket &&
        hardware.flags == (LFB_FLAG_IMEM_VERIFIED | LFB_FLAG_PMIC_VERIFIED)) {
        print("LITHIUM FASTBOOT status=commit attempt=1\n");
        status = lithium_fastboot_firmware->commit(lithium_fastboot_firmware, hardware.ticket);
    }
    (void)rfbt_handoff_returned(&lithium_fastboot_once, wire, RFBT_WIRE_SIZE);
    clean_dcache((uint64_t)(uintptr_t)wire, (uint64_t)(uintptr_t)wire + RFBT_WIRE_SIZE);
    print("LITHIUM FASTBOOT status=retained reason=firmware_returned status=%x retry=0\n", status);
}
