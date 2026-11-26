/* Pure arithmetic shared by production diagnostics and sanitizer regression. */
#ifndef LITHIUM_FSDIAG_CORE_H
#define LITHIUM_FSDIAG_CORE_H
#include <stdint.h>
#define LF_SLOTS 64u
#define LF_STAGES 20u
#define LF_DEPTH 16u
#define LF_FRAMES 16u
#define LF_SAMPLE_LIMIT 24u
#define LF_GLOBAL_SAMPLES 512u
struct lf_stats { uint64_t count, errors, sum, min, max; };
static inline void lf_add(struct lf_stats *s, uint64_t ns, int error) {
    if (!s->count || ns<s->min) s->min=ns;
    if (ns>s->max) s->max=ns;
    s->count++; s->sum+=ns; s->errors+=(error!=0);
}
static inline uint64_t lf_delta(uint64_t now,uint64_t before) { return now>=before?now-before:0; }
static inline uint64_t lf_outside(uint64_t wall,uint64_t bridge) { return wall>bridge?wall-bridge:0; }
static inline int lf_due(uint64_t elapsed,uint64_t last,unsigned count,unsigned total) {
    return elapsed>=500000000ULL && elapsed>=last && elapsed-last>=250000000ULL &&
        count<LF_SAMPLE_LIMIT && total<LF_GLOBAL_SAMPLES;
}
#endif
