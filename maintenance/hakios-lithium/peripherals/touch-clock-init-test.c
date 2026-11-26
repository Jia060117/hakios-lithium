/* Host-only R26 tests. Compile with -I pointing to the sibling directory
 * clock-init-assert; it changes one old CFG=1 assertion
 * to CFG=0/1 only for explicit-mode tests. The old 351 scenarios run first
 * with that adaptation disabled. Production and the old fixture are included
 * verbatim, and both public entry points use the same actual atomic one-shot.
 *
 * RAM-returning fault/delay models prove software behavior only. They do not
 * model hardware ownership, electrical behavior, external aborts or a hung
 * interconnect. No device access or production fallback exists in this test. */
#include <stdlib.h>

#define R26_STAGE1_ASSERT_ADAPTER 1
#define main retained_stage1_main
#include "touch-stage1-test.c"
#undef main

static bool r26_explicit_active;
static unsigned r26_cfg0_assertions;
static unsigned r26_nested_io;
static bool r26_nested_fired;
static bool r26_nested_initialize;
static unsigned r26_clock_checks;

static void r26_assert_check(int passed, const char *expression,
    const char *file, unsigned line)
{
    const bool retained_fixture = strstr(file, "touch-stage1-test.c") != NULL;
    if (!passed && retained_fixture && r26_explicit_active &&
        strcmp(expression, "(*word(0x00330024) & 0x371f) == 1") == 0 &&
        (*word(LTS1_QUP_CFG) & UINT32_C(0x371f)) == 0 &&
        (*word(LTS1_BLSP_BCR) & 1u) == 0 &&
        (*word(LTS1_QUP_BCR) & 1u) == 0 &&
        (*word(LTS1_QUP_CMD) & UINT32_C(0x800000f1)) == 0 &&
        lts1.clocks_ready && lts1.pins_ready) {
        ++r26_cfg0_assertions;
        return;
    }
    if (!passed) {
        fprintf(stderr, "%s:%u: assertion failed: %s\n", file, line, expression);
        abort();
    }
    /* Optional reentry injection at the existing mock's pre-read/pre-write
     * assertions. The assertion itself has already passed. Neither the mock
     * nor production is replaced; nested initialization/probing must be inert. */
    if (retained_fixture && !r26_nested_fired && r26_nested_io &&
        mock.reads + mock.writes + 1 == r26_nested_io &&
        (strcmp(expression, "permitted") == 0 ||
         strcmp(expression, "mask != 0") == 0)) {
        r26_nested_fired = true;
        const unsigned trace = mock.trace_count, times = mock.times;
        const size_t logs = mock.log_size;
        if (r26_nested_initialize) lithium_touch_stage1_initialize();
        else lithium_touch_stage1_probe();
        if (trace != mock.trace_count || times != mock.times ||
            logs != mock.log_size) {
            fprintf(stderr, "%s:%u: nested public entry performed work\n", file, line);
            abort();
        }
    }
}

static void r26_reset(uint32_t cfg, bool explicit_mode)
{
    reset();
    r26_explicit_active = explicit_mode;
    r26_cfg0_assertions = 0;
    r26_nested_io = 0;
    r26_nested_fired = false;
    r26_nested_initialize = false;
    *word(LTS1_QUP_CFG) = cfg;
}

static void r26_run(void)
{
    if (r26_explicit_active) lithium_touch_stage1_initialize();
    else lithium_touch_stage1_probe();
}

static void r26_both_inert(void)
{
    unsigned char saved_mock[sizeof(mock)], saved_lts1[sizeof(lts1)];
    memcpy(saved_mock, &mock, sizeof(mock));
    memcpy(saved_lts1, &lts1, sizeof(lts1));
    lithium_touch_stage1_initialize();
    lithium_touch_stage1_probe();
    lithium_touch_stage1_initialize();
    lithium_touch_stage1_probe();
    assert(memcmp(saved_mock, &mock, sizeof(mock)) == 0);
    assert(memcmp(saved_lts1, &lts1, sizeof(lts1)) == 0);
}

