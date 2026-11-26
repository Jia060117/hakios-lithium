/* SPDX-License-Identifier: MIT
 * Included at the end of the complete HakiOS UFS bridge. No MMIO or filesystem
 * operation: independently reject active/shared/writable storage. Absence of
 * these fields alone is NOT enough; both peers also require the explicit RAM
 * profile and loader's separately built physical-provider-free Mu capability.
 */
bool
lithium_return_fastboot_ram_storage_clean(void)
{
    return ufs_page == NULL && ufs_buffer == NULL && ufs_parts == 0 &&
        !ufs_ready && ufs_write_part == -1 && !ufs_data_used &&
        !ufs_write_busy && !ufs_write_out && !ufs_write_was_native &&
        ufs_var_save == VAR_SAVE_NONE && ufs_var_busy_pid == 0 &&
        ufs_requests == 0 && ufs_writes == 0 && ufs_write_errors == 0 &&
        ufs_write_refused == 0;
}
