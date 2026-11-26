/* Lithium touch Stage 1: an opt-in, one-shot clock/pin/QUP observation.
 * No I2C commands, software or GPIO resets, regulator access, IRQ registration,
 * or HID publication. The default boot path never calls this implementation.
 * Source provenance: evidence/touch-stage1-20261119/{board,clocks,handoff}.
 * Software deadlines bound returning accesses only: an external abort/SError,
 * XPU fault, stalled interconnect, or a stalled ml_io_map cannot be recovered
 * here. There is deliberately no ml_probe_read call or generic abort hook. */
#if !defined(LITHIUM_TOUCH_STAGE1_HOST_TEST)
#include <kern/clock.h>
#include <machine/machine_routines.h>
/* Same declaration used by upstream hakios.goodix.c and lithium_usb.c; the
 * public BSD machine header hides it behind a kernel-private visibility gate. */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
#endif

#ifndef LITHIUM_TOUCH_STAGE1_READ
#define LITHIUM_TOUCH_STAGE1_READ(address) (*(volatile uint32_t *)(address))
#endif
#ifndef LITHIUM_TOUCH_STAGE1_WRITE
#define LITHIUM_TOUCH_STAGE1_WRITE(address, value) \
    (*(volatile uint32_t *)(address) = (value))
#endif
#ifndef LITHIUM_TOUCH_STAGE1_BARRIER
#define LITHIUM_TOUCH_STAGE1_BARRIER() __asm__ volatile("dmb sy" ::: "memory")
#endif
#ifndef LITHIUM_TOUCH_STAGE1_DELAY
#define LITHIUM_TOUCH_STAGE1_DELAY() delay_for_interval(1, NSEC_PER_USEC)
#endif

#define LTS1_BUDGET_US UINT64_C(300000)
#define LTS1_ACCESS_MAX 2048u
#define LTS1_POLL_MAX 500u
#define LTS1_AHB_VOTE UINT32_C(0x00352004)
#define LTS1_AHB_HALT UINT32_C(0x00325004)
#define LTS1_BLSP_BCR UINT32_C(0x00325000)
#define LTS1_QUP_BCR UINT32_C(0x00330000)
#define LTS1_QUP_CBCR UINT32_C(0x00330008)
#define LTS1_QUP_CMD UINT32_C(0x00330020)
#define LTS1_QUP_CFG UINT32_C(0x00330024)
#define LTS1_PIN87 UINT32_C(0x01067000)
#define LTS1_PIN88 UINT32_C(0x01068000)
#define LTS1_IRQ105_CFG UINT32_C(0x01079000)
#define LTS1_IRQ105_IN UINT32_C(0x01079004)
#define LTS1_IRQ125_CFG UINT32_C(0x0108d000)
#define LTS1_IRQ125_IN UINT32_C(0x0108d004)
#define LTS1_QUP_STATE UINT32_C(0x075ba004)
#define LTS1_QUP_VERSION UINT32_C(0x075ba030)
#define LTS1_QUP_STATUS UINT32_C(0x075ba404)

struct lts1_window { uint32_t physical; vm_offset_t mapped; };
static struct {
    struct lts1_window windows[8];
    uint64_t start_us, last_us;
    unsigned reads, writes, mappings;
    bool started, poisoned, clocks_ready, pins_ready;
    const char *reason;
} lts1;

static uint64_t lts1_now(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000;
}

static bool lts1_stop(const char *reason)
{
    lts1.poisoned = true;
    if (!lts1.reason) lts1.reason = reason;
    return false;
}

static bool lts1_open(void)
{
    const uint64_t now = lts1_now();
    if (lts1.poisoned) return false;
    if (now < lts1.last_us) return lts1_stop("clock_rollback");
    lts1.last_us = now;
    if (now - lts1.start_us >= LTS1_BUDGET_US)
        return lts1_stop("software_deadline");
    if (lts1.reads + lts1.writes >= LTS1_ACCESS_MAX)
        return lts1_stop("access_cap");
    return true;
}