static void r26_trace_safety(void)
{
    unsigned writes_seen = 0;
    unsigned previous_slot = 0;
    uint32_t last[4] = {0};
    bool have_last[4] = {false};
    for (unsigned t = 0; t < mock.trace_count; ++t) {
        const uint32_t physical = mock.trace[t].physical;
        if (mock.trace[t].kind == 'R') {
            for (unsigned i = 0; i < 4; ++i)
                if (physical == write_words[i]) {
                    last[i] = mock.trace[t].value;
                    have_last[i] = true;
                }
        }
        if (mock.trace[t].kind != 'W') continue;
        assert(writes_seen < 4);
        unsigned slot = 4;
        for (unsigned i = 0; i < 4; ++i)
            if (physical == write_words[i]) slot = i;
        assert(slot < 4 && have_last[slot]);
        const uint32_t mask = mask_for(physical);
        assert(mask != 0);
        assert((mock.trace[t].value & ~mask) == (last[slot] & ~mask));
        assert((mock.trace[t].value & mask) == desired_for(physical));
        if (writes_seen) assert(slot > previous_slot);
        previous_slot = slot;
        /* Exact membership also excludes CMD/CFG, all M/N/D/PLL words, both
         * reset registers, GPIO89, IRQ words, QUP control/FIFO and rails. */
        assert(physical != LTS1_QUP_CMD && physical != LTS1_QUP_CFG);
        assert(physical != LTS1_BLSP_BCR && physical != LTS1_QUP_BCR);
        ++writes_seen;
    }
    assert(writes_seen == mock.writes);
    assert(mock.barriers == 2 * (mock.reads + mock.writes));
    assert(lts1.reads == mock.reads && lts1.writes == mock.writes);
    assert(mock.reads + mock.writes <= LTS1_ACCESS_MAX && mock.maps <= 8);
}

static void r26_stopped(const char *reason, bool before_qup)
{
    stopped(reason, before_qup);
    r26_trace_safety();
    r26_both_inert();
}

static void r26_complete(void)
{
    complete();
    r26_trace_safety();
    unsigned r = 0, m = 0, w = 0;
    for (unsigned i = 0; i < mock.trace_count; ++i) {
        if (mock.trace[i].kind == 'R') {
            assert(r < sizeof(reads) / sizeof(reads[0]));
            assert(mock.trace[i].physical == reads[r++]);
        }
        if (mock.trace[i].kind == 'M') {
            assert(m < sizeof(pages) / sizeof(pages[0]));
            assert(mock.trace[i].physical == pages[m++]);
        }
        if (mock.trace[i].kind == 'W') {
            while (w < 4 && write_words[w] != mock.trace[i].physical) ++w;
            assert(w < 4);
            ++w;
        }
    }
    assert(r == sizeof(reads) / sizeof(reads[0]) && m == 8);
    assert((*word(LTS1_QUP_CFG) & UINT32_C(0x371f)) <= 1);
    assert((*word(LTS1_QUP_CMD) & UINT32_C(0x800000f1)) == 0);
    if ((*word(LTS1_QUP_CFG) & UINT32_C(0x371f)) == 0)
        assert(r26_explicit_active && r26_cfg0_assertions == 3);
    else assert(r26_cfg0_assertions == 0);
    if (r26_explicit_active) {
        char expected[180];
        const uint32_t cfg = mock.trace[5].value;
        /* Trace entries 0/2 are mappings; initial CFG is the fourth read. */
        assert(mock.trace[5].kind == 'R' && mock.trace[5].physical == LTS1_QUP_CFG);
        snprintf(expected, sizeof(expected),
            "clock_plan cfg=0x%08x source=XO hid=%u "
            "handoff=vendor_equivalent_0_or_1 rcg_write=0",
            cfg, cfg & UINT32_C(0x1f));
        assert(strstr(mock.log, expected) != NULL);
        assert(strstr(mock.log, "rate_hz=unmeasured owner=unverified") != NULL);
    } else assert(strstr(mock.log, "clock_plan") == NULL);
    r26_both_inert();
}

/* Poll tests deliberately add repeated reads, so use the same completion
 * assertions without requiring the no-poll success trace shape. */
