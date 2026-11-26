/* Run A normal-return diagnostics. Called from an ordinary thread, before
 * storage save/quiesce and USB drain. No filesystem policy or clock changes.
 * The inventory is a bounded live snapshot, not proof of database readiness.
 */
#define LA_DEPTH 8u
#define LA_ENTRIES 200000u
#define LA_FILES 256u
#define LA_SCAN_SECONDS 120u
#define LA_BUFSIZE 4096u
struct la_dir {
    vnode_t vp;
    off_t offset;
    size_t at, used;
    int eof;
    bool related;
    char path[MAXPATHLEN];
    char data[LA_BUFSIZE] __attribute__((aligned(8)));
};
struct la_scan {
    mount_t mp;
    vfs_context_t ctx;
    uint64_t started;
    unsigned entries, files, dirs, errors, missing, truncated, skips;
    struct la_dir dirs_stack[LA_DEPTH];
    char child[MAXPATHLEN];
};
static uint64_t la_us(void) {
    uint64_t ns; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns/1000;
}
static bool la_related(const char *name) {
    return strnstr(name,"LaunchServices",MAXPATHLEN) || strnstr(name,"launchservices",MAXPATHLEN) ||
        strnstr(name,".csstore",MAXPATHLEN) || strnstr(name,"com.apple.lsd",MAXPATHLEN) || !strcmp(name,"lsd");
}
static bool la_entry(const struct dirent *e, size_t remaining) {
    const size_t head=offsetof(struct dirent,d_name);
    if(remaining<head)return false;
    const size_t length=e->d_namlen;
    return remaining>=head && e->d_reclen>=head+1 && e->d_reclen<=remaining &&
        length>0 && length<sizeof(e->d_name) && head+length<e->d_reclen &&
        e->d_name[length]==0 && strnlen(e->d_name,length)==length &&
        memchr(e->d_name,'/',length)==NULL;
}
static void la_error(struct la_scan *s,const char *op,int error) {
    if(s->errors++<32) printf("LITHIUM LSDB error op=%s error=%d\n",op,error);
}
static void la_path(const char *kind,unsigned id,const char *path) {
    /* Chunks stay below the existing console's per-line limit. Typical paths
     * are ASCII. Control characters are emitted as hex, never log newlines. */
    for(size_t at=0;path[at];) {
        char line[121]; size_t n=0,begin=at;
        while(path[at] && n<116) {
            unsigned char c=(unsigned char)path[at++];
            if(c<32 || c>=127 || c=='\\') {
                static const char hex[]="0123456789abcdef";
                line[n++]='\\';line[n++]='x';line[n++]=hex[c>>4];line[n++]=hex[c&15];
            } else line[n++]=(char)c;
        }
        line[n]=0;
        printf("LITHIUM LSDB path kind=%s id=%u offset=%u encoding=hex_escape text=%s\n",kind,id,(unsigned)begin,line);
    }
}
static void la_file(struct la_scan *s,vnode_t vp,const char *path) {
    if(s->files>=LA_FILES){s->truncated++;return;}
    struct vnode_attr va; VATTR_INIT(&va);
    VATTR_WANTED(&va,va_data_size); VATTR_WANTED(&va,va_modify_time);
    int error=vnode_getattr(vp,&va,s->ctx);
    unsigned id=++s->files;
    bool size_ok=!error && VATTR_IS_SUPPORTED(&va,va_data_size);
    bool time_ok=!error && VATTR_IS_SUPPORTED(&va,va_modify_time);
    if(error)la_error(s,"getattr",error);
    printf("LITHIUM LSDB file id=%u error=%d size_valid=%u size=%llu mtime_valid=%u mtime_sec=%lld mtime_nsec=%ld\n",
        id,error,size_ok,(unsigned long long)(size_ok?va.va_data_size:0),
        time_ok,(long long)(time_ok?va.va_modify_time.tv_sec:0),(long)(time_ok?va.va_modify_time.tv_nsec:0));
    la_path("file",id,path);
}
static void la_root(struct la_scan *s,const char *path,unsigned root_id) {
    vnode_t root=NULL;
    int error=vnode_lookup(path,VNODE_LOOKUP_NOFOLLOW,&root,s->ctx);
    la_path("root",root_id,path);
    if(error) {
        if(error==ENOENT)s->missing++;else la_error(s,"root_lookup",error);
        printf("LITHIUM LSDB root id=%u error=%d\n",root_id,error);return;
    }
    if(vnode_mount(root)!=s->mp || vnode_vtype(root)!=VDIR) {
        s->skips++;printf("LITHIUM LSDB root id=%u wrong_mount_or_type=1\n",root_id);
        vnode_put(root);return;
    }
    uio_t uio=uio_create(1,0,UIO_SYSSPACE,UIO_READ);
    if(!uio){la_error(s,"uio_create",ENOMEM);vnode_put(root);return;}
    unsigned depth=1;
    bzero(&s->dirs_stack[0],sizeof(struct la_dir));
    s->dirs_stack[0].vp=root;strlcpy(s->dirs_stack[0].path,path,MAXPATHLEN);s->dirs++;
    while(depth) {
        if(s->entries>=LA_ENTRIES || s->files>=LA_FILES || la_us()-s->started>=LA_SCAN_SECONDS*1000000ULL) {
            s->truncated++;break;
        }
        struct la_dir *d=&s->dirs_stack[depth-1];
        if(d->at==d->used) {
            if(d->eof){vnode_put(d->vp);depth--;continue;}
            uio_reset(uio,d->offset,UIO_SYSSPACE,UIO_READ);
            error=uio_addiov(uio,(uintptr_t)d->data,LA_BUFSIZE);
            int count=0;
            if(!error)error=VNOP_READDIR(d->vp,uio,0,&d->eof,&count,s->ctx);
            size_t got=LA_BUFSIZE-(size_t)uio_resid(uio);
            off_t next=uio_offset(uio);
            if(error || got>LA_BUFSIZE || (!d->eof && (!got || next==d->offset))) {
                la_error(s,"readdir",error?error:EIO);vnode_put(d->vp);depth--;continue;
            }
            d->offset=next;d->used=got;d->at=0;
            if(!got){vnode_put(d->vp);depth--;continue;}
        }
        struct dirent *e=(struct dirent *)(void *)(d->data+d->at);
        if(!la_entry(e,d->used-d->at) || (e->d_reclen&3)) {
            la_error(s,"dirent",EIO);vnode_put(d->vp);depth--;continue;
        }
        d->at+=e->d_reclen;s->entries++;
        if(!strcmp(e->d_name,".") || !strcmp(e->d_name,".."))continue;
        int n=snprintf(s->child,sizeof(s->child),"%s/%s",d->path,e->d_name);
        if(n<0 || (size_t)n>=sizeof(s->child)){s->truncated++;continue;}
        vnode_t vp=NULL;
        error=vnode_lookupat(e->d_name,VNODE_LOOKUP_NOFOLLOW|VNODE_LOOKUP_NOCROSSMOUNT,&vp,s->ctx,d->vp);
        if(error){la_error(s,"child_lookup",error);continue;}
        if(vnode_mount(vp)!=s->mp){s->skips++;vnode_put(vp);continue;}
        bool related=d->related || la_related(e->d_name);
        if(vnode_vtype(vp)==VREG && related)la_file(s,vp,s->child);
        if(vnode_vtype(vp)==VDIR) {
            if(depth<LA_DEPTH) {
                struct la_dir *child=&s->dirs_stack[depth++];
                bzero(child,sizeof(*child));child->vp=vp;child->related=related;
                strlcpy(child->path,s->child,MAXPATHLEN);s->dirs++;continue;
            }
            s->truncated++;
        } else if(vnode_vtype(vp)==VLNK)s->skips++;
        vnode_put(vp);
    }
    while(depth)vnode_put(s->dirs_stack[--depth].vp);
    uio_free(uio);
}

