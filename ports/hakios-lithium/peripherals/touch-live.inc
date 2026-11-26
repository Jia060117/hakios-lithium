/* Lithium FT8716/FT5x46 input producer. Explicit touch=1 only.
 * Board: stock lithium DT /soc/i2c@75ba000/ft5x46_i2c@38.
 * GPIO89 reset low 10ms, high 300ms follows ft5x46_hw_init_work/probe.
 * Supplies are inherited from the illuminated panel; no firmware update or
 * regulator voltage programming. If the chip does not answer, stop and log.
 * Polling uses the actual HakiOS input queue consumed by libhakios/inject.c.
 */
#include "touch-live-qup.h"
#include "touch-live-events.h"

static struct {
    vm_offset_t qup, reset, irq, scl, sda;
    thread_call_t poll_call;
    struct ltle_state events;
    uint64_t reports, posted, failures, malformed;
    uint32_t consecutive_errors;
    bool running, verbose, smp, smp_poll_logged;
} lt_live;

void hakios_lithium_touch_stop(void);

void hakios_lithium_touch_stop(void)
{
    __atomic_store_n(&lt_live.running, false, __ATOMIC_RELEASE);
    if (lt_live.poll_call) {
        thread_call_cancel_wait(lt_live.poll_call);
        thread_call_cancel(lt_live.poll_call);
    }
    if (lt_live.events.initialized)
        (void)ltle_release_all(&lt_live.events, ltle_hakios_sink, NULL);
}

static uint32_t lt_live_rd(vm_offset_t address)
{
    __asm__ volatile("dmb sy" ::: "memory");
    uint32_t value = *(volatile uint32_t *)address;
    __asm__ volatile("dmb sy" ::: "memory");
    return value;
}

static void lt_live_wr(vm_offset_t address, uint32_t value)
{
    __asm__ volatile("dmb sy" ::: "memory");
    *(volatile uint32_t *)address = value;
    __asm__ volatile("dmb sy" ::: "memory");
}

static uint32_t lt_live_read(void *cookie, uint32_t offset)
{
    (void)cookie;
    return lt_live_rd(lt_live.qup + offset);
}

static void lt_live_write(void *cookie, uint32_t offset, uint32_t value)
{
    (void)cookie;
    lt_live_wr(lt_live.qup + offset, value);
}

static uint64_t lt_live_now(void *cookie)
{
    (void)cookie;
    return lts1_now();
}

static void lt_live_delay(void *cookie, uint32_t us)
{
    (void)cookie;
    delay_for_interval(us, NSEC_PER_USEC);
}

static struct ltlq_io lt_live_bus = {
    NULL, lt_live_read, lt_live_write, lt_live_now, lt_live_delay
};

/* Single coordinate mapping lives in touch-live-events.h. Queue coordinates
 * are normalized 0..65535; software display remains R20 1080x2040/scale3. */
static int lt_live_emit(void *cookie, const struct ltle_batch *batch)
{
    (void)cookie;
    int error = ltle_hakios_sink(NULL, batch);
    if (error) return error;
    for (unsigned i = 0; i < batch->count; ++i) {
        const struct ltle_event *e = &batch->events[i];
        ++lt_live.posted;
        if (lt_live.verbose && (e->type != HAKIOS_TOUCH_MOVE || lt_live.posted % 20 == 0))
            printf("LITHIUM TOUCH event type=%u finger=%u x=%u y=%u total=%llu\n",
                e->type, e->finger, e->x, e->y, (unsigned long long)lt_live.posted);
    }
    return 0;
}

