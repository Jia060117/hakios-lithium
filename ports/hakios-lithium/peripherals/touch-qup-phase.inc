/* R31 default-off, pre-FIFO QUP state-machine observations.
 * Preserves the old Stage1, ID, snapshot and initializer bytes. No FIFO data
 * port, new tag stream, ID call, GIC write, rail or external GPIO reset exists
 * in this path. First RUN may resume retained hardware activity: absence of
 * bus traffic, ownership and electrical readiness are NOT established.
 * The reused adapter bounds returning calls at100ms/4096 accesses including
 * GIC reads; an inner core budget cannot extend that outer envelope.
 */
#define LTSN_EXPERIMENTAL 1
#define LTSP_EXPERIMENTAL 1
#include "touch-qup-phase-core.h"
#undef LTSP_EXPERIMENTAL
#undef LTSN_EXPERIMENTAL

static bool ltspp_active;
static bool ltspp_reset_written;
static struct {
    uint32_t offset, value;
    bool returned_on_time;
} ltspp_write_trace[64];
static unsigned ltspp_write_count;

static bool ltspp_read(void *cookie, uint32_t offset, uint32_t *value)
{
    (void)cookie;
    if (!ltspp_active || ltid.poisoned) return false;
    if (!ltid.qup) return ltid_stop("state_qup_mapping_missing");
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (offset == ltsn_registers[i].offset)
            return ltid_raw_read(ltid.qup + offset, value);
    return ltid_stop("state_adapter_read_policy"); /* no FIFO-data exception */
}

/* Independent exact value policy, not the old ID union whitelist. Neither
 * FIFO data port nor OP/error acknowledge/bus-clear can be written. */
static bool ltspp_write_policy(uint32_t offset, uint32_t value)
{
    switch (offset) {
    case UINT32_C(0x00c): return value == 1;
    case LTI_STATE: return value == 0 || value == 1 || value == 3;
    case LTI_CONFIG: return value == UINT32_C(0x207);
    case LTI_ERROR_FLAGS_EN: return value == 0 || value == UINT32_C(0x3c);
    case LTI_OPERATIONAL_MASK: return value == UINT32_C(0x300);
    case LTI_MASTER_CONFIG: return value == 1;
    case LTI_MX_OUTPUT_COUNT: case LTI_MX_INPUT_COUNT: return value == 0;
    case LTI_MX_WRITE_COUNT: return value == 9;
    case LTI_MX_READ_COUNT: return value == 3;
    case LTI_IO_MODES: return value == UINT32_C(0xc0a5);
    case LTI_CLK_CTL: return value == UINT32_C(0x003e007c);
    default: return false;
    }
}

static bool ltspp_write(void *cookie, uint32_t offset, uint32_t value)
{
    (void)cookie;
    if (!ltspp_active || ltid.poisoned) return false;
    if (!ltid.qup || !ltspp_write_policy(offset, value))
        return ltid_stop("state_adapter_write_policy");
    const bool software_reset = offset == UINT32_C(0x00c);
    if (software_reset && ltspp_reset_written)
        return ltid_stop("state_second_sw_reset_forbidden");
    if (ltspp_write_count >= sizeof(ltspp_write_trace) / sizeof(ltspp_write_trace[0]))
        return ltid_stop("state_write_trace_cap");
    if (!ltid_spi_disabled(true) || !ltid_open()) return false;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!ltid_open()) return false;
    const unsigned index = ltspp_write_count++;
    ltspp_write_trace[index].offset = offset;
    ltspp_write_trace[index].value = value;
    if (software_reset) ltspp_reset_written = true;
    ++ltid.writes;
    LITHIUM_TOUCH_ID_WRITE(ltid.qup + offset, value);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    ltspp_write_trace[index].returned_on_time = ltid_open();
    return ltspp_write_trace[index].returned_on_time;
}

static uint64_t ltspp_now(void *cookie)
{
    (void)cookie;
    if (!ltspp_active) return ltid.last;
    /* A void delay can discover the tighter outer deadline. Propagate that
     * stop to the core without calling an external clock after poisoning.
     * Its required post-delay tick rejects this sentinel as timeout/rollback. */
    return ltid.poisoned ? UINT64_MAX : lts1_now();
}

