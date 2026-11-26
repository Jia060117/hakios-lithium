/* R32: one initialized register-A3 read. Old adapters and cores are unchanged.
 * The shared ltid one-shot/context/100ms/4096-access lifetime spans BOTH the
 * unchanged R31 initializer and this transfer. A returning deadline cannot
 * contain a hung MMIO/map/barrier, abort, SError or XPU violation.
 * This path never attaches IRQ/HID, changes GIC/GPIO/rails, acknowledges an
 * error/service flag, or resets/retries after transaction failure/success.
 */
#define LTSN_EXPERIMENTAL 1
#define LTSP_EXPERIMENTAL 1
#define LTST_EXPERIMENTAL 1
#include "touch-qup-transfer-core.h"
#undef LTST_EXPERIMENTAL
#undef LTSP_EXPERIMENTAL
#undef LTSN_EXPERIMENTAL

static bool ltstp_active;
/* One-shot destination; lifetime is guarded by the shared ltid latch. */
static struct ltst_result ltstp_retained;
static unsigned ltstp_write_count, ltstp_fifo_reads;
static struct {
    uint32_t offset, value;
    bool returned_on_time;
} ltstp_write_trace[22];

/* Independent numeric ordered policy, not the portable core's union/table.
 * Three little-byte-first words contain nine significant bytes; the last
 * word's padding is not sent because MX_WRITE_COUNT is nine. */
static bool ltstp_write_policy(unsigned index, uint32_t offset, uint32_t value)
{
    static const uint32_t sequence[22][2] = {
        {0x00c,1},{0x004,0},{0x000,0x207},{0x020,0x3c},{0x028,0x300},
        {0x408,1},{0x020,0},{0x028,0x300},{0x200,0},{0x100,0},
        {0x208,3},{0x150,9},{0x000,0x207},{0x008,0xc0a5},{0x408,1},
        {0x004,1},{0x400,0x003e007c},{0x004,3},
        {0x110,0x01827081},{0x110,0x877181a3},{0x110,1},{0x004,1}
    };
    return index < 22 && offset == sequence[index][0] && value == sequence[index][1];
}

static bool ltstp_read(void *cookie, uint32_t offset, uint32_t *value)
{
    (void)cookie;
    if (!ltstp_active || ltid.poisoned) return false;
    if (!ltid.qup) return ltid_stop("transfer_qup_mapping_missing");
    if (offset == UINT32_C(0x218)) {
        if (ltstp_write_count != 22 || ltstp_fifo_reads != 0)
            return ltid_stop("transfer_fifo_read_order_or_cap");
        ++ltstp_fifo_reads; /* attempted read may consume data even if late */
        return ltid_raw_read(ltid.qup + offset, value);
    }
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (offset == ltsn_registers[i].offset)
            return ltid_raw_read(ltid.qup + offset, value);
    return ltid_stop("transfer_adapter_read_policy");
}

static bool ltstp_write(void *cookie, uint32_t offset, uint32_t value)
{
    (void)cookie;
    if (!ltstp_active || ltid.poisoned) return false;
    if (!ltid.qup || !ltstp_write_policy(ltstp_write_count, offset, value))
        return ltid_stop("transfer_adapter_ordered_write_policy");
    if (!ltid_spi_disabled(true) || !ltid_open()) return false;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!ltid_open()) return false;
    const unsigned index = ltstp_write_count++;
    ltstp_write_trace[index].offset = offset;
    ltstp_write_trace[index].value = value;
    ++ltid.writes;
    LITHIUM_TOUCH_ID_WRITE(ltid.qup + offset, value);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    ltstp_write_trace[index].returned_on_time = ltid_open();
    return ltstp_write_trace[index].returned_on_time;
}

static uint64_t ltstp_now(void *cookie)
{
    (void)cookie;
    if (!ltstp_active) return ltid.last;
    return ltid.poisoned ? UINT64_MAX : lts1_now();
}

static void ltstp_delay(void *cookie, uint32_t us)
{
    (void)cookie;
    if (!ltstp_active || ltid.poisoned) return;
    if (us > 10) { ltid_stop("transfer_adapter_delay_policy"); return; }
    if (ltid_open()) LITHIUM_TOUCH_ID_DELAY(us);
}

