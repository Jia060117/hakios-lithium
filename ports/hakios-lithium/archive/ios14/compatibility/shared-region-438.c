
/*
 * lithium / 18H17: dyld uses syscall 438 for the three-region arm64 cache.
 * Adapt its retained public mapping format to this kernel's shared-region core.
 * Addresses and slide are already selected by dyld: do not randomize them again.
 * Vnode/root/signature checks, mapping, relocation and cleanup remain upstream.
 */
int
shared_region_map_and_slide_np(
    struct proc *p,
    struct shared_region_map_and_slide_np_args *uap,
    __unused int *retvalp)
{
#if !defined(LITHIUM_ARMV8_0)
    return ENOSYS;
#else
    struct shared_file_mapping_np legacy;
    struct shared_file_mapping_slide_np *maps;
    struct shared_file_np file = {
        .sf_fd = uap->fd, .sf_mappings_count = uap->count, .sf_slide = uap->slide
    };
    uint32_t count = uap->count;
    int error = vm_shared_region_sliding_valid(uap->slide);

    /* The upstream helper returns this when the identical slide is installed. */
    if (error != KERN_SUCCESS) {
        return error == KERN_INVALID_ARGUMENT ? 0 : error;
    }
    if (count == 0) {
        return 0;
    }
    /* The old public ABI permits at most 1024 mapping records. */
    if (count > 1024) {
        return EINVAL;
    }
    if (uap->mappings > UINT64_MAX - (uint64_t)count * sizeof(legacy)) {
        return EFAULT;
    }
    maps = kalloc_data((size_t)count * sizeof(*maps), Z_WAITOK);
    if (maps == NULL) {
        return ENOMEM;
    }
    for (uint32_t i = 0; i < count; i++) {
        error = shared_region_copyin(p, uap->mappings + (uint64_t)i * sizeof(legacy),
            1, sizeof(legacy), &legacy);
        if (error != 0) {
            goto out;
        }
        maps[i] = (struct shared_file_mapping_slide_np) {
            .sms_address = legacy.sfm_address,
            .sms_size = legacy.sfm_size,
            .sms_file_offset = legacy.sfm_file_offset,
            .sms_slide_size = uap->slide_size,
            .sms_slide_start = uap->slide_start,
            .sms_max_prot = legacy.sfm_max_prot,
            .sms_init_prot = legacy.sfm_init_prot
        };
    }
    error = _shared_region_map_and_slide(p, 1, &file, count, maps);
    {
        char line[128];
        extern void loader_log_line(const char *line);
        snprintf(line, sizeof(line), "LITHIUM P2 SR438 count=%u slide=%u rc=%d\n",
            count, uap->slide, error);
        loader_log_line(line);
    }
out:
    kfree_data(maps, (size_t)count * sizeof(*maps));
    return error;
#endif
}