static void r26_polled_complete(void)
{
    complete();
    r26_trace_safety();
    assert((*word(LTS1_QUP_CMD) & UINT32_C(0x800000f1)) == 0);
    assert((*word(LTS1_QUP_CFG) & UINT32_C(0x371f)) <= 1);
    if ((*word(LTS1_QUP_CFG) & UINT32_C(0x371f)) == 0)
        assert(r26_cfg0_assertions == 3);
    r26_both_inert();
}

static void r26_test_r25_input(void)
{
    for (unsigned explicit_mode = 0; explicit_mode < 2; ++explicit_mode) {
        r26_reset(0, explicit_mode != 0);
        /* R25 observed CFG=0 and ROOT_OFF on the initial CMD read. A later
         * returning CMD=0 models branch demand, not a hardware conclusion. */
        mock.override.physical = LTS1_QUP_CMD;
        mock.override.mask = mock.override.value = UINT32_C(0x80000000);
        mock.override.first = mock.override.last = 1;
        r26_run();
        assert(mock.trace[4].kind == 'R');
        assert(mock.trace[4].physical == LTS1_QUP_CMD);
        assert(mock.trace[4].value == UINT32_C(0x80000000));
        assert(mock.trace[5].kind == 'R' && mock.trace[5].value == 0);
        if (explicit_mode) {
            r26_complete();
            assert(mock.reads == 26 && mock.writes == 4 && mock.maps == 8);
            assert(hits(LTS1_QUP_CMD) == 3);
            r26_clock_checks = mock.times;
        } else {
            r26_stopped("inherited_root_or_reset_unverified", true);
            assert(mock.reads == 4 && mock.writes == 0 && mock.maps == 2);
            assert(strstr(mock.log, "clock_plan") == NULL);
        }
    }
}

static void r26_test_cfg_domain(void)
{
    for (unsigned explicit_mode = 0; explicit_mode < 2; ++explicit_mode) {
        for (unsigned source = 0; source < 8; ++source) {
            for (unsigned mode = 0; mode < 4; ++mode) {
                for (unsigned divider = 0; divider < 32; ++divider) {
                    const uint32_t cfg = source << 8 | mode << 12 | divider;
                    const bool known = source == 0 && mode == 0 &&
                        (divider == 1 || (explicit_mode && divider == 0));
                    r26_reset(cfg, explicit_mode != 0);
                    assert(lts1_root_known(cfg, explicit_mode != 0) == known);
                    r26_run();
                    if (known) r26_complete();
                    else {
                        r26_stopped("inherited_root_or_reset_unverified", true);
                        assert(mock.reads == 4 && mock.writes == 0 && mock.maps == 2);
                        assert(!lts1.clocks_ready && !lts1.pins_ready);
                        assert(strstr(mock.log, "clock_plan") == NULL);
                    }
                    assert(*word(LTS1_QUP_CFG) == cfg);
                }
            }
        }
    }
}

static void r26_test_masks(void)
{
    for (unsigned cfg = 0; cfg < 2; ++cfg) {
        for (unsigned seed = 0; seed < 33; ++seed) {
            const uint32_t raw_cfg = cfg |
                (UINT32_C(0x9e3779b9) * seed & ~UINT32_C(0x371f));
            r26_reset(raw_cfg, true);
            for (unsigned i = 0; i < 4; ++i) {
                const uint32_t noise = seed ? UINT32_C(0x9e3779b9) * seed + i : 0;
                const uint32_t mask = mask_for(write_words[i]);
                const uint32_t reserved = write_words[i] == LTS1_QUP_CBCR ?
                    noise & ~UINT32_C(0xd0000000) : noise;
                *word(write_words[i]) = (reserved & ~mask) |
                    (seed == 0 ? desired_for(write_words[i]) :
                     mask & ~desired_for(write_words[i]));
            }
            r26_run();
            r26_complete();
            assert(mock.writes == (seed ? 4u : 0u));
            assert(*word(LTS1_QUP_CFG) == raw_cfg);
        }
    }
}

