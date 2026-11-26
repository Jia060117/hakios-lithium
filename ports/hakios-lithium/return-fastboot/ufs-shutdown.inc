/* SPDX-License-Identifier: MIT
 * Included at the end of kern_shutdown.c so the normal Darwin process shutdown
 * machinery can be reused without invoking host_reboot or force-unmounting.
 */
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>
#include "ufs-return-profile.h"

int lithium_hfs_return_unmount(dev_t expected_dev, uint64_t cookie);
int hakios_ufs_seal_for_return(dev_t data_dev, uint32_t *flush_request);
int lithium_ufs_shutdown_for_return(uint64_t cookie, uint32_t *flush_request,
    uint32_t *device_number);

int
lithium_ufs_shutdown_for_return(uint64_t cookie, uint32_t *flush_request,
    uint32_t *device_number)
{
    const char *args=PE_boot_args();
    vnode_t device=NULLVP;
    int error=EINVAL;
    if (!cookie || !flush_request || !device_number || !args ||
        !rfbt_ufs_return_profile(args,strnlen(args,BOOT_LINE_LENGTH),1))
        return EINVAL;
    *flush_request=0;
    *device_number=UINT32_MAX;
    vfs_context_t context=vfs_context_kernel();
    error=vnode_lookup("/dev/ufs_darwin_data",VNODE_LOOKUP_NOFOLLOW,&device,context);
    if (error) return error;
    const dev_t dev=vnode_specrdev(device);
    const int valid=vnode_vtype(device)==VBLK && dev!=NODEV;
    vnode_put(device);
    if (!valid) return ENXIO;
    if (!OSCompareAndSwap(0,1,&system_inshutdown)) return EBUSY;
    printf("LITHIUM UFS RETURN phase=process_stop device=0x%x cookie=%llu\n",
        (unsigned)dev,(unsigned long long)cookie);

    lck_mtx_lock(&vm_swap_data_lock);
    compressor_store_stop_compaction=TRUE;
    unsigned polls=0;
    while (vm_swapfile_create_thread_running || vm_swapfile_gc_thread_running) {
        lck_mtx_unlock(&vm_swap_data_lock);
        if (++polls>500) return ETIMEDOUT;
        delay_for_interval(10,NSEC_PER_MSEC);
        lck_mtx_lock(&vm_swap_data_lock);
    }
    lck_mtx_unlock(&vm_swap_data_lock);

    IOSystemShutdownNotification(0,kIOSystemShutdownNotificationStageProcessExit);
    proc_shutdown(TRUE);
#if CONFIG_AUDIT
    audit_shutdown();
#endif
    proc_shutdown(FALSE);
    error=sync((proc_t)NULL,NULL,NULL);
    if (!error && cl_sparse_push_error) error=EIO;
    if (!error) error=lithium_hfs_return_unmount(dev,cookie);
    printf("LITHIUM UFS RETURN phase=unmount error=%d device=0x%x\n",error,(unsigned)dev);
    if (error) return error;
    error=hakios_ufs_seal_for_return(dev,flush_request);
    printf("LITHIUM UFS RETURN phase=seal error=%d request=%u\n",error,*flush_request);
    if (!error && !*flush_request) return EIO;
    if (!error) *device_number=(uint32_t)dev;
    return error;
}
