/* Include after mBlock in LithiumUfsDxe.c. */
#define R39_WARM_OWNER_MU 1
#include "warm-ufs-owner-protocol.h"
#undef R39_WARM_OWNER_MU

STATIC EFI_GUID mWarmOwnerGuid = R39_WARM_OWNER_GUID;
STATIC struct r39_warm_owner_protocol mWarmOwner;

STATIC EFI_STATUS EFIAPI
WarmOwnerSnapshot (CONST struct r39_warm_owner_protocol *This,
                   struct r39_warm_owner_snapshot *Result)
{
  if (This != &mWarmOwner || Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!mActive || mDead || !mWriteEnabled || !mMedia.MediaPresent || mMedia.ReadOnly) {
    return EFI_NOT_READY;
  }
  UINT64 List = (UINT64)(UINTN)utrl;
  __asm__ volatile ("dsb sy" ::: "memory");
  if (!(ufs_rd(REG_HCE) & 1) ||
      (ufs_rd(REG_HCS) & (HCS_DP | HCS_UTRLRDY)) != (HCS_DP | HCS_UTRLRDY) ||
      !(ufs_rd(REG_CAP) & (1u << 24)) || ufs_rd(REG_UTRLDBR) || ufs_rd(0x78) ||
      ufs_rd(REG_UTRLBA) != (UINT32)List || ufs_rd(REG_UTRLBAU) != (UINT32)(List >> 32) ||
      !(ufs_rd(REG_UTRLRSR) & 1) || ufs_rd(REG_UTRLDBR) || ufs_rd(0x78)) {
    return EFI_NOT_READY;
  }
  *Result = (struct r39_warm_owner_snapshot){UFS_BASE, List, &mBlock};
  return EFI_SUCCESS;
}

STATIC struct r39_warm_owner_protocol mWarmOwner = {
  R39_WARM_OWNER_REVISION, sizeof(mWarmOwner), WarmOwnerSnapshot
};