static void ltspp_delay(void *cookie, uint32_t us)
{
    (void)cookie;
    if (!ltspp_active || ltid.poisoned) return;
    if (us > 10) { ltid_stop("state_adapter_delay_policy"); return; }
    if (ltid_open()) LITHIUM_TOUCH_ID_DELAY(us);
}

static void lithium_touch_qup_phase_probe(void)
{
    bool not_started = false;
    if (!__atomic_compare_exchange_n(&ltid.started, &not_started, true, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    ltid.start = ltid.last = lts1_now();
    if (ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        LITHIUM_PERIPHERAL_AFFINITY() != 1 || ml_get_max_cpu_number() != 0 ||
        !lts1.started || lts1.poisoned || !lts1.clocks_ready || !lts1.pins_ready) {
        ltid_stop("stage1_or_execution_context_not_ready"); goto blocked;
    }
    if (lts1.mappings > sizeof(lts1.windows) / sizeof(lts1.windows[0])) {
        ltid_stop("stage1_mapping_count_invalid"); goto blocked;
    }
    for (unsigned i = 0; i < lts1.mappings; ++i) {
        if (!lts1.windows[i].mapped) {
            ltid_stop("stage1_published_mapping_zero"); goto blocked;
        }
        if (lts1.windows[i].physical == (LTS1_QUP_STATE & ~UINT32_C(0xfff)))
            ltid.qup = lts1.windows[i].mapped;
    }
    if (!ltid.qup) { ltid_stop("stage1_qup_mapping_missing"); goto blocked; }
    if (!ltid_board_ready()) goto blocked;
    printf("LITHIUM TOUCH phase begin experimental=1 max_sw_reset=1 fifo_data_io=0 new_tags=0 id_call=0 first_run_bus_activity=unverified owner=unverified gpio_reset=0 rails=unchanged gic_writes=0 irq_attached=0 hid_events=0\n");
    ++ltid.maps;
    ltid.gic = LITHIUM_TOUCH_ID_MAP((vm_offset_t)LTID_GICD_PHYS, (vm_size_t)0x1000);
    if (!ltid.gic) { ltid_stop("gic_mapping_failed"); goto blocked; }
    if (!ltid_open() || !ltid_spi_disabled(true)) goto blocked;
    {
        struct ltsp_io io = {NULL, ltspp_read, ltspp_write, ltspp_now, ltspp_delay};
        ltspp_active = true;
        const struct ltsp_result r = ltsp_observe_phases(&io);
        ltspp_active = false;
        printf("LITHIUM TOUCH phase result status=%s reason=%s reset_issued=%u base_configured=%u transfer_configured=%u run_observed=%u clock_programmed=%u pause_observed=%u elapsed_us=%llu reads=%u writes=%u polls=%u\n",
            r.status, r.reason, r.reset_issued, r.base_configured, r.transfer_configured,
            r.run_observed, r.clock_programmed, r.pause_observed,
            (unsigned long long)r.elapsed_us, r.reads, r.writes, r.polls);
        if (r.failed_poll_valid)
            printf("LITHIUM TOUCH phase poll_failure valid=1 slot=%u\n", r.failed_poll_index);
        else
            printf("LITHIUM TOUCH phase poll_failure valid=0 slot=unread\n");
        static const char *const poll_names[5] = {"VERSION", "ERROR", "STATUS", "OPERATIONAL", "STATE"};
        for (unsigned slot = 0; slot < 4; ++slot) {
            const struct ltsp_poll_sample *p = &r.poll_samples[slot];
            if (!p->attempted) {
                printf("LITHIUM TOUCH phase poll slot=%u attempted=0 complete=0 valid=0x00 from=unread to=unread index=unread start_us=unread done_us=unread\n", slot);
            } else if (!p->valid_mask) {
                printf("LITHIUM TOUCH phase poll slot=%u attempted=1 complete=0 valid=0x00 from=%u to=%u index=%u start_us=%llu done_us=unread\n",
                    slot, p->from, p->target, p->poll_index, (unsigned long long)p->started_us);
            } else {
                printf("LITHIUM TOUCH phase poll slot=%u attempted=1 complete=%u valid=0x%02x from=%u to=%u index=%u start_us=%llu done_us=%llu\n",
                    slot, p->complete, p->valid_mask, p->from, p->target, p->poll_index,
                    (unsigned long long)p->started_us, (unsigned long long)p->completed_us);
            }
            const uint32_t raw[5] = {p->version, p->error, p->status, p->operational, p->state};
            for (unsigned i = 0; i < 5; ++i) {
                if (p->valid_mask & (UINT32_C(1) << i))
                    printf("LITHIUM TOUCH phase poll_field slot=%u index=%u reg=%s valid=1 value=0x%08x\n", slot, i, poll_names[i], raw[i]);
                else
                    printf("LITHIUM TOUCH phase poll_field slot=%u index=%u reg=%s valid=0 value=unread\n", slot, i, poll_names[i]);
            }
        }
        if (r.first_failed_offset_valid)
            printf("LITHIUM TOUCH phase failure valid=1 access=%s off=0x%03x\n",
                r.first_failed_access, r.first_failed_offset);
        else
            printf("LITHIUM TOUCH phase failure valid=0 access=unread off=unread\n");
        static const char *const phases[8] = {"pre", "post_reset", "base", "setup", "run", "clock", "pause", "terminal"};
        for (unsigned phase = 0; phase < 8; ++phase) {
            printf("LITHIUM TOUCH phase snapshot phase=%s attempted=%u complete=%u pass0=0x%08x pass1=0x%08x stable=0x%08x changed=0x%08x\n",
                phases[phase], r.snapshot_attempted[phase], r.snapshots[phase].complete,
                r.snapshots[phase].valid_mask[0], r.snapshots[phase].valid_mask[1],
                r.snapshots[phase].stable_mask, r.snapshots[phase].changed_mask);
            for (unsigned pass = 0; pass < LTSN_PASSES; ++pass) {
                for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i) {
                    if (r.snapshots[phase].valid_mask[pass] & (UINT32_C(1) << i))
                        printf("LITHIUM TOUCH phase field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=1 value=0x%08x at_us=%llu read_us=%llu\n",
                            phases[phase], pass, i, ltsn_registers[i].name, ltsn_registers[i].offset,
                            r.snapshots[phase].values[pass][i],
                            (unsigned long long)r.snapshots[phase].at_us[pass][i],
                            (unsigned long long)r.snapshots[phase].read_elapsed_us[pass][i]);
                    else
                        printf("LITHIUM TOUCH phase field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=0 value=unread\n",
                            phases[phase], pass, i, ltsn_registers[i].name, ltsn_registers[i].offset);
                }
            }
        }
        for (unsigned i = 0; i < ltspp_write_count; ++i)
            printf("LITHIUM TOUCH phase write index=%u off=0x%03x value=0x%08x issued=1 returned_on_time=%u\n",
                i, ltspp_write_trace[i].offset, ltspp_write_trace[i].value,
                ltspp_write_trace[i].returned_on_time);
        printf("LITHIUM TOUCH phase end adapter_reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u sw_reset_issued=%u terminal_gic=unread fifo_data_io=0 new_tags=0 id_call=0 first_run_bus_activity=unverified owner=unverified touch_verified=0\n",
            ltid.reason ? ltid.reason : "none", ltid.reads, ltid.writes, ltid.maps,
            ltspp_reset_written);
    }
    return;
blocked:
    printf("LITHIUM TOUCH phase end status=blocked reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u sw_reset_issued=%u terminal_gic=unread fifo_data_io=0 new_tags=0 id_call=0 owner=unverified touch_verified=0\n",
        ltid.reason ? ltid.reason : "unknown", ltid.reads, ltid.writes, ltid.maps,
        ltspp_reset_written);
}
