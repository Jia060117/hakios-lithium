/* Dyld ABI and MAC registration suites do not call the signature hook.
 * The real implementation has its own firmware-trust sanitizer suite. */
typedef int cpu_type_t;
struct vnode; struct label; struct cs_blob;
typedef int (*signature_hook_t)(struct vnode *,struct label *,cpu_type_t,
    struct cs_blob *,unsigned int *,unsigned int *,int,unsigned int,char **,size_t *);
int lithium_firmware_check_signature(struct vnode *v,struct label *l,cpu_type_t c,
    struct cs_blob *b,unsigned int *f,unsigned int *s,int k,unsigned int p,char **d,size_t *n) {
    (void)v;(void)l;(void)c;(void)b;(void)f;(void)s;(void)k;(void)p;(void)d;(void)n;
    abort(); /* This independent dependency must not be exercised by the query suite. */
}