static bool lts1_allowed(uint32_t physical, bool write)
{
    if (write) return physical == LTS1_AHB_VOTE || physical == LTS1_QUP_CBCR ||
        physical == LTS1_PIN87 || physical == LTS1_PIN88;
    switch (physical) {
    case LTS1_AHB_VOTE: case LTS1_AHB_HALT: case LTS1_BLSP_BCR: case LTS1_QUP_BCR:
    case LTS1_QUP_CBCR: case LTS1_QUP_CMD: case LTS1_QUP_CFG:
    case LTS1_PIN87: case LTS1_PIN88: case LTS1_IRQ105_CFG:
    case LTS1_IRQ105_IN: case LTS1_IRQ125_CFG: case LTS1_IRQ125_IN:
        return true;
    case LTS1_QUP_STATE: case LTS1_QUP_VERSION: case LTS1_QUP_STATUS:
        return lts1.clocks_ready && lts1.pins_ready;
    default: return false;
    }
}

static vm_offset_t lts1_address(uint32_t physical, bool write)
{
    if (!lts1_allowed(physical, write)) { lts1_stop("address_policy"); return 0; }
    if (!lts1_open()) return 0;
    const uint32_t page = physical & ~UINT32_C(0xfff);
    for (unsigned i = 0; i < lts1.mappings; ++i)
        if (lts1.windows[i].physical == page)
            return lts1.windows[i].mapped + (physical & UINT32_C(0xfff));
    if (lts1.mappings == 8) { lts1_stop("mapping_cap"); return 0; }
    /* ml_io_map supplies VM_WIMG_IO. It can allocate with KMA_NOFAIL; this is
     * another explicitly unbounded platform operation, not a timeout guard. */
    const vm_offset_t mapped = ml_io_map((vm_offset_t)page, (vm_size_t)0x1000);
    if (!mapped) { lts1_stop("mapping_failed"); return 0; }
    lts1.windows[lts1.mappings++] = (struct lts1_window){page, mapped};
    return lts1_open() ? mapped + (physical & UINT32_C(0xfff)) : 0;
}

static bool lts1_read(uint32_t physical, uint32_t *value)
{
    const vm_offset_t address = lts1_address(physical, false);
    if (!address) return false;
    ++lts1.reads;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    const uint32_t result = LITHIUM_TOUCH_STAGE1_READ(address);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    if (!lts1_open()) return false;
    *value = result;
    return true;
}

static bool lts1_write(uint32_t physical, uint32_t value)
{
    const vm_offset_t address = lts1_address(physical, true);
    if (!address) return false;
    ++lts1.writes;
    LITHIUM_TOUCH_STAGE1_BARRIER();
    LITHIUM_TOUCH_STAGE1_WRITE(address, value);
    LITHIUM_TOUCH_STAGE1_BARRIER();
    return lts1_open();
}

/* Every write preserves bits outside its one documented field. Avoid writing
 * already-correct state. A failed/late access poisons the one-shot, no replay. */
static bool lts1_field(uint32_t physical, uint32_t mask, uint32_t desired)
{
    uint32_t old = 0, current = 0;
    if (!lts1_read(physical, &old)) return false;
    const uint32_t next = (old & ~mask) | (desired & mask);
    if (next != old && !lts1_write(physical, next)) return false;
    if (!lts1_read(physical, &current)) return false;
    printf("LITHIUM TOUCH stage=1 field phys=0x%08x before=0x%08x after=0x%08x mask=0x%08x\n",
        physical, old, current, mask);
    return (current & mask) == (desired & mask) || lts1_stop("field_readback");
}

