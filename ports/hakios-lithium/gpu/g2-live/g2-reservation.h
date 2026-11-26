/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef LITHIUM_G2_RESERVATION_H
#define LITHIUM_G2_RESERVATION_H
#include <stdint.h>
#include <stddef.h>
#define LG2L_CODE_PA UINT64_C(0x92300000)
#define LG2L_CODE_BYTES UINT64_C(8192)
#define LG2L_META_BYTES UINT64_C(16384)
#define LG2L_KERNEL_PA UINT64_C(0x100000000)
#define LG2L_KERNEL_BYTES UINT64_C(0xb4000000)
#define LG2L_RECEIPT_MAGIC UINT64_C(0x315253415032474c)
#define LG2L_RECEIPT_FLAGS UINT64_C(15)
#define LG2L_RECEIPT_PROPERTY "lithium,g2-reservation-v1"
#define LG2L_INFO19_PROFILE UINT64_C(0xbc0e4d9e)

/* Native-endian local boot handoff, sixteen u64 words, no pointers.
 * flags: complete map, exact code descriptor, exact kernel descriptor,
 * actual Mu Normal-NC translations. It is a software receipt, not a signature.
 */
struct lg2l_receipt {
    uint64_t magic, version, bytes, boot_cookie;
    uint64_t map_key, map_bytes, descriptor_bytes, descriptor_version;
    uint64_t code_pa, code_bytes, kernel_pa, kernel_bytes;
    uint64_t map_fingerprint, par[2], flags;
};
_Static_assert(sizeof(struct lg2l_receipt)==128,"receipt layout");
int lg2l_par_normal_nc(uint64_t par, uint64_t expected_pa);
int lg2l_receipt_valid(const struct lg2l_receipt *);
int lg2l_receipt_build(struct lg2l_receipt *, const void *map, size_t map_bytes,
                       uint64_t map_key, size_t stride, uint32_t version,
                       uint64_t boot_cookie, const uint64_t par[2]);

#endif
