/* Bounded R11 state. No logging, Objective-C, allocation or service calls under
 * the mutex. Text output is a repeated snapshot, never the source of counts. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#define LP_OPS 16
#define LP_ACTIVE 64
#define LP_OBJECTS 2048
#define LP_GROUPS 128
#define LP_KEY 1024
#define LP_TEXT 192
struct lp_item {
    uint64_t id, tid, start, end, hash, occurrence;
    int op, slot, object, result, error, syserr;
    bool identity_complete;
    char text[LP_TEXT], domain[64];
};
struct lp_stat {
    uint64_t begin, end, failed, total_us, max_us, distinct, repeats;
    struct lp_item last, failure, slowest;
};
struct lp_object { uint64_t hash, seen; int op; char key[LP_KEY]; };
struct lp_group {
    uintptr_t group, call; uint64_t count, tid, ticks; long value;
    char kind[40];
};
struct lp_snapshot {
    uint64_t serial, now, active_full, objects_full, groups_full, identity_bad, overhead_us;
    struct lp_stat stats[LP_OPS];
    struct lp_item active[LP_ACTIVE];
    struct lp_group groups[LP_GROUPS];
};
static pthread_mutex_t lp_lock = PTHREAD_MUTEX_INITIALIZER;
static struct lp_snapshot lp_state;
static struct lp_object lp_objects[LP_OBJECTS];
static uint64_t lp_next;
static uint64_t lp_hash(const char *s) {
    uint64_t h=14695981039346656037ULL;
    for (;*s;s++) { h^=(unsigned char)*s;h*=1099511628211ULL; }
    return h ? h : 1;
}
static void lp_copy(char *to, size_t size, const char *from) {
    if (!from) from="";
    size_t n=strlen(from);if(n>=size)n=size-1;memcpy(to,from,n);to[n]=0;
}
static struct lp_item lp_begin(int op, uint64_t tid, uint64_t now,
    const char *key, bool complete) {
    struct lp_item x={.tid=tid,.start=now,.op=op,.slot=-1,.object=-1,
        .identity_complete=complete};
    if(!key)key="";
    x.hash=lp_hash(key);lp_copy(x.text,sizeof(x.text),key);
    pthread_mutex_lock(&lp_lock);
    x.id=++lp_next;lp_state.stats[op].begin++;
    if(!complete)lp_state.identity_bad++;
    if(complete && *key && strlen(key)<LP_KEY) {
        for(unsigned i=0;i<LP_OBJECTS;i++) {
            unsigned j=(unsigned)(x.hash+i)%LP_OBJECTS;struct lp_object *o=&lp_objects[j];
            if(!o->hash || (o->hash==x.hash && o->op==op && !strcmp(o->key,key))) {
                if(!o->hash) {o->hash=x.hash;o->op=op;lp_copy(o->key,sizeof(o->key),key);lp_state.stats[op].distinct++;}
                else lp_state.stats[op].repeats++;
                x.occurrence=++o->seen;x.object=(int)j;break;
            }
        }
        if(x.object<0)lp_state.objects_full++;
    }
    for(int i=0;i<LP_ACTIVE;i++)if(!lp_state.active[i].id){x.slot=i;lp_state.active[i]=x;break;}
    if(x.slot<0)lp_state.active_full++;
    pthread_mutex_unlock(&lp_lock);return x;
}
static void lp_end(struct lp_item x, uint64_t now, int result, bool failed,
    int error, int syserr, const char *domain) {
    if(!x.id)return;
    x.end=now;x.result=result;x.error=error;x.syserr=syserr;
    lp_copy(x.domain,sizeof(x.domain),domain);
    uint64_t elapsed=now>=x.start?now-x.start:0;
    pthread_mutex_lock(&lp_lock);
    struct lp_stat *s=&lp_state.stats[x.op];s->end++;s->failed+=failed;s->total_us+=elapsed;s->last=x;
    if(failed)s->failure=x;
    if(elapsed>=s->max_us){s->max_us=elapsed;s->slowest=x;}
    if(x.slot>=0 && lp_state.active[x.slot].id==x.id)lp_state.active[x.slot].id=0;
    pthread_mutex_unlock(&lp_lock);
}
static void lp_event(const char *kind, uintptr_t group, uintptr_t call, long value,
    uint64_t tid, uint64_t now) {
    pthread_mutex_lock(&lp_lock);
    /* Phases reserve their own slots; a flood of distinct groups cannot evict
     * database completion/recovery states. Counts are for observed callsites. */
    unsigned first=group?32:0,limit=group?LP_GROUPS:32,i;
    for(i=first;i<limit;i++) {
        struct lp_group *g=&lp_state.groups[i];
        if(!g->count || (g->group==group && g->call==call && !strcmp(g->kind,kind))) {
            lp_copy(g->kind,sizeof(g->kind),kind);g->group=group;g->call=call;
            g->count++;g->value=value;g->tid=tid;g->ticks=now;break;
        }
    }
    if(i==limit)lp_state.groups_full++;
    pthread_mutex_unlock(&lp_lock);
}
static void lp_overhead(uint64_t us) {
    pthread_mutex_lock(&lp_lock);lp_state.overhead_us+=us;pthread_mutex_unlock(&lp_lock);
}
static void lp_snapshot(struct lp_snapshot *out, uint64_t now) {
    pthread_mutex_lock(&lp_lock);lp_state.serial++;lp_state.now=now;*out=lp_state;pthread_mutex_unlock(&lp_lock);
}
