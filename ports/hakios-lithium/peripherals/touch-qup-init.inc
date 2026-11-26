/* R29 experimental QUP6 cold initialization + conditional unchanged ID core.
 * The R26 board setup, R27 ID and R28 snapshot paths remain byte-identical.
 * Secure ownership/electrical readiness and permanently stalling accesses
 * remain unverified; this separate opt-in is NOT an ownership attestation.
 * No regulator, GPIO reset, GIC write, IRQ handler or HID event is introduced.
 */
#define LTI_EXPERIMENTAL 1
#define LTSN_EXPERIMENTAL 1
#define LTIQ_EXPERIMENTAL 1
#include "touch-qup-init-core.h"
#undef LTIQ_EXPERIMENTAL
#undef LTSN_EXPERIMENTAL
#undef LTI_EXPERIMENTAL

static bool ltiqp_active;
static bool ltiqp_reset_written;
static struct {
    uint32_t offset, value;
    bool returned_on_time;
} ltiqp_write_trace[64];
static unsigned ltiqp_write_count;

static bool ltiqp_read(void *cookie, uint32_t offset, uint32_t *value)
{
    (void)cookie;
    if (!ltiqp_active) return false;
    if (!ltid.qup) return ltid_stop("init_qup_mapping_missing");
    /* The portable core further restricts FIFO data access to its one
     * delegated ID window. The initializer snapshots exclude FIFO data. */
    if (offset == LTI_IN_FIFO)
        return ltid_raw_read(ltid.qup + offset, value);
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (offset == ltsn_registers[i].offset)
            return ltid_raw_read(ltid.qup + offset, value);
    return ltid_stop("init_adapter_read_policy");
}

static bool ltiqp_write(void *cookie, uint32_t offset, uint32_t value)
{
    (void)cookie;
    if (!ltiqp_active) return false;
    const bool software_reset = offset == UINT32_C(0x00c) && value == 1;
    const bool base_configuration =
        (offset == LTI_CONFIG && value == UINT32_C(0x207)) ||
        (offset == LTI_ERROR_FLAGS_EN && value == UINT32_C(0x3c)) ||
        (offset == LTI_OPERATIONAL_MASK && value == UINT32_C(0x300)) ||
        (offset == LTI_MASTER_CONFIG && value == 1);
    if (!ltid.qup || (!software_reset && !base_configuration &&
        !lti_write_allowed(offset, value))) return ltid_stop("init_adapter_write_policy");
    if (software_reset && ltiqp_reset_written) return ltid_stop("init_second_sw_reset_forbidden");
    if (ltiqp_write_count >= sizeof(ltiqp_write_trace) / sizeof(ltiqp_write_trace[0]))
        return ltid_stop("init_write_trace_cap");
    /* Same read-only GIC access view as the old experiment. Check every write;
     * this cannot exclude a secure-world owner or certify global masking. */
    if (!ltid_spi_disabled(true) || !ltid_open()) return false;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!ltid_open()) return false;
    const unsigned index = ltiqp_write_count++;
    ltiqp_write_trace[index].offset = offset;
    ltiqp_write_trace[index].value = value;
    if (software_reset) ltiqp_reset_written = true; /* issued, even if late */
    ++ltid.writes;
    LITHIUM_TOUCH_ID_WRITE(ltid.qup + offset, value);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    ltiqp_write_trace[index].returned_on_time = ltid_open();
    return ltiqp_write_trace[index].returned_on_time;
}

static uint64_t ltiqp_now(void *cookie)
{
    (void)cookie;
    return ltiqp_active ? lts1_now() : ltid.last;
}

static void ltiqp_delay(void *cookie, uint32_t us)
{
    (void)cookie;
    if (!ltiqp_active) return;
    if (us > LTI_POLL_DELAY_US) { ltid_stop("init_adapter_delay_policy"); return; }
    if (ltid_open()) LITHIUM_TOUCH_ID_DELAY(us);
}

