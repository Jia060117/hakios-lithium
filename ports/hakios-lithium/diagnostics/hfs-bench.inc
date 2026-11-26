/* Same fixed workload in either cpus=1 or cpus=3. After HFS mount, before
 * original launchd: no LS database/cache deletion, journal or mount-policy edits.
 * Only a newly created temporary directory is written and then removed. */
enum { LH_EIO=5,LH_ENOENT=2,LH_EEXIST=17,LH_ETIMEDOUT=60 };
struct lh_stats { unsigned n,errors; uint64_t min,max,sum; };
static uint64_t lh_now(void) {
    struct timeval tv;
    if(gettimeofday(&tv,NULL))return 0;
    return (uint64_t)tv.tv_sec*1000000000ULL+(uint64_t)tv.tv_usec*1000;
}
static void lh_add(struct lh_stats *s,uint64_t ns,int error) {
    if(!s->n||ns<s->min)s->min=ns;if(ns>s->max)s->max=ns;
    s->n++;s->sum+=ns;s->errors+=(error!=0);
}
static void lh_print(const char *name,const struct lh_stats *s) {
    printf("LITHIUM HFS BENCH test=%s count=%u errors=%u min_us=%llu avg_us=%llu max_us=%llu\n",
        name,s->n,s->errors,(unsigned long long)(s->min/1000),
        (unsigned long long)(s->n?s->sum/s->n/1000:0),(unsigned long long)(s->max/1000));
}
static void lithium_hfs_bench(void) {
    const char *requested=boot_arg("hfs_bench");
    if(!requested||strcmp(requested,"1"))return;
    const char *device=boot_arg("var_hfs");
    if(!var_kept||!device||strcmp(device,"ufs_darwin_data")) {
        printf("LITHIUM HFS BENCH refused reason=not_ufs_hfs_data\n");return;
    }
    char directory[]="/private/var/tmp/lithium-r13-0000000000000000",from[256],to[256];
    const size_t offset=sizeof(directory)-17;int made=0;
    uint64_t unique=lh_now()^((uint64_t)getpid()<<32);
    for(unsigned attempt=0;attempt<32;attempt++) {
        uint64_t name=unique+attempt;
        for(unsigned i=0;i<16;i++)directory[offset+i]="0123456789abcdef"[(name>>(4*(15-i)))&15];
        if(!mkdir(directory,0700)) {made=1;break;}
        if(errno!=LH_EEXIST)break;
    }
    if(!made) {printf("LITHIUM HFS BENCH setup_error=%d\n",errno);return;}
    strcpy(from,directory);strcpy(from+strlen(from),"/from");
    strcpy(to,directory);strcpy(to+strlen(to),"/to");
    char cpuarg[16],bootarg[16];const char *value=boot_arg("cpus");
    strcpy(cpuarg,value?value:"unknown");value=boot_arg("bootcpu");strcpy(bootarg,value?value:"unknown");
    unsigned char data[4096],readback[4096];for(unsigned i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*17+31);
    struct lh_stats writes={0},flushes={0},replaces={0},reads={0};
    unsigned completed=0,mismatches=0;int error=0;uint64_t start=lh_now();
    printf("LITHIUM HFS BENCH begin revision=13 prelaunch=1 pid=%d rounds=20 bytes=4096 directory=%s mount=ufs_darwin_data cpus=%s bootcpu=%s clock=gettimeofday resolution_us=1\n",
        getpid(),directory,cpuarg,bootarg);
    for(unsigned round=0;round<20;round++) {
        int fd=open(from,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) {error=errno;break;}
        uint64_t began=lh_now();ssize_t n=write(fd,data,sizeof(data));
        int e=n==(ssize_t)sizeof(data)?0:(n<0?errno:LH_EIO);lh_add(&writes,lh_now()-began,e);
        if(!e) {began=lh_now();int rc=fsync(fd);e=rc?errno:0;lh_add(&flushes,lh_now()-began,e);}
        int rc=close(fd);if(!e&&rc)e=errno;
        if(e) {error=e;break;}
        /* First rename creates the destination, later rounds replace it. */
        began=lh_now();rc=rename(from,to);e=rc?errno:0;lh_add(&replaces,lh_now()-began,e);
        if(e) {error=e;break;}
        fd=open(to,O_RDONLY,0);if(fd<0) {error=errno;break;}
        began=lh_now();n=read(fd,readback,sizeof(readback));e=n==(ssize_t)sizeof(readback)?0:(n<0?errno:LH_EIO);
        lh_add(&reads,lh_now()-began,e);rc=close(fd);if(!e&&rc)e=errno;
        if(e) {error=e;break;}
        if(memcmp(data,readback,sizeof(data))) {mismatches++;error=LH_EIO;break;}
        completed++;
        if(lh_now()-start>120000000000ULL) {error=LH_ETIMEDOUT;break;}
    }
    int cleanup=0;
    if(unlink(from)&&errno!=LH_ENOENT)cleanup=errno;
    if(unlink(to)&&errno!=LH_ENOENT)cleanup=errno;
    if(rmdir(directory))cleanup=errno;
    lh_print("write",&writes);lh_print("fsync",&flushes);lh_print("rename_replace",&replaces);lh_print("read",&reads);
    printf("LITHIUM HFS BENCH end completed=%u expected=20 error=%d mismatches=%u cleanup_error=%d elapsed_us=%llu prelaunch=1 first_rename_creates=1 later_renames_replace=1\n",
        completed,error,mismatches,cleanup,(unsigned long long)((lh_now()-start)/1000));
}
