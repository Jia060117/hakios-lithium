/* Bounded, default-off R37 observations. Scheduler hot path never prints. */
#ifndef LITHIUM_CPU_SCHED_R37_H
#define LITHIUM_CPU_SCHED_R37_H
#define LR37_CPUS 4u
#define LR37_SLOTS 128u
struct lr37_row {
    uint64_t tid, task_id;
    uint64_t ticks[LR37_CPUS], switches[LR37_CPUS], expiries[LR37_CPUS];
    uint64_t last_tick, backwards;
};
void lithium_sched_r37_enable(void);
void lithium_sched_r37_select(unsigned group, uint64_t task_id);
unsigned lithium_sched_r37_snapshot(struct lr37_row *rows, unsigned capacity,
    uint64_t *dropped);
void lithium_sched_r37_init(void);
void lithium_slowcores_r37_report(void);
#endif
