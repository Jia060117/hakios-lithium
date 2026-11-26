/* SPDX-License-Identifier: MIT
 * Lithium UEFI Fastboot-return protocol, version 2.
 * The loader must locate this protocol; never fall back to ResetSystem when it
 * is absent. Old firmware ignores ResetData and would perform a normal reset.
 * Supply LFB_UINT32/LFB_UINT64/LFB_STATUS/LFB_EFIAPI before including this file.
 */
#ifndef LITHIUM_RETURN_FASTBOOT_FIRMWARE_PROTOCOL_H
#define LITHIUM_RETURN_FASTBOOT_FIRMWARE_PROTOCOL_H

#define LFB_PROTOCOL_GUID_VALUES \
    0x9fef394d, 0x21f1, 0x4bd4, {0xb9,0xc4,0x60,0xf0,0x82,0x53,0xea,0x61}
#define LFB_PROTOCOL_REVISION 0x00020000u
#define LFB_RESULT_SIZE 64u
#define LFB_PROOF_SIZE 64u
#define LFB_BACKEND_NONE 0u
#define LFB_BACKEND_LITHIUM 1u
#define LFB_STATE_IDLE 0u
#define LFB_STATE_PREPARED 1u
#define LFB_STATE_CONSUMED 2u
#define LFB_STATE_FAILED 3u
#define LFB_FLAG_IMEM_VERIFIED 1u
#define LFB_FLAG_PMIC_VERIFIED 2u
#define LFB_PROFILE_UNSPECIFIED 0u
#define LFB_PROFILE_RAM_ISOLATED 1u
#define LFB_PROFILE_UFS 2u
#define LFB_STORAGE_UFS 1u
#define LFB_STORAGE_RAM 2u
#define LFB_CAP_NO_PHYSICAL_UFS 1u
#define LFB_CAP_UFS_FINALIZED 2u

#define LFB_PROOF_DATA_UNMOUNTED 1u
#define LFB_PROOF_WRITE_SEALED 2u
#define LFB_PROOF_DEVICE_FLUSHED 4u
#define LFB_PROOF_USB_DRAINED 8u
#define LFB_PROOF_NORMAL_TIMER 16u
#define LFB_PROOF_SMP_STOPPED 32u
#define LFB_PROOF_UFS_FLAGS 0x3fu
#define LFB_PROOF_RAM_FLAGS 0x38u

struct lithium_fastboot_proof {
    LFB_UINT32 revision;
    LFB_UINT32 size;
    LFB_UINT32 profile;
    LFB_UINT32 storage;
    LFB_UINT32 flags;
    LFB_UINT32 reserved;
    LFB_UINT64 boot_cookie;
    LFB_UINT64 generation;
    LFB_UINT64 primary_target;
    LFB_UINT64 final_target;
    LFB_UINT64 tail_tail;
};

struct lithium_fastboot_result {
    LFB_UINT32 revision;
    LFB_UINT32 size;
    LFB_UINT32 state;
    LFB_UINT32 backend;
    LFB_UINT32 flags;
    LFB_UINT32 imem_before;
    LFB_UINT32 imem_after;
    LFB_UINT32 pmic_before;
    LFB_UINT32 pmic_after;
    LFB_UINT32 reserved;
    LFB_UINT64 ticket;
    LFB_UINT64 detail;
    LFB_UINT64 affinity;
};

struct lithium_fastboot_protocol;
typedef LFB_STATUS (LFB_EFIAPI *lithium_fastboot_prepare_fn)(
    struct lithium_fastboot_protocol *,
    const struct lithium_fastboot_proof *, struct lithium_fastboot_result *);
typedef LFB_STATUS (LFB_EFIAPI *lithium_fastboot_commit_fn)(
    struct lithium_fastboot_protocol *, LFB_UINT64 ticket);

struct lithium_fastboot_protocol {
    LFB_UINT64 revision;
    LFB_UINT64 size;
    LFB_UINT32 profile;
    LFB_UINT32 capabilities;
    lithium_fastboot_prepare_fn prepare;
    lithium_fastboot_commit_fn commit;
};

/* PREPARE may only be called after a verified normal kernel return. It stages
 * a board-supported bootloader request and reports actual readback. COMMIT is
 * a one-shot terminal request. Any returned status retains the failure screen;
 * the caller must not retry, use generic reboot, or launch another image.
 * Protocol presence/version establishes software compatibility, not successful
 * hardware operation. All physical results require a separate device test.
 */
#endif
