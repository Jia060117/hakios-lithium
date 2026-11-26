/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LITHIUM_GPU_G2_RESERVE_MU_H
#define LITHIUM_GPU_G2_RESERVE_MU_H
#include <Uefi.h>
#include "g2-reserve.h"
struct lg2r_mu {
    EFI_BOOT_SERVICES *boot;
    struct lg2r_policy policy;
    EFI_STATUS last_efi_status;
};
/* OS-loader/boot-driver hook only; not registered or called by this package.
 * It never allocates the map buffer, installs a reservation policy, changes
 * memory attributes or calls SMC. All four platform policy callbacks are
 * mandatory; passing only boot services does not authorize allocation.
 */
enum lg2r_status lg2r_mu_reserve_stock(struct lg2r_context *, struct lg2r_mu *,
                                     const struct lg2r_config *, const struct lg2_image *,
                                     void *map_buffer, size_t map_capacity);
#endif
