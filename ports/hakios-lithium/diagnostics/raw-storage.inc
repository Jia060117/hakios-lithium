/* Included by the complete HakiOS storage bridge, before hakios_ufs_init. */
#include "../firmware/lithiumPkg/Include/LithiumStorage.h"
#include "raw-storage-core.h"
static uint8_t lr_backup[LR_N][LR_B], lr_buf[LR_B], lr_expected[LR_B];
static uint64_t lr_freq;
static uint64_t lr_now(void *unused) {
    (void)unused;
    uint64_t ticks=__builtin_arm_rsr64("CNTVCT_EL0");
    return (ticks/lr_freq)*1000000000ULL+(ticks%lr_freq)*1000000000ULL/lr_freq;
}
static int lr_io(void *unused, unsigned op, uint64_t lba, uint8_t *buf, uint64_t *ns) {
    (void)unused;
    if (!lithium_scratch_range(lba,1) || (op!=0 && op!=2 && op!=3) || ufs_dead) return EIO;
    lck_mtx_lock(&ufs_lock);
    uint64_t before=ufs_page->answer.ticks;
    int error=ufs_request(op,(uint32_t)ufs_write_part,lba,1,op==0?buf:NULL,op==2?buf:NULL);
    uint64_t ticks=ufs_page->answer.ticks-before;
    *ns=(ticks/lr_freq)*1000000000ULL+(ticks%lr_freq)*1000000000ULL/lr_freq;
    lck_mtx_unlock(&ufs_lock);
    return error;
}
static void lr_print(const char *test, const char *scope, const struct lr_stats *s) {
    printf("LITHIUM RAW test=%s scope=%s count=%llu errors=%llu min_us=%llu avg_us=%llu max_us=%llu\n",
        test,scope,s->n,s->errors,s->n?s->min/1000:0,s->n?s->sum/s->n/1000:0,s->max/1000);
}
static uint32_t lr_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void lithium_raw_storage(void) {
    extern void lithium_log_bytes(const char *, unsigned);
    const char *args=PE_boot_args();
    lithium_log_bytes("LITHIUM BOOTARGS ",sizeof("LITHIUM BOOTARGS ")-1);
    lithium_log_bytes(args,(unsigned)strlen(args));
    lithium_log_bytes("\n",1);
    lr_freq=__builtin_arm_rsr64("CNTFRQ_EL0");
    printf("LITHIUM RAW begin revision=12 block_bytes=4096 samples=100 prelaunch=1 hfs_bypass=1 cntfrq=%llu\n",lr_freq);
    printf("LITHIUM RAW scratch first=%llu blocks=%llu image_end=%llu preserve_original=1\n",
        LITHIUM_SCRATCH_FIRST,LITHIUM_SCRATCH_BLOCKS,LITHIUM_IMAGE_BLOCKS);
    struct lr_result result={0};
    int valid=ufs_ready && !ufs_dead && ufs_write_part>=0 && !ufshc_list_in() && !ufs_native_on &&
        lr_freq>0 && lr_freq<=1000000000ULL &&
        ufs_page->part[ufs_write_part].blocks==LITHIUM_STORAGE_BLOCKS &&
        ufs_page->part[ufs_write_part].start_lba==LITHIUM_STORAGE_FIRST &&
        ufs_page->part[ufs_write_part].block_size==LR_B &&
        (ufs_page->write_caps & BLOCK_CAN_FAST);
    if (valid) {
        lck_mtx_lock(&ufs_lock);
        int error=ufs_request(BLOCK_OP_READ,(uint32_t)ufs_write_part,UFS_WRITE_FIRST,1,lr_buf,NULL);
        lck_mtx_unlock(&ufs_lock);
        /* HFSX/HFS+ volume header: declared extent must end inside the installed image. */
        uint8_t *h=lr_buf+1024;
        uint32_t bs=lr_be32(h+40), count=lr_be32(h+44);
        valid=!error && h[0]=='H' && (h[1]=='X'||h[1]=='+') && bs==4096 && count &&
            (uint64_t)count <= LITHIUM_IMAGE_BLOCKS-LITHIUM_DATA_FIRST;
        printf("LITHIUM RAW geometry valid=%d data_block_bytes=%u data_blocks=%u\n",valid,bs,count);
    }
    if (valid) {
        struct lr_ops ops={NULL,lr_now,lr_io};
        lr_run(&ops,&result,LITHIUM_SCRATCH_FIRST,lr_backup,lr_buf,lr_expected);
    }
    static const char *names[LR_TESTS]={"backup_read","write","flush","write_flush","read","restore_write","restore_read"};
    /* No printf inside a timed request or the sample loop. */
    for (unsigned i=0;i<LR_TESTS;i++) {
        lr_print(names[i],"wall",&result.wall[i]);
        lr_print(names[i],"firmware",&result.service[i]);
    }
    printf("LITHIUM RAW restore backed=%u touched=%u restored=%u verified=%u mismatches=%u\n",
        result.backed,result.touched,result.restored,result.verified,result.mismatches);
    printf("LITHIUM RAW end valid=%d complete=%d timeout=%d preflush_error=%d restore_flush_error=%d\n",
        valid,result.complete,result.timed_out,result.preflush_error,result.restore_flush_error);
    printf("LITHIUM RAW awaiting_normal_timer=1 launchd_started=0\n");
    /* Preserve all results and allow automatic USB capture until normal return. */
    for (;;) IOSleep(1000);
}
static void lithium_raw_storage_if_requested(void) {
    uint32_t enabled=0;
    if (PE_parse_boot_argn("ufs_rawbench",&enabled,sizeof(enabled)) && enabled==1) lithium_raw_storage();
}
