/* SPDX-License-Identifier: BSD-2-Clause */
#include "g2-reserve-mu.h"
_Static_assert(sizeof(UINTN)==8,"AArch64 Mu ABI required");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR)==40,"UEFI v1 descriptor");
_Static_assert(offsetof(EFI_MEMORY_DESCRIPTOR,PhysicalStart)==8,"EFI descriptor PA");
_Static_assert(offsetof(EFI_MEMORY_DESCRIPTOR,NumberOfPages)==24,"EFI descriptor pages");
_Static_assert(offsetof(EFI_MEMORY_DESCRIPTOR,Attribute)==32,"EFI descriptor attributes");
_Static_assert(EfiConventionalMemory==LG2R_EFI_CONVENTIONAL,"EFI memory type");
_Static_assert(EfiLoaderData==LG2R_EFI_LOADER_DATA,"EFI loader data");
_Static_assert(EfiBootServicesData==LG2R_EFI_BOOT_DATA,"EFI boot data");
static int get_map(void *cookie, void *buffer, size_t *bytes, uint64_t *key,
                   size_t *stride, uint32_t *version) {
    struct lg2r_mu *mu=(struct lg2r_mu *)cookie;
    UINTN n=(UINTN)*bytes,k=0,d=0;
    UINT32 v=0;
    mu->last_efi_status=mu->boot->GetMemoryMap(&n,(EFI_MEMORY_DESCRIPTOR *)buffer,&k,&d,&v);
    *bytes=(size_t)n;*key=(uint64_t)k;*stride=(size_t)d;*version=v;
    return mu->last_efi_status==EFI_SUCCESS?0:-1;
}
static int allocate_exact(void *cookie, uint32_t type, uint64_t pages, uint64_t *pa) {
    struct lg2r_mu *mu=(struct lg2r_mu *)cookie;
    EFI_PHYSICAL_ADDRESS address=*pa;
    mu->last_efi_status=mu->boot->AllocatePages(AllocateAddress,(EFI_MEMORY_TYPE)type,(UINTN)pages,&address);
    if (mu->last_efi_status!=EFI_SUCCESS) return -1;
    *pa=address;
    return 0;
}
enum lg2r_status lg2r_mu_reserve_stock(struct lg2r_context *c, struct lg2r_mu *mu,
                                     const struct lg2r_config *cfg, const struct lg2_image *im,
                                     void *buffer, size_t capacity) {
    if (!mu || !mu->boot || !mu->boot->GetMemoryMap || !mu->boot->AllocatePages ||
        !buffer || ((uintptr_t)buffer&7u)) return LG2R_ARGUMENT;
    struct lg2r_ops ops={mu,get_map,allocate_exact,mu->policy};
    return lg2r_reserve_stock(c,&ops,cfg,im,buffer,capacity);
}