static void r26_test_initial_context(void)
{
    for (unsigned cfg = 0; cfg < 2; ++cfg) {
        for (unsigned context = 0; context < 6; ++context) {
            r26_reset(cfg, true);
            if (context == 0) mock.interrupt_context = 1;
            if (context == 1) mock.interrupts_enabled = 0;
            if (context == 2) mock.affinity = 0;
            if (context == 3) mock.affinity = 2;
            if (context == 4) mock.max_cpu = -1;
            if (context == 5) mock.max_cpu = 1;
            r26_run();
            r26_stopped("execution_context", true);
            assert(mock.maps == 0 && mock.reads == 0 && mock.writes == 0);
        }
        const uint32_t address[] = {LTS1_BLSP_BCR, LTS1_QUP_BCR, LTS1_QUP_CMD};
        const uint32_t mask[] = {1, 1, 0xf1};
        for (unsigned reg = 0; reg < 3; ++reg) {
            for (unsigned bit = 0; bit < 32; ++bit) {
                if (!(mask[reg] & (UINT32_C(1) << bit))) continue;
                r26_reset(cfg, true);
                *word(address[reg]) |= UINT32_C(1) << bit;
                r26_run();
                r26_stopped("inherited_root_or_reset_unverified", true);
                assert(mock.reads == 4 && mock.writes == 0 && mock.maps == 2);
                assert(strstr(mock.log, "clock_plan") == NULL);
            }
        }
    }
}

static void r26_test_polls_and_dropped_writes(void)
{
    for (unsigned cfg = 0; cfg < 2; ++cfg) {
        for (unsigned i = 0; i < 4; ++i) {
            r26_reset(cfg, true);
            mock.dropped_write = write_words[i];
            r26_run();
            r26_stopped("field_readback", true);
            assert(mock.writes == i + 1);
        }
        const uint32_t address[] = {LTS1_AHB_HALT, LTS1_QUP_CBCR, LTS1_QUP_CMD};
        const unsigned previous[] = {0, 2, 1};
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned busy = LTS1_POLL_MAX - 1; busy <= LTS1_POLL_MAX; ++busy) {
                r26_reset(cfg, true);
                mock.override.physical = address[i];
                mock.override.mask = mock.override.value = UINT32_C(0x80000000);
                mock.override.first = previous[i] + 1;
                mock.override.last = previous[i] + busy;
                r26_run();
                if (busy == LTS1_POLL_MAX - 1) r26_polled_complete();
                else r26_stopped("clock_poll_cap", true);
                assert(mock.delays == busy);
                assert(hits(address[i]) == previous[i] + LTS1_POLL_MAX +
                    (unsigned)(i == 2 && busy == LTS1_POLL_MAX - 1));
            }
        }
        r26_reset(cfg, true);
        *word(LTS1_QUP_CMD) = UINT32_C(0x80000000);
        r26_run();
        r26_stopped("clock_poll_cap", true);
        assert(hits(LTS1_QUP_CMD) == LTS1_POLL_MAX + 1);
        assert(mock.writes == 2 && mock.delays == LTS1_POLL_MAX);
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (!(UINT32_C(0xf1) & (UINT32_C(1) << bit))) continue;
            r26_reset(cfg, true);
            mock.override.physical = LTS1_QUP_CMD;
            mock.override.mask = mock.override.value = UINT32_C(1) << bit;
            mock.override.first = 2;
            mock.override.last = UINT32_MAX;
            r26_run();
            r26_stopped("clock_poll_cap", true);
            assert(mock.writes == 2 && mock.delays == LTS1_POLL_MAX);
        }
        for (unsigned branch = 0; branch < 2; ++branch) {
            for (unsigned nibble = 0; nibble < 16; ++nibble) {
                r26_reset(cfg, true);
                *word(address[branch]) = (*word(address[branch]) &
                    ~UINT32_C(0xf0000000)) | (uint32_t)nibble << 28;
                r26_run();
                if (nibble == 0 || nibble == 2) r26_complete();
                else {
                    r26_stopped("clock_poll_cap", true);
                    assert(mock.delays == LTS1_POLL_MAX);
                }
            }
        }
    }
}