static void ltstp_log_sample(const char *stage, const struct ltst_sample *s)
{
    static const char *const names[7] = {
        "VERSION","ERROR","STATUS","OPERATIONAL","STATE","IN_COUNT","OUT_COUNT"};
    const uint32_t values[7] = {s->version,s->error,s->status,s->operational,
        s->state,s->in_count,s->out_count};
    printf("LITHIUM TOUCH transfer sample stage=%s attempted=%u complete=%u valid=0x%02x poll=%u\n",
        stage,s->attempted,s->complete,s->valid_mask,s->poll_index);
    for (unsigned i=0;i<7;++i) {
        if (s->valid_mask & (UINT32_C(1)<<i))
            printf("LITHIUM TOUCH transfer field stage=%s index=%u reg=%s valid=1 value=0x%08x\n",
                stage,i,names[i],values[i]);
        else
            printf("LITHIUM TOUCH transfer field stage=%s index=%u reg=%s valid=0 value=unread\n",
                stage,i,names[i]);
    }
    if (s->attempted)
        printf("LITHIUM TOUCH transfer sample_time stage=%s start_absolute_us=%llu\n",
            stage,(unsigned long long)s->started_us);
    if (s->valid_mask)
        printf("LITHIUM TOUCH transfer sample_time stage=%s accepted_absolute_us=%llu\n",
            stage,(unsigned long long)s->completed_us);
}