void lithium_run_a_cpu(void);
void lithium_run_a_cpu(void) {
    uint64_t mpidr=__builtin_arm_rsr64("MPIDR_EL1");
    unsigned cpus=0,bootcpu=0;PE_parse_boot_argn("cpus",&cpus,sizeof(cpus));
    PE_parse_boot_argn("bootcpu",&bootcpu,sizeof(bootcpu));
    bool matches=cpus==1 && bootcpu==1 && cpu_number()==0 &&
        ml_get_max_cpu_number()==0 && (mpidr&0xff00ffffffULL)==1;
    printf("LITHIUM RUN A logical=%d mpidr=0x%llx affinity=0x%llx max_logical=%d bootcpu=%u cpus=%u expected_affinity=0x1 match=%u\n",
        cpu_number(),mpidr,mpidr&0xff00ffffffULL,ml_get_max_cpu_number(),bootcpu,cpus,matches);
}

void lithium_lsdb_finalize(void);
void lithium_lsdb_finalize(void) {
    uint64_t began=la_us();vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULL;
    printf("LITHIUM LSDB finalize begin scope=live_snapshot writers_quiesced=0 desktop=UNVERIFIED\n");
    int error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if(error){printf("LITHIUM DATA SYNC end error=%d phase=lookup\n",error);return;}
    mount_t mp=vnode_mount(vp);
    if(!mp || strcmp(vfs_statfs(mp)->f_fstypename,"hfs") ||
        strcmp(vfs_statfs(mp)->f_mntfromname,"/dev/ufs_darwin_data") || (mp->mnt_flag&MNT_RDONLY)) {
        printf("LITHIUM DATA SYNC end error=%d phase=mount_validation\n",EINVAL);vnode_put(vp);return;
    }
    error=vfs_busy(mp,LK_NOWAIT);
    if(error){printf("LITHIUM DATA SYNC end error=%d phase=mount_busy\n",error);vnode_put(vp);return;}
    struct la_scan *s=kalloc_data(sizeof(*s),Z_WAITOK|Z_ZERO);
    if(s) {
        s->mp=mp;s->ctx=ctx;s->started=la_us();
        const char *roots[]={"/private/var/mobile/Library","/private/var/db","/private/var/root/Library",
            "/private/var/containers/Shared/SystemGroup","/private/var/containers/Data/System"};
        for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++)la_root(s,roots[i],i+1);
        printf("LITHIUM LSDB inventory end entries=%u dirs=%u files=%u errors=%u missing_roots=%u truncated=%u skipped=%u elapsed_us=%llu\n",
            s->entries,s->dirs,s->files,s->errors,s->missing,s->truncated,s->skips,la_us()-s->started);
        kfree_data(s,sizeof(*s));
    } else printf("LITHIUM LSDB inventory end error=%d\n",ENOMEM);
    uint64_t sync_began=la_us();
    printf("LITHIUM DATA SYNC begin wait=MNT_WAIT mount=/private/var device=/dev/ufs_darwin_data\n");
    error=VFS_SYNC(mp,MNT_WAIT,ctx);
    printf("LITHIUM DATA SYNC filesystem_end error=%d elapsed_us=%llu\n",error,la_us()-sync_began);
    int flush_error=ENXIO;vnode_t dev=mp->mnt_devvp;
    if(dev) {
        flush_error=vnode_get(dev);
        if(!flush_error){flush_error=VNOP_IOCTL(dev,DKIOCSYNCHRONIZECACHE,NULL,FWRITE,ctx);vnode_put(dev);}
    }
    printf("LITHIUM DATA SYNC end error=%d flush_error=%d elapsed_us=%llu persistence=UNVERIFIED writers_quiesced=0\n",
        error,flush_error,la_us()-began);
    vfs_unbusy(mp);vnode_put(vp);
}