static bool lts1_poll(uint32_t physical, uint32_t mask, uint32_t expected)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < LTS1_POLL_MAX; ++i) {
        if (!lts1_read(physical, &value)) return false;
        if ((value & mask) == expected) {
            printf("LITHIUM TOUCH stage=1 clock phys=0x%08x value=0x%08x polls=%u\n",
                physical, value, i + 1);
            return true;
        }
        LITHIUM_TOUCH_STAGE1_DELAY();
        if (!lts1_open()) return false;
    }
    printf("LITHIUM TOUCH stage=1 clock phys=0x%08x value=0x%08x polls=%u\n",
        physical, value, LTS1_POLL_MAX);
    return lts1_stop("clock_poll_cap");
}

/* MiCode clock-local2.c handoff matching treats HID=0 (bypass) and HID=1
 * (/1) as equivalent: reject mismatched dividers only if either is >1.
 * Source=0 and MND mode=0 are required in both cases. R25's canonical-only
 * observation is preserved unless touch_clock_init=1 explicitly selects
 * this vendor-recognized handoff. CFG and CMD are never written. */
static bool lts1_root_known(uint32_t cfg, bool initialize_clocks)
{
    const uint32_t field = cfg & UINT32_C(0x371f);
    return field == 1u || (initialize_clocks && field == 0u);
}