static void lt_live_poll_work(void)
{
    if (!__atomic_load_n(&lt_live.running, __ATOMIC_ACQUIRE)) return;
    uint8_t report[63];
    struct ltlq_result result = {0};
    int error = ltlq_read_register(&lt_live_bus, 0, report, sizeof(report), &result);
    if (!error) {
        ++lt_live.reports;
        lt_live.consecutive_errors = 0;
        enum ltle_result parsed = ltle_report(&lt_live.events, report, sizeof(report),
            lt_live_emit, NULL);
        if (parsed != LTLE_OK && (++lt_live.malformed == 1 || lt_live.malformed % 120 == 0))
            printf("LITHIUM TOUCH report_error code=%u total=%llu prefix=%02x%02x%02x first=%02x%02x%02x%02x\n",
                (unsigned)parsed, (unsigned long long)lt_live.malformed,
                report[0], report[1], report[2], report[3], report[4], report[5], report[6]);
        if (lt_live.reports == 1 || (lt_live.verbose && lt_live.reports % 600 == 0))
            printf("LITHIUM TOUCH report number=%llu mode=0x%02x points=%u posted=%llu irq=%u\n",
                (unsigned long long)lt_live.reports, report[0], report[2] & 15u,
                (unsigned long long)lt_live.posted, lt_live_rd(lt_live.irq + 4) & 1u);
    } else {
        ++lt_live.failures;
        if (++lt_live.consecutive_errors == 1)
            printf("LITHIUM TOUCH read_error error=%d status=0x%08x op=0x%08x core=0x%08x polls=%u\n",
                error, result.status, result.op, result.core, result.polls);
        if (lt_live.consecutive_errors >= 3) {
            __atomic_store_n(&lt_live.running, false, __ATOMIC_RELEASE);
            (void)ltle_release_all(&lt_live.events, lt_live_emit, NULL);
            printf("LITHIUM TOUCH stopped reason=three_consecutive_read_errors reports=%llu failures=%llu\n",
                (unsigned long long)lt_live.reports, (unsigned long long)lt_live.failures);
            return;
        }
    }
    if (!__atomic_load_n(&lt_live.running, __ATOMIC_ACQUIRE)) return;
    uint64_t deadline;
    clock_interval_to_deadline(16, NSEC_PER_MSEC, &deadline);
    thread_call_enter_delayed(lt_live.poll_call, deadline);
}

static void lt_live_poll(__unused thread_call_param_t p0,
    __unused thread_call_param_t p1)
{
    if (!__atomic_load_n(&lt_live.running, __ATOMIC_ACQUIRE)) return;
    struct lithium_touch_cpu_scope scope = {0};
    if (lt_live.smp) {
        if (!lithium_touch_cpu_enter(&scope)) {
            __atomic_store_n(&lt_live.running, false, __ATOMIC_RELEASE);
            (void)ltle_release_all(&lt_live.events, lt_live_emit, NULL);
            printf("LITHIUM TOUCH stopped reason=smp_poll_cpu_bind mmio=0\n");
            return;
        }
        if (!lt_live.smp_poll_logged) {
            printf("LITHIUM TOUCH smp context=poll logical=%d affinity=0x%llx max_logical=%d bound=1\n",
                cpu_number(), (unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),
                ml_get_max_cpu_number());
            lt_live.smp_poll_logged = true;
        }
    }
    lt_live_poll_work();
    lithium_touch_cpu_leave(&scope);
}