static void r26_test_late_faults(void)
{
    for (unsigned cfg = 0; cfg < 2; ++cfg) {
        const uint32_t address[] = {LTS1_BLSP_BCR, LTS1_QUP_BCR, LTS1_QUP_CMD};
        const uint32_t mask[] = {1, 1, 0x800000f1};
        for (unsigned reg = 0; reg < 3; ++reg) {
            for (unsigned bit = 0; bit < 32; ++bit) {
                const uint32_t flag = UINT32_C(1) << bit;
                if (!(mask[reg] & flag)) continue;
                r26_reset(cfg, true);
                mock.override.physical = address[reg];
                mock.override.mask = mock.override.value = flag;
                mock.override.first = reg == 2 ? 3 : 2;
                mock.override.last = UINT32_MAX;
                r26_run();
                r26_stopped("root_or_reset_changed", true);
                assert(mock.writes == 2 && !lts1.clocks_ready && !lts1.pins_ready);
                assert(hits(LTS1_PIN87) == 0 && hits(LTS1_PIN88) == 0);
            }
        }
        for (unsigned source = 0; source < 8; ++source) {
            for (unsigned mode = 0; mode < 4; ++mode) {
                for (unsigned divider = 0; divider < 32; ++divider) {
                    const uint32_t changed = source << 8 | mode << 12 | divider;
                    if (changed <= 1) continue;
                    r26_reset(cfg, true);
                    mock.override.physical = LTS1_QUP_CFG;
                    mock.override.mask = UINT32_C(0x371f);
                    mock.override.value = changed;
                    mock.override.first = 2;
                    mock.override.last = UINT32_MAX;
                    r26_run();
                    r26_stopped("root_or_reset_changed", true);
                    assert(mock.writes == 2 && hits(LTS1_PIN87) == 0);
                }
            }
        }
        /* Both encodings are recognizable at entry, but branch enable must
         * preserve the exact inherited masked CFG, including HID 0 versus 1. */
        r26_reset(cfg, true);
        mock.override.physical = LTS1_QUP_CFG;
        mock.override.mask = UINT32_C(0x371f);
        mock.override.value = cfg ^ 1u;
        mock.override.first = 2;
        mock.override.last = UINT32_MAX;
        r26_run();
        r26_stopped("root_or_reset_changed", true);
        assert(mock.writes == 2 && !lts1.clocks_ready && !lts1.pins_ready);
        assert(hits(LTS1_PIN87) == 0 && hits(LTS1_PIN88) == 0);
        for (unsigned i = 0; i < 4; ++i) {
            r26_reset(cfg, true);
            mock.override.physical = write_words[i];
            mock.override.mask = mask_for(write_words[i]);
            mock.override.value = mock.override.mask & ~desired_for(write_words[i]);
            mock.override.first = 2;
            mock.override.last = UINT32_MAX;
            r26_run();
            r26_stopped("field_readback", true);
            assert(mock.writes == i + 1);
        }
    }
}

