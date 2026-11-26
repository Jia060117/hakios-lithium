
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define __unused __attribute__((unused))
#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_FAILURE 5
#define Z_WAITOK 1
typedef uint64_t user_addr_t;
typedef uint64_t mach_vm_address_t;
typedef uint64_t mach_vm_size_t;
typedef uint64_t mach_vm_offset_t;
typedef int vm_prot_t;
struct proc {int unused;};
struct shared_file_mapping_np {
	mach_vm_address_t       sfm_address;
	mach_vm_size_t          sfm_size;
	mach_vm_offset_t        sfm_file_offset;
	vm_prot_t               sfm_max_prot;
	vm_prot_t               sfm_init_prot;
};
struct shared_file_mapping_slide_np {
	/* address at which to create mapping */
	mach_vm_address_t       sms_address ;
	/* size of region to map */
	mach_vm_size_t          sms_size;
	/* offset into file to be mapped */
	mach_vm_offset_t        sms_file_offset ;
	/* size of data at sms_slide_start */
	user_addr_t             sms_slide_size;
	/* address from which to get relocation data */
	user_addr_t             sms_slide_start;
	/* protections, plus flags, see below */
	vm_prot_t               sms_max_prot;
	vm_prot_t               sms_init_prot;
};
struct shared_file_np {
	int                     sf_fd;             /* file to be mapped into shared region */
	uint32_t                sf_mappings_count; /* number of mappings */
	uint32_t                sf_slide;          /* distance in bytes of the slide */
};
struct shared_region_map_and_slide_np_args {
    int fd;uint32_t pad_fd;
    uint32_t count,pad_count;
    user_addr_t mappings;
    uint32_t slide,pad_slide;
    user_addr_t slide_start;
    uint32_t slide_size,pad_slide_size;
};
static struct shared_file_mapping_np input[1024];
static struct shared_file_mapping_slide_np captured[1024];
static struct shared_file_np captured_file;
static uint32_t previous_slide;
static int alloc_fail,copy_fail_at,core_error,copy_calls,core_calls,live_allocs;
static void *allocation;
static size_t allocation_size;
static int vm_shared_region_sliding_valid(uint32_t slide) {
    if(previous_slide && slide) return slide==previous_slide?KERN_INVALID_ARGUMENT:KERN_FAILURE;
    return KERN_SUCCESS;
}
static void *kalloc_data(size_t bytes,int flags) {
    assert(flags==Z_WAITOK && live_allocs==0);
    if(alloc_fail)return NULL;
    allocation=malloc(bytes);assert(allocation);allocation_size=bytes;live_allocs++;
    return allocation;
}
static void kfree_data(void *p,size_t bytes) {
    assert(p==allocation && bytes==allocation_size && live_allocs==1);
    free(p);allocation=NULL;allocation_size=0;live_allocs--;
}
static int shared_region_copyin(struct proc *p,user_addr_t from,unsigned n,unsigned bytes,void *dest) {
    (void)p;assert(n==1 && bytes==sizeof(input[0]));
    int i=copy_calls++;
    if(i==copy_fail_at)return EFAULT;
    assert(i<1024 && from==(user_addr_t)(uintptr_t)&input[i]);
    memcpy(dest,&input[i],bytes);return 0;
}
static int _shared_region_map_and_slide(struct proc *p,uint32_t files,struct shared_file_np *file,
    uint32_t count,struct shared_file_mapping_slide_np *maps) {
    (void)p;assert(files==1 && count==file->sf_mappings_count && count<=1024);
    captured_file=*file;memcpy(captured,maps,count*sizeof(*maps));core_calls++;
    return core_error;
}
void loader_log_line(const char *line) { (void)line; }

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


static void reset(void) {
    assert(live_allocs==0);previous_slide=0;alloc_fail=0;copy_fail_at=-1;core_error=0;
    copy_calls=core_calls=0;
}
static struct shared_region_map_and_slide_np_args args(void) {
    return (struct shared_region_map_and_slide_np_args) {
        .fd=7,.count=3,.mappings=(user_addr_t)(uintptr_t)input,.slide=0x4000,
        .slide_start=0x1a0000000ULL,.slide_size=0x8000
    };
}
int main(void) {
    _Static_assert(sizeof(struct shared_file_mapping_np)==32,"legacy ABI");
    _Static_assert(sizeof(struct shared_file_mapping_slide_np)==48,"8796 mapping ABI");
    _Static_assert(offsetof(struct shared_region_map_and_slide_np_args,slide_start)==32,"ARM64 syscall slots");
    for(unsigned i=0;i<1024;i++) input[i]=(struct shared_file_mapping_np) {
        .sfm_address=0x180004000ULL+i*0x10000,.sfm_size=0x4000,
        .sfm_file_offset=i*0x4000,.sfm_max_prot=i==1?0x23:5,.sfm_init_prot=i==1?3:5
    };
    reset();struct shared_region_map_and_slide_np_args a=args();
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==0 && core_calls==1 && copy_calls==3 && !live_allocs);
    assert(captured_file.sf_fd==7 && captured_file.sf_slide==0x4000);
    for(unsigned i=0;i<3;i++) {
        assert(captured[i].sms_address==input[i].sfm_address && captured[i].sms_size==input[i].sfm_size);
        assert(captured[i].sms_file_offset==input[i].sfm_file_offset && captured[i].sms_max_prot==input[i].sfm_max_prot);
        assert(captured[i].sms_init_prot==input[i].sfm_init_prot && captured[i].sms_slide_start==a.slide_start);
        assert(captured[i].sms_slide_size==a.slide_size);
    }
    puts("PASS three-region legacy mapping preserves chosen addresses, slide, protections and slide-info pointer");
    reset();a=args();a.count=1024;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==0 && core_calls==1 && copy_calls==1024 && !live_allocs);
    puts("PASS complete legacy mapping-count bound is accepted without a three-region-only shortcut");
    reset();a=args();a.count=0;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==0 && !copy_calls && !core_calls && !live_allocs);
    a.count=1025;assert(shared_region_map_and_slide_np(NULL,&a,NULL)==EINVAL && !core_calls && !live_allocs);
    a=args();a.mappings=UINT64_MAX-16;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==EFAULT && !copy_calls && !live_allocs);
    puts("PASS zero count, excessive count and address overflow preserve bounded allocation/copy behavior");
    reset();a=args();copy_fail_at=1;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==EFAULT && !core_calls && copy_calls==2 && !live_allocs);
    puts("PASS partial copyin failure releases temporary mappings without calling the shared-region core");
    reset();a=args();alloc_fail=1;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==ENOMEM && !copy_calls && !core_calls && !live_allocs);
    puts("PASS allocation failure returns errno without copy or mapping");
    reset();a=args();previous_slide=a.slide;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==0 && !core_calls && !live_allocs);
    a.slide+=0x4000;assert(shared_region_map_and_slide_np(NULL,&a,NULL)==KERN_FAILURE && !core_calls && !live_allocs);
    puts("PASS upstream installed-slide check prevents repeated or conflicting relocation");
    reset();a=args();core_error=EPERM;
    assert(shared_region_map_and_slide_np(NULL,&a,NULL)==EPERM && core_calls==1 && !live_allocs);
    puts("PASS root/signature or mapping rejection from the existing core is propagated unchanged");
    return 0;
}
