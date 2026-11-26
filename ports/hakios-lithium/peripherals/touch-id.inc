/* R27: one explicitly requested, experimental FT5x46 register-A3 read.
 * R26 Stage1 stays byte-identical. Reuse its mapped QUP page, never expand its
 * four-field write whitelist. A separate read-only GICD page checks that
 * SPI106/INTID138 is disabled; no IRQ handler, route, reset or rail changes.
 * No software deadline can contain a permanent mapping/MMIO/barrier stall,
 * external abort, SError or XPU fault. Secure-world ownership is unverified. */
#define LTI_EXPERIMENTAL 1
#include "touch-id-core.h"
#undef LTI_EXPERIMENTAL

#ifndef LITHIUM_TOUCH_ID_MAP
#define LITHIUM_TOUCH_ID_MAP(physical, size) ml_io_map((physical), (size))
#endif
#ifndef LITHIUM_TOUCH_ID_READ
#define LITHIUM_TOUCH_ID_READ(address) LITHIUM_TOUCH_STAGE1_READ(address)
#endif
#ifndef LITHIUM_TOUCH_ID_WRITE
#define LITHIUM_TOUCH_ID_WRITE(address, value) LITHIUM_TOUCH_STAGE1_WRITE(address, value)
#endif
#ifndef LITHIUM_TOUCH_ID_DELAY
#define LITHIUM_TOUCH_ID_DELAY(us) delay_for_interval((us), NSEC_PER_USEC)
#endif

/* Stock DTS QUP6 SPI106 + 32; active HakiOS GICD mapping uses this board base.
 * See evidence/touch-r27-20261119/gic/ and the fixed original GIC driver. */
#define LTID_GICD_PHYS UINT32_C(0x09bc0000)
#define LTID_SPI_BIT (UINT32_C(1) << 10)
#define LTID_ISENABLER UINT32_C(0x110)
#define LTID_ISPENDR UINT32_C(0x210)
#define LTID_ISACTIVER UINT32_C(0x310)
#define LTID_BUDGET_US UINT64_C(100000)

static struct {
    vm_offset_t qup, gic;
    uint64_t start, last;
    unsigned reads, writes, maps;
    bool started, poisoned;
    const char *reason;
} ltid;

static bool ltid_stop(const char *reason)
{
    ltid.poisoned = true;
    if (!ltid.reason) ltid.reason = reason;
    return false;
}

static bool ltid_open(void)
{
    const uint64_t now = lts1_now();
    if (ltid.poisoned) return false;
    if (now < ltid.last) return ltid_stop("adapter_clock_rollback");
    ltid.last = now;
    if (now - ltid.start >= LTID_BUDGET_US) return ltid_stop("adapter_deadline");
    if (ltid.reads + ltid.writes >= 4096u) return ltid_stop("adapter_access_cap");
    return true;
}

static bool ltid_raw_read(vm_offset_t address, uint32_t *value)
{
    if (!address || !value || !ltid_open()) return false;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!ltid_open()) return false;
    ++ltid.reads;
    const uint32_t staged = LITHIUM_TOUCH_ID_READ(address);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!ltid_open()) return false;
    *value = staged;
    return true;
}

static bool ltid_spi_disabled(bool entry)
{
    uint32_t enable = 0, active = 0, pending = 0;
    if (!ltid.gic) return ltid_stop("gic_mapping_missing");
    if (!ltid_raw_read(ltid.gic + LTID_ISENABLER, &enable) ||
        !ltid_raw_read(ltid.gic + LTID_ISACTIVER, &active) ||
        (entry && !ltid_raw_read(ltid.gic + LTID_ISPENDR, &pending))) return false;
    if ((enable | active | pending) & LTID_SPI_BIT)
        return ltid_stop("gic_spi_enabled_active_or_entry_pending");
    return true;
}

static bool ltid_board_ready(void)
{
    /* Recheck only read-only fields in Stage1's already mapped windows. No
     * new physical board window or write permission is introduced here. */
    static const struct { uint32_t physical, mask, expected; } checks[] = {
        {LTS1_BLSP_BCR, 1, 0}, {LTS1_QUP_BCR, 1, 0},
        {LTS1_QUP_CMD, UINT32_C(0x800000f1), 0},
        {LTS1_AHB_VOTE, UINT32_C(0x8000), UINT32_C(0x8000)},
        {LTS1_AHB_HALT, UINT32_C(0xd0000000), 0},
        {LTS1_QUP_CBCR, UINT32_C(0xd0000001), 1},
        {LTS1_PIN87, UINT32_C(0x23c), UINT32_C(0x0c)},
        {LTS1_PIN88, UINT32_C(0x23c), UINT32_C(0x0c)},
        {LTS1_QUP_CFG, UINT32_C(0x371e), 0}, /* HID0/1 only, unchanged XO parent. */
    };
    for (unsigned c = 0; c < sizeof(checks) / sizeof(checks[0]); ++c) {
        vm_offset_t address = 0;
        const uint32_t physical = checks[c].physical;
        for (unsigned i = 0; i < lts1.mappings; ++i)
            if (lts1.windows[i].physical == (physical & ~UINT32_C(0xfff)))
                address = lts1.windows[i].mapped + (physical & UINT32_C(0xfff));
        if (!address) return ltid_stop("stage1_board_mapping_missing");
        uint32_t value = 0;
        if (!ltid_raw_read(address, &value)) return false;
        if ((value & checks[c].mask) != checks[c].expected)
            return ltid_stop("stage1_board_state_changed");
    }
    return true;
}