static void lithium_touch_qup_init_probe(void)
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
    printf("LITHIUM TOUCH init begin experimental=1 max_sw_reset=1 conditional_id=1 address=0x38 reg=0xa3 owner=unverified gpio_reset=0 rails=unchanged gic_writes=0 irq_attached=0 hid_events=0\n");
    ++ltid.maps;
    ltid.gic = LITHIUM_TOUCH_ID_MAP((vm_offset_t)LTID_GICD_PHYS, (vm_size_t)0x1000);
    if (!ltid.gic) { ltid_stop("gic_mapping_failed"); goto blocked; }
    if (!ltid_open() || !ltid_spi_disabled(true)) goto blocked;
    {
        struct ltiq_io io = {NULL, ltiqp_read, ltiqp_write, ltiqp_now, ltiqp_delay};
        ltiqp_active = true;
        const struct ltiq_result r = ltiq_initialize_and_id(&io);
        ltiqp_active = false;
        /* The core owns terminal observations. Do not append MMIO or clock
         * callbacks after a failure just to make a summary look complete. */
        printf("LITHIUM TOUCH init result status=%s reason=%s reset_issued=%u configured=%u id_attempted=%u elapsed_us=%llu reads=%u writes=%u polls=%u\n",
            r.status, r.reason, r.reset_issued, r.configured, r.id_attempted,
            (unsigned long long)r.elapsed_us, r.reads, r.writes, r.polls);
        if (r.first_failed_offset_valid)
            printf("LITHIUM TOUCH init failure valid=1 access=%s off=0x%03x\n",
                r.first_failed_access, r.first_failed_offset);
        else
            printf("LITHIUM TOUCH init failure valid=0 access=unread off=unread\n");
        static const char *const phases[4] = {"pre", "post_reset", "post_config", "terminal"};
        for (unsigned phase = 0; phase < 4; ++phase) {
            printf("LITHIUM TOUCH init snapshot phase=%s attempted=%u complete=%u pass0=0x%08x pass1=0x%08x stable=0x%08x changed=0x%08x\n",
                phases[phase], r.snapshot_attempted[phase], r.snapshots[phase].complete,
                r.snapshots[phase].valid_mask[0], r.snapshots[phase].valid_mask[1],
                r.snapshots[phase].stable_mask, r.snapshots[phase].changed_mask);
            for (unsigned pass = 0; pass < LTSN_PASSES; ++pass) {
                for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i) {
                    if (r.snapshots[phase].valid_mask[pass] & (UINT32_C(1) << i))
                        printf("LITHIUM TOUCH init field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=1 value=0x%08x at_us=%llu read_us=%llu\n",
                            phases[phase], pass, i, ltsn_registers[i].name, ltsn_registers[i].offset,
                            r.snapshots[phase].values[pass][i],
                            (unsigned long long)r.snapshots[phase].at_us[pass][i],
                            (unsigned long long)r.snapshots[phase].read_elapsed_us[pass][i]);
                    else
                        printf("LITHIUM TOUCH init field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=0 value=unread\n",
                            phases[phase], pass, i, ltsn_registers[i].name, ltsn_registers[i].offset);
                }
            }
        }
        for (unsigned i = 0; i < ltiqp_write_count; ++i)
            printf("LITHIUM TOUCH init write index=%u off=0x%03x value=0x%08x issued=1 returned_on_time=%u\n",
                i, ltiqp_write_trace[i].offset, ltiqp_write_trace[i].value,
                ltiqp_write_trace[i].returned_on_time);
        if (r.id_attempted && r.id.transfer_complete)
            printf("LITHIUM TOUCH init id status=%s reason=%s raw=0x%02x complete=%u plausible=%u clean_entry=%u stop=%u quarantine=%u elapsed_us=%llu reads=%u writes=%u polls=%u owner=unverified\n",
                r.id.status, r.id.reason, r.id.id, r.id.transfer_complete, r.id.valid,
                r.id.clean_entry, r.id.stop_observed, r.id.quarantined,
                (unsigned long long)r.id.elapsed_us, r.id.reads, r.id.writes, r.id.polls);
        else if (r.id_attempted)
            printf("LITHIUM TOUCH init id attempted=1 status=%s reason=%s raw=unread complete=0 clean_entry=%u stop=%u quarantine=%u elapsed_us=%llu reads=%u writes=%u polls=%u owner=unverified\n",
                r.id.status, r.id.reason, r.id.clean_entry, r.id.stop_observed,
                r.id.quarantined, (unsigned long long)r.id.elapsed_us,
                r.id.reads, r.id.writes, r.id.polls);
        else
            printf("LITHIUM TOUCH init id attempted=0 raw=unread complete=0 owner=unverified\n");
        printf("LITHIUM TOUCH init end adapter_reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u sw_reset_issued=%u owner=unverified touch_verified=0\n",
            ltid.reason ? ltid.reason : "none", ltid.reads, ltid.writes, ltid.maps,
            ltiqp_reset_written);
    }
    return;
blocked:
    printf("LITHIUM TOUCH init end status=blocked reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u sw_reset_issued=%u id_attempted=0 owner=unverified touch_verified=0\n",
        ltid.reason ? ltid.reason : "unknown", ltid.reads, ltid.writes, ltid.maps,
        ltiqp_reset_written);
}