static __attribute__((noinline)) void lithium_touch_live_start(bool verbose)
{
    if (lt_live.running || lt_live.poll_call) return;
    lt_live.smp = lithium_peripheral_requested.touch_smp;
    lithium_touch_stage1_run_context(true, lt_live.smp);
    if (!lts1.clocks_ready || !lts1.pins_ready || lts1.poisoned) {
        printf("LITHIUM TOUCH live stopped reason=controller_clock_or_pin_setup\n");
        return;
    }
    lt_live.qup = ml_io_map(0x075ba000, 0x1000);
    lt_live.reset = ml_io_map(0x01069000, 0x1000); /* TLMM GPIO89 */
    lt_live.irq = ml_io_map(0x0108d000, 0x1000);   /* GPIO125, polled */
    lt_live.scl = ml_io_map(0x01067000, 0x1000);   /* GPIO87/88 QUP12 */
    lt_live.sda = ml_io_map(0x01068000, 0x1000);
    if (!lt_live.qup || !lt_live.reset || !lt_live.irq || !lt_live.scl || !lt_live.sda) {
        printf("LITHIUM TOUCH live stopped reason=mmio_mapping\n");
        return;
    }
    vm_offset_t gic = ml_io_map(0x09bc0000, 0x1000);
    if (!gic || (lt_live_rd(gic + 0x110) & (1u << 10))) {
        printf("LITHIUM TOUCH live stopped reason=qup_irq138_already_enabled\n");
        return;
    }
    /* Stock i2c_12_active: function3, 2mA, bias-disable, input. Unlike the
     * historical snapshot path this also removes inherited sleep pulldowns. */
    lt_live_wr(lt_live.scl, (lt_live_rd(lt_live.scl) & ~0x3ffu) | 0x0cu);
    lt_live_wr(lt_live.sda, (lt_live_rd(lt_live.sda) & ~0x3ffu) | 0x0cu);
    /* Stock ts_active: GPIO125 input, 16mA, pull-up. GIC stays untouched. */
    lt_live_wr(lt_live.irq, (lt_live_rd(lt_live.irq) & ~0x3ffu) | 0x1c3u);
    uint32_t reset_cfg = lt_live_rd(lt_live.reset);
    uint32_t reset_io = lt_live_rd(lt_live.reset + 4);
    printf("LITHIUM TOUCH live board bus=12 addr=0x38 reset_gpio=89 irq_gpio=125 poll_ms=16 inherited_reset_cfg=0x%08x io=0x%08x\n",
        reset_cfg, reset_io);
    /* Set the latch before enabling the output to avoid a high glitch. */
    lt_live_wr(lt_live.reset + 4, reset_io & ~2u);
    lt_live_wr(lt_live.reset, (reset_cfg & ~0x3ffu) | 0x200u);
    delay_for_interval(10, NSEC_PER_MSEC);
    lt_live_wr(lt_live.reset + 4, (reset_io & ~2u) | 2u);
    delay_for_interval(300, NSEC_PER_MSEC);
    printf("LITHIUM TOUCH live reset low_ms=10 high_settle_ms=300 cfg=0x%08x io=0x%08x supplies=inherited firmware_write=0\n",
        lt_live_rd(lt_live.reset), lt_live_rd(lt_live.reset + 4));

    uint8_t id = 0;
    struct ltlq_result result = {0};
    int error = ltlq_read_register(&lt_live_bus, 0xa3, &id, 1, &result);
    printf("LITHIUM TOUCH live id error=%d byte=0x%02x status=0x%08x op=0x%08x core=0x%08x polls=%u\n",
        error, id, result.status, result.op, result.core, result.polls);
    if (error) return;
    /* FT8716 and FTE716 normal register IDs in the stock driver. Unknown
     * successful reads are retained but do not authorize parsing reports. */
    if (id != 0x87 && id != 0xe7 && id != 0x54) {
        printf("LITHIUM TOUCH live stopped reason=unexpected_chip_id raw=0x%02x\n", id);
        return;
    }
    lt_live.verbose = verbose;
    (void)ltle_init(&lt_live.events, 1080, 2040, 0);
    lt_live.poll_call = thread_call_allocate(lt_live_poll, NULL);
    if (!lt_live.poll_call) {
        printf("LITHIUM TOUCH live stopped reason=poll_allocation\n");
        return;
    }
    __atomic_store_n(&lt_live.running, true, __ATOMIC_RELEASE);
    printf("LITHIUM TOUCH live ready id=0x%02x report_bytes=63 sink=hakios_touch_post coordinates=normalized65535 polling=1\n", id);
    thread_call_enter(lt_live.poll_call);
}
