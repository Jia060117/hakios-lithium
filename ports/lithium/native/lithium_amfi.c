/*
 * LITHIUM dyld policy-query compatibility for the iOS 14 ARM64 ABI.
 * The firmware dyld-851.31 calls __mac_syscall("AMFI", 0x5a, &request),
 * where request is { uint64_t input_flags; uint64_t output_user_address; }.
 *
 * Zero output flags deny every optional dyld override (including @paths,
 * environment path/cache overrides, fallback paths and library interposing).
 * A separate pinned-firmware membership callback supplies platform identity.
 * No entitlement or credential hooks are provided. This is a compatibility
 * adapter, not an implementation of Apple's complete AMFI subsystem.
 * ABI reference: pinned ios-sailfish patch 0139; output policy deliberately
 * differs. Actual firmware instructions and dyld-851.27 source were checked.
 */
#if defined(ARM64_BOARD_CONFIG_LITHIUM) && CONFIG_MACF
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <security/mac_policy.h>

struct lithium_dyld_policy_request {
    uint64_t input_flags;
    uint64_t output_address;
};
_Static_assert(sizeof(struct lithium_dyld_policy_request) == 16, "dyld query ABI");

static int
lithium_amfi_syscall(struct proc *p, int call, user_addr_t arg)
{
    struct lithium_dyld_policy_request request;
    const uint64_t output_flags = 0;
    int error;

    if (call != 0x5a) {
        return ENOPOLICY;
    }
    /* This port implements the verified 64-bit data ABI only. */
    if (!proc_is64bit_data(p)) {
        return ENOTSUP;
    }
    error = copyin(arg, &request, sizeof(request));
    if (error != 0) {
        return error;
    }
    if (request.output_address == 0) {
        return EFAULT;
    }
    /* Input flags cannot grant capabilities: every request gets the same
     * restrictive policy, including requests with unknown future bits. */
    error = copyout(&output_flags, (user_addr_t)request.output_address,
        sizeof(output_flags));
    if (error == 0 && proc_pid(p) == 1) {
        printf("LITHIUM AMFI: PID 1 dyld policy query accepted; flags=0\n");
    }
    return error;
}

extern int lithium_firmware_check_signature(struct vnode *, struct label *,
    cpu_type_t, struct cs_blob *, unsigned int *, unsigned int *, int,
    unsigned int, char **, size_t *);

static const struct mac_policy_ops lithium_amfi_ops = {
    .mpo_policy_syscall = lithium_amfi_syscall,
    .mpo_vnode_check_signature = lithium_firmware_check_signature,
};

static mac_policy_handle_t lithium_amfi_handle;
static struct mac_policy_conf lithium_amfi_conf = {
    .mpc_name = "AMFI",
    .mpc_fullname = "LITHIUM restrictive dyld policy query",
    .mpc_ops = &lithium_amfi_ops,
    .mpc_loadtime_flags = MPC_LOADTIME_FLAG_NOTLATE,
};

void lithium_amfi_register(void);
void
lithium_amfi_register(void)
{
    int error = mac_policy_register(&lithium_amfi_conf, &lithium_amfi_handle, NULL);
    if (error != 0) {
        panic("LITHIUM AMFI: policy registration failed: %d", error);
    }
    printf("LITHIUM AMFI: early dyld policy registered; optional permissions=0\n");
}
#endif