static void r26_test_maps_and_time(void)
{
    for (unsigned cfg = 0; cfg < 2; ++cfg) {
        for (unsigned map = 1; map <= 8; ++map) {
            r26_reset(cfg, true);
            mock.fail_map = map;
            r26_run();
            r26_stopped("mapping_failed", map <= 6);
            assert(mock.maps == map && lts1.mappings == map - 1);
        }
        for (unsigned kind = 0; kind < 2; ++kind) {
            for (unsigned call = 2; call <= r26_clock_checks; ++call) {
                r26_reset(cfg, true);
                mock.fail_time = call;
                mock.fail_ns = kind ? initial_ns - 1000 :
                    initial_ns + LTS1_BUDGET_US * 1000;
                r26_run();
                assert(mock.failure_fired);
                r26_stopped(kind ? "clock_rollback" : "software_deadline", false);
            }
        }
        const char operations[] = {'M', 'R', 'W'};
        const unsigned count[] = {8, sizeof(reads) / sizeof(reads[0]), 4};
        for (unsigned op = 0; op < 3; ++op) {
            for (unsigned index = 1; index <= count[op]; ++index) {
                r26_reset(cfg, true);
                mock.delay_kind = operations[op];
                mock.delay_index = index;
                mock.delay_ns = LTS1_BUDGET_US * 1000;
                r26_run();
                assert(mock.failure_fired);
                r26_stopped("software_deadline", false);
            }
        }
        r26_reset(cfg, true);
        mock.delay_kind = 'M';
        mock.delay_index = 1;
        mock.delay_ns = LTS1_BUDGET_US * 1000 - 1;
        r26_run();
        r26_complete();
        assert(lts1.last_us - lts1.start_us == LTS1_BUDGET_US - 1);
        for (unsigned phase = 0; phase < 3; ++phase) {
            r26_reset(cfg, true);
            mock.override.physical = phase == 0 ? LTS1_AHB_HALT :
                phase == 1 ? LTS1_QUP_CBCR : LTS1_QUP_CMD;
            mock.override.mask = mock.override.value = UINT32_C(0x80000000);
            mock.override.first = phase == 0 ? 1 : phase == 1 ? 3 : 2;
            mock.override.last = UINT32_MAX;
            mock.delay_kind = 'D';
            mock.delay_index = 1;
            mock.delay_ns = LTS1_BUDGET_US * 1000;
            r26_run();
            r26_stopped("software_deadline", true);
            assert(mock.delays == 1);
        }
    }
}

static void r26_test_reentry(void)
{
    const unsigned io_count = sizeof(reads) / sizeof(reads[0]) + 4;
    for (unsigned cfg = 0; cfg < 2; ++cfg) {
        for (unsigned io = 1; io <= io_count; ++io) {
            r26_reset(cfg, true);
            mock.reenter_io = io; /* old fixture recursively calls probe() */
            r26_run();
            r26_complete();
            assert(mock.reentered && mock.reads == 26 && mock.writes == 4);
            assert(mock.times == r26_clock_checks);
        }
        for (unsigned nested_init = 0; nested_init < 2; ++nested_init) {
            for (unsigned io = 1; io <= io_count; ++io) {
                r26_reset(cfg, true);
                r26_nested_io = io;
                r26_nested_initialize = nested_init != 0;
                r26_run();
                r26_complete();
                assert(r26_nested_fired && mock.reads == 26 && mock.writes == 4);
                assert(mock.times == r26_clock_checks);
            }
        }
    }
    for (unsigned io = 1; io <= io_count; ++io) {
        r26_reset(1, false);
        r26_nested_io = io;
        r26_nested_initialize = true;
        r26_run();
        r26_complete();
        assert(r26_nested_fired && strstr(mock.log, "clock_plan") == NULL);
        assert(mock.times == r26_clock_checks);
    }
}

static void r26_group(const char *name, void (*test)(void))
{
    const unsigned before = scenarios;
    test();
    printf("PASS touch clock init group=%s scenarios=%u\n", name, scenarios - before);
}

int main(void)
{
    assert(retained_stage1_main() == 0);
    assert(scenarios == 351);
    assert(!r26_explicit_active && r26_cfg0_assertions == 0);
    r26_group("r25_real_cfg0_root_off_and_legacy_gate", r26_test_r25_input);
    r26_group("exhaustive_initial_src_mode_div_and_legacy", r26_test_cfg_domain);
    r26_group("reserved_bits_four_write_masks_order_idempotence", r26_test_masks);
    r26_group("initial_context_reset_cmd", r26_test_initial_context);
    r26_group("dropped_write_poll_caps_branch_states", r26_test_polls_and_dropped_writes);
    r26_group("late_reset_cmd_cfg_field_faults", r26_test_late_faults);
    r26_group("mapping_time_deadline_rollback_returning_accesses", r26_test_maps_and_time);
    r26_group("cross_entry_reentry_and_shared_atomic_one_shot", r26_test_reentry);
    printf("PASS touch clock init R26: retained=351 additional=%u total=%u; "
        "exact production include; no CMD/CFG/PLL/MND/reset writes; "
        "host mocks only; hardware unverified\n", scenarios - 351, scenarios);
    return 0;
}