static void lithium_touch_stage1_run_context(bool initialize_clocks, bool live_smp)
{
    bool not_started = false;
    if (!__atomic_compare_exchange_n(&lts1.started, &not_started, true, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    lts1.start_us = lts1.last_us = lts1_now();
    if (ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        LITHIUM_PERIPHERAL_AFFINITY() != 1 ||
        (ml_get_max_cpu_number() != 0 &&
         !(live_smp && ml_get_max_cpu_number() > 0 &&
           ml_get_max_cpu_number() <= 2))) {
        lts1_stop("execution_context");
        goto finish;
    }
    printf("LITHIUM TOUCH stage=1 begin budget_us=300000 access_cap=2048 poll_cap=500 rails=unverified secure_owner=unverified reset=0 transfer=0 irq_attached=0 hid_events=0 uncontained_faults=external_abort,SError,XPU,stalled_MMIO\n");
    uint32_t blsp_bcr = 0, bcr = 0, cmd = 0, cfg = 0;
    printf("LITHIUM TOUCH stage=1 phase=gcc_precheck next_phys=0x00325000\n");
    if (!lts1_read(LTS1_BLSP_BCR, &blsp_bcr) || !lts1_read(LTS1_QUP_BCR, &bcr) || !lts1_read(LTS1_QUP_CMD, &cmd) ||
        !lts1_read(LTS1_QUP_CFG, &cfg)) goto finish;
    printf("LITHIUM TOUCH stage=1 inherited blsp_bcr=0x%08x bcr=0x%08x cmd=0x%08x cfg=0x%08x\n", blsp_bcr, bcr, cmd, cfg);
    /* MiCode's F(19200000,cxo,1,0,0): source=0, HID=1, MND mode=0.
     * No parent/PLL/rate programming or reset deassertion is guessed here. */
    if ((blsp_bcr & 1u) || (bcr & 1u) || (cmd & UINT32_C(0xf1)) ||
        !lts1_root_known(cfg, initialize_clocks)) {
        lts1_stop("inherited_root_or_reset_unverified"); goto finish;
    }
    if (initialize_clocks) {
        printf("LITHIUM TOUCH stage=1 clock_plan cfg=0x%08x source=XO hid=%u handoff=vendor_equivalent_0_or_1 rcg_write=0 rate_hz=unmeasured owner=unverified\n",
            cfg, cfg & UINT32_C(0x1f));
    }
    const uint32_t inherited_cfg = cfg & UINT32_C(0x371f);
    printf("LITHIUM TOUCH stage=1 phase=enable_clocks next_phys=0x00352004\n");
    /* Vendor clock-branch ON is top nibble 0 or 2. Leave bit29 free,
     * require bits31/30/28 clear; bit31-clear alone accepts transition states. */
    if (!lts1_field(LTS1_AHB_VOTE, UINT32_C(1) << 15, UINT32_C(1) << 15) ||
        !lts1_poll(LTS1_AHB_HALT, UINT32_C(0xd0000000), 0) ||
        !lts1_field(LTS1_QUP_CBCR, 1u, 1u) ||
        !lts1_poll(LTS1_QUP_CBCR, UINT32_C(0xd0000001), 1u) ||
        !lts1_poll(LTS1_QUP_CMD, UINT32_C(0x800000f1), 0)) goto finish;
    if (!lts1_read(LTS1_BLSP_BCR, &blsp_bcr) || !lts1_read(LTS1_QUP_BCR, &bcr) ||
        !lts1_read(LTS1_QUP_CMD, &cmd) || !lts1_read(LTS1_QUP_CFG, &cfg)) goto finish;
    if ((blsp_bcr & 1u) || (bcr & 1u) || (cmd & UINT32_C(0x800000f1)) ||
        !lts1_root_known(cfg, initialize_clocks) ||
        (cfg & UINT32_C(0x371f)) != inherited_cfg) {
        lts1_stop("root_or_reset_changed"); goto finish;
    }
    lts1.clocks_ready = true;
    printf("LITHIUM TOUCH stage=1 phase=pinmux next_phys=0x01067000 electrical_pull_drive=preserved\n");
    /* TLMM mux[5:2]=3 selects blsp_i2c12; OE[9]=0. Neither GPIO89 reset
     * nor either IRQ pin's configuration/output registers are written. */
    if (!lts1_field(LTS1_PIN87, UINT32_C(0x23c), UINT32_C(0x0c)) ||
        !lts1_field(LTS1_PIN88, UINT32_C(0x23c), UINT32_C(0x0c))) goto finish;
    lts1.pins_ready = true;
    printf("LITHIUM TOUCH stage=1 phase=qup_snapshot next_phys=0x075ba004\n");
    uint32_t state = 0, version = 0, status = 0;
    if (!lts1_read(LTS1_QUP_STATE, &state) || !lts1_read(LTS1_QUP_VERSION, &version) ||
        !lts1_read(LTS1_QUP_STATUS, &status)) goto finish;
    printf("LITHIUM TOUCH stage=1 qup state=0x%08x version=0x%08x i2c_status=0x%08x runtime_chip_id=unverified reset=0 transfer=0\n",
        state, version, status);
    uint32_t cfg105 = 0, in105 = 0, cfg125 = 0, in125 = 0;
    printf("LITHIUM TOUCH stage=1 phase=irq_inputs next_phys=0x01079000\n");
    if (!lts1_read(LTS1_IRQ105_CFG, &cfg105) || !lts1_read(LTS1_IRQ105_IN, &in105) ||
        !lts1_read(LTS1_IRQ125_CFG, &cfg125) || !lts1_read(LTS1_IRQ125_IN, &in125)) goto finish;
    printf("LITHIUM TOUCH stage=1 gpio105_cfg=0x%08x gpio105_in=%u gpio125_cfg=0x%08x gpio125_in=%u irq_runtime=unverified single_snapshot=1\n",
        cfg105, in105 & 1u, cfg125, in125 & 1u);
    lts1.reason = "snapshot_complete_not_touch_verified";
finish:
    printf("LITHIUM TOUCH stage=1 end status=%s reason=%s reads=%u writes=%u mappings=%u elapsed_us=%llu clocks_checked=%u pins_checked=%u reset=0 transfer=0 irq_attached=0 hid_events=0\n",
        lts1.poisoned ? "stopped" : "observed", lts1.reason ? lts1.reason : "unknown",
        lts1.reads, lts1.writes, lts1.mappings,
        (unsigned long long)(lts1.last_us - lts1.start_us), lts1.clocks_ready, lts1.pins_ready);
}

static void lithium_touch_stage1_run(bool initialize_clocks)
{
    lithium_touch_stage1_run_context(initialize_clocks, false);
}

static void lithium_touch_stage1_probe(void)
{
    lithium_touch_stage1_run(false);
}

static void __attribute__((unused)) lithium_touch_stage1_initialize(void)
{
    lithium_touch_stage1_run(true);
}
