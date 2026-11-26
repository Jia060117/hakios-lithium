/* R28: two bounded read-only observations of the inherited QUP entry state.
 * The existing R27 board/GIC prerequisite helpers and map lifetime are reused;
 * no ID call, QUP write, reset, bus clear, IRQ/HID or recovery occurs here.
 * Register offsets follow the fixed vendor header; FIFO data ports are not
 * read. Hardware read-side effects/security visibility remain unverified.
 * A permanent MMIO/map/barrier stall, external abort/SError/XPU is uncontained.
 */
#define LTSN_EXPERIMENTAL 1
#include "touch-snapshot-core.h"
#undef LTSN_EXPERIMENTAL

static bool ltsnp_active;

/* This wrapper deliberately excludes the data FIFO even though the separate
 * R27 transaction adapter has a read permission for it. No write callback. */
static bool ltsnp_read(void *cookie, uint32_t offset, uint32_t *value)
{
    (void)cookie;
    if (!ltsnp_active) return false; /* Retained callbacks cannot extend the one-shot. */
    if (!ltid.qup) return ltid_stop("snapshot_qup_mapping_missing");
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (offset == ltsn_registers[i].offset)
            return ltid_raw_read(ltid.qup + offset, value);
    return ltid_stop("snapshot_read_policy");
}

static void lithium_touch_snapshot_probe(void)
{
    bool not_started = false;
    /* Share the R27 latch as a second line of exclusion, even if a future
     * caller bypasses the current mutually exclusive parser/dispatch. */
    if (!__atomic_compare_exchange_n(&ltid.started, &not_started, true, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    ltid.start = ltid.last = lts1_now();
    if (ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        LITHIUM_PERIPHERAL_AFFINITY() != 1 || ml_get_max_cpu_number() != 0 ||
        !lts1.started || lts1.poisoned || !lts1.clocks_ready || !lts1.pins_ready) {
        ltid_stop("stage1_or_execution_context_not_ready"); goto blocked;
    }
    /* Stage1 publishes only nonzero windows and at most its fixed array.
     * Validate that private contract again before handing any address to
     * the shared helpers; damaged state must not form a low MMIO address. */
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
    printf("LITHIUM TOUCH snapshot begin passes=2 registers=17 transaction=0 qup_writes=0 fifo_data_reads=0 sw_reset=0 gpio_reset=0 irq_attached=0 hid_events=0 owner=unverified\n");
    ++ltid.maps;
    ltid.gic = LITHIUM_TOUCH_ID_MAP((vm_offset_t)LTID_GICD_PHYS, (vm_size_t)0x1000);
    if (!ltid.gic) { ltid_stop("gic_mapping_failed"); goto blocked; }
    if (!ltid_open() || !ltid_spi_disabled(true)) goto blocked;
    {
        const struct ltsn_io io = {NULL, ltsnp_read, ltid_now};
        ltsnp_active = true;
        const struct ltsn_result r = ltsn_capture(&io);
        ltsnp_active = false;
        /* No follow-up MMIO after a failed capture. Completed two-pass
         * observations may take one more read-only GIC check before logging. */
        const bool terminal_attempted = r.complete && !ltid.poisoned;
        const bool terminal_quiet = terminal_attempted && ltid_spi_disabled(false);
        printf("LITHIUM TOUCH snapshot summary status=%s reason=%s complete=%u attempts=%u accepted=%u elapsed_us=%llu\n",
            r.status, r.reason, r.complete, r.attempts, r.completed_reads,
            (unsigned long long)r.elapsed_us);
        printf("LITHIUM TOUCH snapshot validity pass0=0x%08x pass1=0x%08x stable=0x%08x changed=0x%08x all=0x%08x\n",
            r.valid_mask[0], r.valid_mask[1], r.stable_mask, r.changed_mask,
            LTSN_ALL_VALID_MASK);
        for (unsigned pass = 0; pass < LTSN_PASSES; ++pass) {
            for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i) {
                if (r.valid_mask[pass] & (UINT32_C(1) << i)) {
                    printf("LITHIUM TOUCH snapshot field pass=%u index=%u reg=%s off=0x%03x valid=1 value=0x%08x at_us=%llu read_us=%llu\n",
                        pass, i, ltsn_registers[i].name, ltsn_registers[i].offset, r.values[pass][i],
                        (unsigned long long)r.at_us[pass][i],
                        (unsigned long long)r.read_elapsed_us[pass][i]);
                } else {
                    printf("LITHIUM TOUCH snapshot field pass=%u index=%u reg=%s off=0x%03x valid=0 value=unread\n",
                        pass, i, ltsn_registers[i].name, ltsn_registers[i].offset);
                }
            }
        }
        printf("LITHIUM TOUCH snapshot end status=%s adapter_reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u\n",
            r.complete && terminal_quiet ? "observed" : "stopped",
            ltid.reason ? ltid.reason : "none", ltid.reads, ltid.writes, ltid.maps);
        printf("LITHIUM TOUCH snapshot scope entry_gic_checked=1 terminal_gic_attempted=%u terminal_gic_quiet=%u transaction=0 id_read=0 owner=unverified touch_verified=0\n",
            terminal_attempted, terminal_quiet);
    }
    return;
blocked:
    printf("LITHIUM TOUCH snapshot end status=blocked reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u transaction=0 id_read=0\n",
        ltid.reason ? ltid.reason : "unknown", ltid.reads, ltid.writes, ltid.maps);
}