static __attribute__((noinline)) void lithium_touch_qup_transfer_probe(void)
{
    /* No clock/context/map call is permitted once the shared lifetime has
     * been poisoned, including an already-poisoned first entry. */
    if (ltid.poisoned) return;
    bool not_started = false;
    if (!__atomic_compare_exchange_n(&ltid.started,&not_started,true,false,
            __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return;
    ltid.start = ltid.last = lts1_now();
    if (ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        LITHIUM_PERIPHERAL_AFFINITY()!=1 || ml_get_max_cpu_number()!=0 ||
        !lts1.started || lts1.poisoned || !lts1.clocks_ready || !lts1.pins_ready) {
        ltid_stop("stage1_or_execution_context_not_ready"); goto blocked;
    }
    if (lts1.mappings > sizeof(lts1.windows)/sizeof(lts1.windows[0])) {
        ltid_stop("stage1_mapping_count_invalid"); goto blocked;
    }
    for (unsigned i=0;i<lts1.mappings;++i) {
        if (!lts1.windows[i].mapped) { ltid_stop("stage1_published_mapping_zero"); goto blocked; }
        if (lts1.windows[i].physical == (LTS1_QUP_STATE & ~UINT32_C(0xfff)))
            ltid.qup=lts1.windows[i].mapped;
    }
    if (!ltid.qup) { ltid_stop("stage1_qup_mapping_missing"); goto blocked; }
    if (!ltid_board_ready()) goto blocked;
    printf("LITHIUM TOUCH transfer begin experimental=1 addr=0x38 reg=0xa3 payload_bytes=1 tx_bytes=9 rx_bytes=3 attempts=1 max_sw_reset=1 max_run=2 owner=unverified\n");
    printf("LITHIUM TOUCH transfer limits ack_waveform=unverified stop_waveform=unverified irq_attached=0 hid_events=0 recovery_reset=0 retry=0\n");
    if (!ltid_open()) goto blocked;
    ++ltid.maps;
    ltid.gic=LITHIUM_TOUCH_ID_MAP((vm_offset_t)LTID_GICD_PHYS,(vm_size_t)0x1000);
    if (!ltid.gic) { ltid_stop("gic_mapping_failed"); goto blocked; }
    if (!ltid_open() || !ltid_spi_disabled(true)) goto blocked;
    {
        struct ltsp_io io={NULL,ltstp_read,ltstp_write,ltstp_now,ltstp_delay};
        ltstp_active=true;
        ltst_read_id_into(&io, &ltstp_retained);
        const struct ltst_result *rp=&ltstp_retained;
        ltstp_active=false;
        /* From this point: logs/scalars only, no terminal GIC/time/MMIO calls. */
        printf("LITHIUM TOUCH transfer result status=%s reason=%s phase_ok=%u guard_ok=%u attempt=%u complete=%u quarantined=%u poisoned=%u\n",
            rp->status,rp->reason,rp->phase_accepted,rp->fresh_guard_accepted,rp->attempt_issued,
            rp->transfer_complete,rp->quarantined,rp->poisoned);
        printf("LITHIUM TOUCH transfer progress run_issued=%u run_accepted=%u run_observed=%u input_done=%u stop=%u\n",
            rp->run_issued,rp->run_write_accepted,rp->run_observed,rp->input_done_observed,rp->stop_observed);
        printf("LITHIUM TOUCH transfer counters reads=%u writes=%u polls=%u rx_reads=%u elapsed_us=%llu transaction_elapsed_us=%llu\n",
            rp->reads,rp->writes,
            rp->polls,rp->rx_reads,(unsigned long long)rp->elapsed_us,
            (unsigned long long)rp->transaction_elapsed_us);
        if (rp->rx_word_valid)
            printf("LITHIUM TOUCH transfer rx valid=1 word=0x%08x id_byte=0x%02x id_committed=%u nontrivial=%u identity_match=unverified\n",
                rp->raw_rx_word,rp->raw_id,rp->id_valid,rp->id_nontrivial);
        else
            printf("LITHIUM TOUCH transfer rx valid=0 word=unread id_byte=unread id_committed=0 identity_match=unverified\n");
        printf("LITHIUM TOUCH transfer initializer status=%s reason=%s reads=%u writes=%u polls=%u pause=%u\n",
            rp->phase.status,rp->phase.reason,rp->phase.reads,rp->phase.writes,rp->phase.polls,rp->phase.pause_observed);
        static const char *const poll_names[5] = {"VERSION","ERROR","STATUS","OPERATIONAL","STATE"};
        for (unsigned slot=0;slot<4;++slot) {
            const struct ltsp_poll_sample *p=&rp->phase.poll_samples[slot];
            const uint32_t raw[5]={p->version,p->error,p->status,p->operational,p->state};
            printf("LITHIUM TOUCH transfer init_poll slot=%u attempted=%u complete=%u valid=0x%02x from=%u to=%u index=%u\n",
                slot,p->attempted,p->complete,p->valid_mask,p->from,p->target,p->poll_index);
            for(unsigned i=0;i<5;++i) {
                if(p->valid_mask & (UINT32_C(1)<<i))
                    printf("LITHIUM TOUCH transfer field stage=init_poll slot=%u index=%u reg=%s valid=1 value=0x%08x\n",
                        slot,i,poll_names[i],raw[i]);
                else
                    printf("LITHIUM TOUCH transfer field stage=init_poll slot=%u index=%u reg=%s valid=0 value=unread\n",
                        slot,i,poll_names[i]);
            }
        }
        for (unsigned phase=0;phase<8;++phase)
            for (unsigned pass=0;pass<2;++pass)
                for (unsigned i=0;i<LTSN_REGISTER_COUNT;++i) {
                    const struct ltsn_result *s=&rp->phase.snapshots[phase];
                    if (s->valid_mask[pass] & (UINT32_C(1)<<i))
                        printf("LITHIUM TOUCH transfer field stage=init phase=%u pass=%u index=%u reg=%s valid=1 value=0x%08x\n",
                            phase,pass,i,ltsn_registers[i].name,s->values[pass][i]);
                    else
                        printf("LITHIUM TOUCH transfer field stage=init phase=%u pass=%u index=%u reg=%s valid=0 value=unread\n",
                            phase,pass,i,ltsn_registers[i].name);
                }
        for (unsigned i=0;i<LTSN_REGISTER_COUNT;++i) {
            if (rp->guard_valid_mask & (UINT32_C(1)<<i))
                printf("LITHIUM TOUCH transfer field stage=guard index=%u reg=%s valid=1 value=0x%08x\n",
                    i,ltsn_registers[i].name,rp->guard_values[i]);
            else
                printf("LITHIUM TOUCH transfer field stage=guard index=%u reg=%s valid=0 value=unread\n",
                    i,ltsn_registers[i].name);
        }
        ltstp_log_sample("pre_load0",&rp->pre_load[0]);
        ltstp_log_sample("pre_load1",&rp->pre_load[1]);
        ltstp_log_sample("pre_load2",&rp->pre_load[2]);
        ltstp_log_sample("pre_run",&rp->pre_run);
        ltstp_log_sample("completion",&rp->last_completion);
        ltstp_log_sample("post_rx",&rp->post_rx);
        ltstp_log_sample("stop",&rp->last_stop);
        ltstp_log_sample("first_failure",&rp->first_failed_sample);
        if (rp->first_failed_offset_valid)
            printf("LITHIUM TOUCH transfer failure valid=1 access=%s off=0x%03x stage=%s\n",
                rp->first_failed_access,rp->first_failed_offset,rp->first_failed_stage);
        else
            printf("LITHIUM TOUCH transfer failure valid=0 access=unread off=unread\n");
        for (unsigned i=0;i<ltstp_write_count;++i)
            printf("LITHIUM TOUCH transfer write index=%u off=0x%03x value=0x%08x returned_on_time=%u\n",
                i,ltstp_write_trace[i].offset,ltstp_write_trace[i].value,ltstp_write_trace[i].returned_on_time);
        printf("LITHIUM TOUCH transfer end status=%s adapter_reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u fifo_reads_attempted=%u terminal_gic=unread quarantine=1\n",
            rp->transfer_complete && !ltid.poisoned ? "transaction_observed" : "stopped",
            ltid.reason ? ltid.reason : "none",ltid.reads,ltid.writes,ltid.maps,ltstp_fifo_reads);
    }
    return;
blocked:
    printf("LITHIUM TOUCH transfer end status=blocked reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u quarantine=1 owner=unverified irq_attached=0 hid_events=0\n",
        ltid.reason ? ltid.reason : "unknown",ltid.reads,ltid.writes,ltid.maps);
}
