/* Original 20F66 methods remain authoritative. Calls are measured, never
 * retried, completed artificially, or replied to by this observer. */
#include <errno.h>
#include "progress-store.h"
enum { LP_BUNDLE, LP_CONTAINER, LP_SCAN, LP_GATHER, LP_WRITE, LP_METADATA,
       LP_PROTECT, LP_REPLACE, LP_SB_RECORD, LP_ENUMERATE };
static const char *lp_names[LP_OPS]={"bundle","container","scan","gather",
    "writeData","metadata","protection","replace","SBRecord","enumerate"};
static mach_timebase_info_data_t lp_timebase;
static unsigned lp_hooks,lp_expected;
static uint64_t lp_output_errors,lp_output_truncated,lp_monitor_us;
static void lp_say(const char *format, ...) __attribute__((format(printf,1,2)));
static void lp_say(const char *format, ...) {
    int saved=errno;char line[1024];
    int head=snprintf(line,sizeof(line),"LITHIUM PROGRESS pid=%d ",getpid());
    va_list ap;va_start(ap,format);int n=vsnprintf(line+head,sizeof(line)-(size_t)head,format,ap);va_end(ap);
    if(n<0){__atomic_add_fetch(&lp_output_errors,1,__ATOMIC_RELAXED);errno=saved;return;}
    size_t size=(size_t)head+(size_t)n;
    if(size>=sizeof(line)){size=sizeof(line)-1;line[size-1]='\n';__atomic_add_fetch(&lp_output_truncated,1,__ATOMIC_RELAXED);}
    for(size_t done=0;done<size;){ssize_t written=write(2,line+done,size-done);
        if(written<0 && errno==EINTR)continue;
        if(written<=0){__atomic_add_fetch(&lp_output_errors,1,__ATOMIC_RELAXED);break;}
        done+=(size_t)written;
    }
    errno=saved;
}