static bool ltid_read(void *cookie, uint32_t offset, uint32_t *value)
{
    (void)cookie;
    switch (offset) {
    case LTI_CONFIG: case LTI_STATE: case LTI_IO_MODES: case LTI_OPERATIONAL:
    case LTI_ERROR_FLAGS: case LTI_ERROR_FLAGS_EN: case LTI_OPERATIONAL_MASK:
    case LTI_HW_VERSION: case LTI_MX_OUTPUT_COUNT: case LTI_OUT_FIFO_COUNT:
    case LTI_MX_WRITE_COUNT: case LTI_MX_INPUT_COUNT: case LTI_MX_READ_COUNT:
    case LTI_IN_FIFO_COUNT: case LTI_IN_FIFO: case LTI_CLK_CTL:
    case LTI_I2C_STATUS: case LTI_MASTER_CONFIG:
        return ltid.qup && ltid_raw_read(ltid.qup + offset, value);
    default: return ltid_stop("adapter_read_policy");
    }
}

static bool ltid_write(void *cookie, uint32_t offset, uint32_t value)
{
    (void)cookie;
    if (!ltid.qup || !lti_write_allowed(offset, value)) return ltid_stop("adapter_write_policy");
    /* No GIC writes or permissive fallback. A later enable/active observation
     * stops this experiment. This is not exclusion from a secure-world owner. */
    if (!ltid_spi_disabled(false) || !ltid_open()) return false;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!ltid_open()) return false;
    ++ltid.writes;
    LITHIUM_TOUCH_ID_WRITE(ltid.qup + offset, value);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    return ltid_open();
}

static uint64_t ltid_now(void *cookie)
{
    (void)cookie;
    return lts1_now();
}

static void ltid_delay(void *cookie, uint32_t us)
{
    (void)cookie;
    if (us > LTI_POLL_DELAY_US) { ltid_stop("adapter_delay_policy"); return; }
    if (ltid_open()) LITHIUM_TOUCH_ID_DELAY(us);
}

static void lithium_touch_id_probe(void)
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
    for (unsigned i = 0; i < lts1.mappings; ++i)
        if (lts1.windows[i].physical == (LTS1_QUP_STATE & ~UINT32_C(0xfff)))
            ltid.qup = lts1.windows[i].mapped;
    if (!ltid.qup) { ltid_stop("stage1_qup_mapping_missing"); goto blocked; }
    if (!ltid_board_ready()) goto blocked;
    printf("LITHIUM TOUCH id begin address=0x38 reg=0xa3 bytes=1 attempts=1 experimental=1 owner=unverified nominal_hz=100000 sw_reset=0 gpio_reset=0 rails=unchanged irq_attached=0 hid_events=0\n");
    /* The GIC driver maps this already initialized distributor itself. This
     * additional read-only alias is not a register or interrupt-state change. */
    ++ltid.maps;
    ltid.gic = LITHIUM_TOUCH_ID_MAP((vm_offset_t)LTID_GICD_PHYS, (vm_size_t)0x1000);
    if (!ltid.gic) { ltid_stop("gic_mapping_failed"); goto blocked; }
    if (!ltid_open() || !ltid_spi_disabled(true)) goto blocked;
    {
        const struct lti_io io = {NULL, ltid_read, ltid_write, ltid_now, ltid_delay};
        const struct lti_result r = lti_read_chip_id(&io);
        const bool quiet = r.transfer_complete && !ltid.poisoned && ltid_spi_disabled(false);
        printf("LITHIUM TOUCH id result status=%s reason=%s raw=0x%02x complete=%u plausible=%u clean_entry=%u stop=%u quarantine=%u elapsed_us=%llu reads=%u writes=%u polls=%u\n",
            r.status, r.reason, r.id, r.transfer_complete && quiet, r.valid && quiet,
            r.clean_entry, r.stop_observed, r.quarantined || !quiet,
            (unsigned long long)r.elapsed_us, r.reads, r.writes, r.polls);
        printf("LITHIUM TOUCH id registers version=0x%08x state=0x%08x status=0x%08x operational=0x%08x core_error=0x%08x bus_error=0x%08x config=0x%08x io_modes=0x%08x\n",
            r.version, r.state, r.bus_status, r.operational, r.core_error, r.bus_error,
            r.config, r.io_modes);
        printf("LITHIUM TOUCH id end status=%s adapter_reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u irq_disabled=%u owner=unverified touch_verified=0\n",
            quiet && r.transfer_complete ? "transaction_observed" : "stopped",
            ltid.reason ? ltid.reason : "none", ltid.reads, ltid.writes, ltid.maps, quiet);
    }
    return;
blocked:
    printf("LITHIUM TOUCH id end status=blocked reason=%s physical_reads=%u physical_writes=%u extra_mappings=%u owner=unverified irq_attached=0 hid_events=0\n",
        ltid.reason ? ltid.reason : "unknown", ltid.reads, ltid.writes, ltid.maps);
}
