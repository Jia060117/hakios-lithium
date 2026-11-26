#ifndef R39_WARM_UFS_OWNER_PROTOCOL_H
#define R39_WARM_UFS_OWNER_PROTOCOL_H

/* Native AArch64 UEFI ABI shared by the loader and LithiumUfsDxe. */
#define R39_WARM_OWNER_GUID \
	{ 0x128bd2f8, 0x363b, 0x46d4, { 0xae, 0x02, 0x27, 0xfb, 0xcb, 0x2f, 0x1f, 0xc1 } }
#define R39_WARM_OWNER_REVISION 1u

#if defined(R39_WARM_OWNER_MU)
typedef EFI_STATUS r39_warm_owner_status;
#else
typedef efi_status r39_warm_owner_status;
#endif

struct r39_warm_owner_snapshot {
	uint64_t hc_pa;
	uint64_t utrl_pa;
	const void *block_io;
};

struct r39_warm_owner_protocol {
	uint32_t revision;
	uint32_t size;
	r39_warm_owner_status (EFIAPI *snapshot)(const struct r39_warm_owner_protocol *self,
	    struct r39_warm_owner_snapshot *result);
};

_Static_assert(sizeof(struct r39_warm_owner_snapshot) == 24, "warm owner receipt ABI");
_Static_assert(sizeof(struct r39_warm_owner_protocol) == 16, "warm owner protocol ABI");
#endif