static uint64_t lp_us(void) {
    uint64_t ticks=mach_absolute_time();
    return ticks/lp_timebase.denom*lp_timebase.numer/1000+
        ticks%lp_timebase.denom*lp_timebase.numer/lp_timebase.denom/1000;
}
static uint64_t lp_tid(void) {uint64_t n=0;pthread_threadid_np(NULL,&n);return n;}
static bool lp_string(id value, char *out, size_t size) {
    out[0]=0;
    if(!value){lp_copy(out,size,"<nil>");return true;}
    return CFStringGetCString((CFStringRef)value,out,(CFIndex)size,kCFStringEncodingUTF8);
}
static bool lp_url(id value, char *out, size_t size) {
    if(!value){lp_copy(out,size,"<nil>");return true;}
    CFStringRef p=CFURLCopyFileSystemPath((CFURLRef)value,kCFURLPOSIXPathStyle);
    bool ok=p && lp_string((id)p,out,size);if(p)CFRelease(p);
    if(!ok)lp_copy(out,size,"<path-unavailable>");return ok;
}
static struct lp_item lp_start_object(int op,id value,bool url) {
    int saved=errno;uint64_t before=lp_us();char key[LP_KEY];
    bool ok=url?lp_url(value,key,sizeof(key)):lp_string(value,key,sizeof(key));
    if(!ok)lp_copy(key,sizeof(key),"<identity-unavailable>");
    struct lp_item x=lp_begin(op,lp_tid(),lp_us(),key,ok);
    lp_overhead(lp_us()-before);errno=saved;return x;
}
static struct lp_item lp_start_plain(int op) {
    int saved=errno;uint64_t before=lp_us();
    struct lp_item x=lp_begin(op,lp_tid(),before,"",true);
    lp_overhead(lp_us()-before);errno=saved;return x;
}
static void lp_finish(struct lp_item x,int result,bool failed,id *error) {
    int saved=errno;uint64_t now=lp_us();char domain[64]="";int code=0;
    /* NSError output is inspected only on failure; success may leave it unset. */
    if(failed && error && *error) {
        code=(int)CFErrorGetCode((CFErrorRef)*error);
        (void)lp_string((id)CFErrorGetDomain((CFErrorRef)*error),domain,sizeof(domain));
    }
    lp_end(x,now,result,failed,code,saved,domain);lp_overhead(lp_us()-now);errno=saved;
}
static void lp_clean(char *s) {
    for(;*s;s++)if((unsigned char)*s<32 || *s=='"' || *s=='\\')*s='_';
}
static void lp_print_item(const char *kind,struct lp_item x,uint64_t now) {
    if(!x.id)return;
    uint64_t end=x.end?x.end:now;
    uint64_t elapsed=end>=x.start?end-x.start:0;
    lp_say("item=%s id=%llu tid=%#llx op=%d hash=%llx seen=%llu us=%llu result=%d err=%d errno_raw=%d complete=%d\n",
        kind,(unsigned long long)x.id,(unsigned long long)x.tid,x.op,
        (unsigned long long)x.hash,(unsigned long long)x.occurrence,
        (unsigned long long)elapsed,x.result,x.error,x.syserr,x.identity_complete);
    char key[LP_KEY];lp_copy(key,sizeof(key),x.text);
    if(x.object>=0) {
        pthread_mutex_lock(&lp_lock);lp_copy(key,sizeof(key),lp_objects[x.object].key);pthread_mutex_unlock(&lp_lock);
    }
    lp_clean(key);lp_clean(x.domain);
    lp_say("detail id=%llu domain=%s start_us=%llu end_us=%llu\n",
        (unsigned long long)x.id,x.domain,(unsigned long long)x.start,(unsigned long long)x.end);
    for(size_t at=0;at<strlen(key);at+=120)
        lp_say("key id=%llu part=%u text=%.120s\n",(unsigned long long)x.id,(unsigned)at/120,key+at);
}
static void *lp_monitor(void *unused) {
    (void)unused;
    struct lp_snapshot *s=calloc(1,sizeof(*s));if(!s){lp_say("monitor allocation failed\n");return NULL;}
    uint64_t began=lp_us();
    for(unsigned round=0;round<361;round++) {
        if(round)sleep(30);
        uint64_t now=lp_us();lp_snapshot(s,now);
        lp_say("snapshot=%llu pid=%d us=%llu hooks=%u/%u\n",
            (unsigned long long)s->serial,getpid(),(unsigned long long)now,lp_hooks,lp_expected);
        lp_say("losses pid=%d active_full=%llu object_full=%llu group_full=%llu identity_bad=%llu overhead_us=%llu\n",getpid(),
            (unsigned long long)s->active_full,(unsigned long long)s->objects_full,
            (unsigned long long)s->groups_full,(unsigned long long)s->identity_bad,(unsigned long long)s->overhead_us);
        for(int i=0;i<LP_OPS;i++)if(s->stats[i].begin) {
            struct lp_stat *a=&s->stats[i];
            lp_say("op=%d name=%s begin=%llu end=%llu fail=%llu distinct=%llu repeat=%llu total_us=%llu max_us=%llu\n",i,lp_names[i],
                (unsigned long long)a->begin,(unsigned long long)a->end,(unsigned long long)a->failed,
                (unsigned long long)a->distinct,(unsigned long long)a->repeats,
                (unsigned long long)a->total_us,(unsigned long long)a->max_us);
            lp_print_item("last",a->last,now);
            if(a->failure.id && a->failure.id!=a->last.id)lp_print_item("failure",a->failure,now);
            if(a->slowest.id!=a->last.id && a->slowest.id!=a->failure.id)lp_print_item("slowest",a->slowest,now);
        }
        for(int i=0;i<LP_ACTIVE;i++)if(s->active[i].id)lp_print_item("active",s->active[i],now);
        for(int i=0;i<LP_GROUPS;i++)if(s->groups[i].count) {
            struct lp_group *g=&s->groups[i];
            lp_say("group=%p call=%#lx kind=%s count=%llu value=%ld tid=%#llx last_us=%llu\n",
                (void*)g->group,(unsigned long)g->call,g->kind,(unsigned long long)g->count,g->value,
                (unsigned long long)g->tid,(unsigned long long)g->ticks);
        }
        lp_say("output errors=%llu truncated=%llu previous_monitor_us=%llu\n",
            (unsigned long long)__atomic_load_n(&lp_output_errors,__ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&lp_output_truncated,__ATOMIC_RELAXED),
            (unsigned long long)lp_monitor_us);
        lp_say("end snapshot=%llu pid=%d\n",(unsigned long long)s->serial,getpid());
        lp_monitor_us+=lp_us()-now;
        if(now-began>=10800000000ULL)break;
    }
    free(s);return NULL;
}
static bool lp_hook(const char *cls,bool class_method,const char *sel,const char *types,uintptr_t address,IMP replacement) {
    lp_expected++;
    Class c=objc_getClass(cls);SEL s=sel_registerName(sel);
    Method m=c?(class_method?class_getClassMethod(c,s):class_getInstanceMethod(c,s)):NULL;
    if(!m || (uintptr_t)method_getImplementation(m)!=address+slide || strcmp(method_getTypeEncoding(m),types)) {
        lp_say("hook refused class=%s selector=%s\n",cls,sel);return false;
    }
    method_setImplementation(m,replacement);lp_hooks++;return true;
}
static bool lp_setup(const void *cache,size_t length) {
    static const unsigned char uuid[16]={0xe5,0x42,0x0d,0xb3,0x30,0xd0,0x33,0xaa,0x8b,0x7e,0x05,0xb5,0x03,0x8b,0xb4,0x6d};
    if(length<104 || memcmp((const char*)cache+88,uuid,16))return false;
    mach_timebase_info(&lp_timebase);return lp_timebase.denom!=0;
}
static void lp_launch(void) {
    if(!lp_timebase.denom)return;
    lp_say("installed=%u/%u interval=30 limit=10800 identity=2048 active=64 groups=128 errno_raw_not_failure_proof\n",lp_hooks,lp_expected);
    lp_say("clock numer=%u denom=%u ticks=mach_absolute_time; command-return-is-not-success\n",lp_timebase.numer,lp_timebase.denom);
    pthread_t t;int e=pthread_create(&t,NULL,lp_monitor,NULL);
    if(!e)pthread_detach(t);else lp_say("monitor create error=%d\n",e);
}
static id (*lp_real_bundle)(id,SEL,id,id*);
static id lp_bundle(id s,SEL c,id url,id *e) {
    struct lp_item x=lp_start_object(LP_BUNDLE,url,true);
    id r=lp_real_bundle(s,c,url,e);lp_finish(x,r!=nil,r==nil,e);return r;
}
static void *(*lp_real_container)(id,SEL,id,id,uint64_t,id,bool,bool,bool*,id*);
static void *lp_container(id s,SEL c,id ident,id parent,uint64_t content,id persona,bool transient,bool create,bool *created,id *e) {
    int saved=errno;uint64_t before=lp_us();char key[LP_KEY],a[384],b[384];
    bool ok=lp_string(ident,a,sizeof(a));ok=lp_string(parent,b,sizeof(b)) && ok;
    /* Persona object pointer is not a stable identity. Counts refer to the
     * displayed tuple only, not uniqueness across different personas. */
    int n=snprintf(key,sizeof(key),"class=%llu create=%d transient=%d identifier=%s parent=%s",
        (unsigned long long)content,create,transient,a,b);
    ok=ok && n>=0 && (size_t)n<sizeof(key);
    struct lp_item x=lp_begin(LP_CONTAINER,lp_tid(),lp_us(),key,ok);
    lp_overhead(lp_us()-before);errno=saved;
    void *r=lp_real_container(s,c,ident,parent,content,persona,transient,create,created,e);
    lp_finish(x,r!=NULL,r==NULL,e);return r;
}
static bool (*lp_real_scan)(id,SEL,id,id*);
static bool lp_scan(id s,SEL c,id url,id *e) {
    struct lp_item x=lp_start_object(LP_SCAN,url,true);bool r=lp_real_scan(s,c,url,e);lp_finish(x,r,!r,e);return r;
}
static bool (*lp_real_gather)(id,SEL,id*);
static bool lp_gather(id s,SEL c,id *e) {
    struct lp_item x=lp_start_plain(LP_GATHER);bool r=lp_real_gather(s,c,e);lp_finish(x,r,!r,e);return r;
}
static bool (*lp_real_enumerate)(id,SEL,id,id*);
static bool lp_enumerate(id s,SEL c,id block,id *e) {
    struct lp_item x=lp_start_plain(LP_ENUMERATE);bool r=lp_real_enumerate(s,c,block,e);lp_finish(x,r,!r,e);return r;
}
static bool (*lp_real_write)(id,SEL,id,id,uint64_t,unsigned short,id*);
static bool lp_write(id s,SEL c,id data,id url,uint64_t options,unsigned short mode,id *e) {
    struct lp_item x=lp_start_object(LP_WRITE,url,true);bool r=lp_real_write(s,c,data,url,options,mode,e);lp_finish(x,r,!r,e);return r;
}
static bool (*lp_real_metadata)(id,SEL,id*);
static bool lp_metadata(id s,SEL c,id *e) {
    struct lp_item x=lp_start_plain(LP_METADATA);bool r=lp_real_metadata(s,c,e);lp_finish(x,r,!r,e);return r;
}
static void (*lp_real_protect)(id,SEL),(*lp_real_replace)(id,SEL);
static void lp_protect(id s,SEL c) {
    struct lp_item x=lp_start_plain(LP_PROTECT);lp_real_protect(s,c);lp_finish(x,0,false,NULL);
}
static void lp_replace(id s,SEL c) {
    struct lp_item x=lp_start_plain(LP_REPLACE);lp_real_replace(s,c);lp_finish(x,0,false,NULL);
}
static id (*lp_real_record)(id,SEL,id,bool,id*);
static id lp_record(id s,SEL c,id ident,bool allow,id *e) {
    struct lp_item x=lp_start_object(LP_SB_RECORD,ident,false);id r=lp_real_record(s,c,ident,allow,e);lp_finish(x,r!=nil,r==nil,e);return r;
}
#define LP_HOOK(fn,cls,cm,sel,types,addr) do {lp_real_##fn=(__typeof__(lp_real_##fn))(addr+slide);lp_hook(cls,cm,sel,types,addr,(IMP)lp_##fn);}while(0)
static void lp_install(const char *name,const void *cache,size_t length) {
    if(!lp_setup(cache,length)){lp_say("refused cache/timebase mismatch\n");return;}
    if(!strcmp(name,"lsd")) {
        LP_HOOK(bundle,"MIBundle",false,"initWithBundleURL:error:","@32@0:8@16^@24",0x1b1166220UL);
        LP_HOOK(container,"MIContainer",true,"_copyContainerForIdentifier:parentIdentifier:ofContentClass:forPersona:transient:create:created:error:","^{container_object_s=}72@0:8@16@24Q32@40B48B52^B56^@64",0x1b1160444UL);
        LP_HOOK(scan,"MIFilesystemScanner",false,"_scanAppsDirectory:withError:","B32@0:8@16^@24",0x1b116b480UL);
        LP_HOOK(gather,"MILaunchServicesDatabaseGatherer",false,"performGatherWithError:","B24@0:8^@16",0x1b1199944UL);
        LP_HOOK(enumerate,"ICLWorkspace",false,"enumerateBuiltInSystemContentWithBlock:error:","B32@0:8@?16^@24",0x1b117c444UL);
    } else if(!strncmp(name,"containermanagerd",17)) {
        LP_HOOK(write,"MCMFileManager",false,"writeData:toURL:options:mode:error:","B52@0:8@16@24Q32S40^@44",0x1c3ec2144UL);
        LP_HOOK(metadata,"MCMMetadata",false,"writeMetadataToDiskWithError:","B24@0:8^@16",0x1c3ed5d04UL);
        LP_HOOK(protect,"MCMCommandSetDataProtection",false,"execute","v16@0:8",0x1c3f1f078UL);
        LP_HOOK(replace,"MCMCommandReplaceContainer",false,"execute","v16@0:8",0x1c3f3d984UL);
    } else if(!strcmp(name,"SpringBoard")) {
        LP_HOOK(record,"LSApplicationRecord",false,"initWithBundleIdentifier:allowPlaceholder:error:","@36@0:8@16B24^@28",0x181bdcaecUL);
    }
    /* Caller starts the monitor after every hook is installed. */
}
#undef LP_HOOK
