/* SPDX-License-Identifier: GPL-2.0-only */
#include "qup-transfer.h"

#include <assert.h>
#include <stdio.h>

struct fake {
    uint32_t regs[0x40cu / 4u];
    uint32_t tx[16], rx[16];
    size_t tx_count, rx_count, rx_pos, calls, fail_at, reset_count;
    size_t time_calls, delays, state_reads, fifo_writes, invalid_reads;
    uint64_t now;
    uint32_t done_flags, inject_bus, inject_core, clear_bus, clear_core;
    uint32_t capacity_mode, reject_state;
    bool active, freeze_time, invalid_state, no_master, stuck_bus;
    bool fail_cleanup, fail_reset_write, backward_time, active_bus_stuck;
};

static bool
fake_read(void *cookie, uint32_t offset, uint32_t *value)
{
    struct fake *f = cookie;
    assert(!(offset & 3u) && offset <= LQT_MASTER_GEN);
    if (++f->calls == f->fail_at) return false;
    if (offset == LQT_STATE) {
        ++f->state_reads;
        *value = f->regs[offset / 4u];
        if (f->invalid_state || (f->fail_cleanup && f->reset_count > 1u)) *value = 0;
        if (f->invalid_reads) { --f->invalid_reads; *value = 0; }
    } else if (offset == LQT_IO_MODE) *value = f->capacity_mode;
    else if (offset == LQT_I2C_STATUS) {
        *value = (f->stuck_bus || (f->active && f->active_bus_stuck)) ? LQT_BUS_ACTIVE : 0;
        if (f->active) *value |= f->inject_bus;
    } else if (offset == LQT_ERROR_FLAGS) *value = f->active ? f->inject_core : 0;
    else if (offset == LQT_OPERATIONAL) *value = f->active ? f->done_flags : 0;
    else if (offset == LQT_IN_FIFO) {
        assert(f->active && f->rx_pos < f->rx_count);
        *value = f->rx[f->rx_pos++];
    } else *value = f->regs[offset / 4u];
    return true;
}

static bool
fake_write(void *cookie, uint32_t offset, uint32_t value)
{
    struct fake *f = cookie;
    assert(!(offset & 3u) && offset <= LQT_MASTER_GEN);
    if (++f->calls == f->fail_at) return false;
    if (offset == LQT_SW_RESET) {
        assert(value == 1);
        ++f->reset_count;
        if (f->fail_reset_write && f->reset_count > 1u) return false;
        f->regs[LQT_STATE / 4u] = LQT_STATE_VALID;
        f->active = false;
    } else if (offset == LQT_STATE) {
        assert(value == 0 || value == 1 || value == 3);
        assert(f->regs[LQT_STATE / 4u] & LQT_STATE_VALID);
        if (f->reject_state == value + 1u) return true;
        f->regs[offset / 4u] = LQT_STATE_VALID | LQT_MASTER | value;
        if (value == 1 && f->tx_count) f->active = true;
        if (!value) {
            ++f->reset_count;
            f->active = false;
        }
    } else if (offset == LQT_MASTER_GEN) {
        assert(value == 1);
        if (!f->no_master) f->regs[LQT_STATE / 4u] |= LQT_MASTER;
    } else if (offset == LQT_OUT_FIFO) {
        assert((f->regs[LQT_STATE / 4u] & 3u) == 3u);
        assert(f->tx_count < 16u);
        f->tx[f->tx_count++] = value;
        ++f->fifo_writes;
    } else if (offset == LQT_I2C_STATUS) {
        assert(value && !(value & ~LQT_BUS_ERRORS));
        f->clear_bus |= value;
        f->inject_bus &= ~value;
    } else if (offset == LQT_ERROR_FLAGS) {
        assert(value && !(value & ~LQT_CORE_ERRORS));
        f->clear_core |= value;
        f->inject_core &= ~value;
    } else if (offset == LQT_OPERATIONAL) {
        assert(value == LQT_OUT_SERVICE || value == LQT_IN_SERVICE);
        f->done_flags &= ~value;
    } else f->regs[offset / 4u] = value;
    return true;
}

static uint64_t
fake_time(void *cookie)
{
    struct fake *f = cookie;
    ++f->time_calls;
    return f->now;
}

static void
fake_delay(void *cookie, uint32_t us)
{
    struct fake *f = cookie;
    assert(us == 1);
    ++f->delays;
    if (!f->freeze_time) f->now = f->backward_time ? f->now - us : f->now + us;
}

static struct lqt_io
make_io(struct fake *f)
{
    struct lqt_io io = {f, fake_read, fake_write, fake_time, fake_delay,
        LQT_ALL_VERIFIED, 0x35d, 100, 8};
    memset(f, 0, sizeof(*f));
    f->capacity_mode = 1u | (1u << 2) | (1u << 5) | (1u << 7); /* 64 bytes */
    f->done_flags = LQT_OUT_SERVICE | LQT_OUT_DONE | LQT_IN_SERVICE | LQT_IN_DONE;
    return io;
}

static void
prepare_rx(struct fake *f, size_t len)
{
    uint8_t wire[LQT_MAX_BYTES + 4u] = {0x87, 0};
    wire[1] = (uint8_t)len;
    f->rx_count = (len + 5u) / 4u;
    for (size_t i = 0; i < len; ++i) wire[i + 2u] = (uint8_t)(i + 0xa0u);
    for (size_t i = 0; i < len + 2u; ++i)
        f->rx[i / 4u] |= (uint32_t)wire[i] << ((i % 4u) * 8u);
}

static void
assert_untouched(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) assert(data[i] == 0x5a);
}

static void
test_packing(void)
{
    const uint8_t payload[5] = {0x00, 0x7f, 0x80, 0xff, 0x42};
    uint32_t words[10], before[10];
    size_t count = 99;
    memset(words, 0x5a, sizeof(words));
    memcpy(before, words, sizeof(words));
    assert(!lqt_pack(0x38, false, payload, 5, words, 2, &count));
    assert(count == 99 && !memcmp(words, before, sizeof(words)));
    assert(!lqt_pack(0x80, false, payload, 5, words, 10, &count));
    assert(!lqt_pack(0x38, false, NULL, 1, words, 10, &count));
    assert(!lqt_pack(0x38, true, NULL, SIZE_MAX, words, 10, &count));
    assert(!lqt_pack(0x38, true, NULL, 0, words, 10, &count));
    assert(!lqt_pack(0x38, true, NULL, 1, NULL, 10, &count));
    assert(!lqt_pack(0x38, true, NULL, 1, words, 10, NULL));
    assert(!memcmp(words, before, sizeof(words)) && count == 99);
    assert(lqt_pack(0x38, false, payload, 5, words, 10, &count));
    assert(count == 3 && words[0] == 0x05837081u);
    assert(words[1] == 0xff807f00u && words[2] == 0x42u);
    assert(words[3] == before[3]);
    assert(lqt_pack(0x38, true, NULL, 32, words, 10, &count));
    assert(count == 1 && words[0] == 0x20877181u);
}

static void
test_gate_and_validation(void)
{
    struct fake f;
    struct lqt_io io = make_io(&f);
    uint8_t data[33] = {0};
    for (uint32_t bit = 1; bit <= 128; bit <<= 1) {
        io.verified = LQT_ALL_VERIFIED ^ bit;
        assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_PLATFORM_UNVERIFIED);
        {
            const struct lqt_budget b = {&io, 0};
            struct lqt_outcome r = {LQT_OK, LQT_OK, 0, 0};
            assert(lqt_change_state(&b, 1) == LQT_PLATFORM_UNVERIFIED);
            assert(lqt_wait_idle(&b, &r) == LQT_PLATFORM_UNVERIFIED);
            assert(lqt_check_errors(&io, &r, NULL) == LQT_PLATFORM_UNVERIFIED);
        }
        assert(!f.calls && !f.time_calls && !f.delays);
    }
    io.verified = LQT_ALL_VERIFIED;
    {
        const struct lqt_budget b = {&io, 0};
        assert(lqt_change_state(&b, 2) == LQT_INVALID);
    }
    assert(lqt_transfer(NULL, 0, true, data, 1).status == LQT_INVALID);
    assert(lqt_transfer(&io, 0x80, true, data, 1).status == LQT_INVALID);
    assert(lqt_transfer(&io, 0x38, true, NULL, 1).status == LQT_INVALID);
    assert(lqt_transfer(&io, 0x38, true, data, 0).status == LQT_INVALID);
    assert(lqt_transfer(&io, 0x38, true, data, 33).status == LQT_INVALID);
    io.max_polls = 257;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.max_polls = 0;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.max_polls = 8; io.timeout_us = 0;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.timeout_us = 1000001;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.timeout_us = 100; io.read32 = NULL;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.read32 = fake_read; io.write32 = NULL;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.write32 = fake_write; io.time_us = NULL;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    io.time_us = fake_time; io.delay_us = NULL;
    assert(lqt_transfer(&io, 0x38, true, data, 1).status == LQT_INVALID);
    assert(!f.calls && !f.time_calls && !f.delays);
}

static void
test_lengths_and_canaries(void)
{
    for (size_t len = 1; len <= LQT_MAX_BYTES; ++len) {
        for (unsigned read = 0; read < 2; ++read) {
            struct fake f;
            struct lqt_io io = make_io(&f);
            uint8_t data[LQT_MAX_BYTES + 2u];
            struct lqt_outcome r;
            memset(data, 0x5a, sizeof(data));
            prepare_rx(&f, len);
            r = lqt_transfer(&io, 0x38, read != 0, data + 1, len);
            assert(r.status == LQT_OK && r.cleanup == LQT_OK);
            assert(data[0] == 0x5a && data[len + 1] == 0x5a);
            for (size_t i = 0; i < len; ++i)
                assert(data[i + 1] == (read ? (uint8_t)(0xa0u + i) : 0x5a));
            assert(f.tx_count == (read ? 1u : (len + 7u) / 4u));
            assert(f.tx[0] == ((uint32_t)len << 24 | (read ? 0x877181u : 0x837081u)));
            assert(f.regs[LQT_MX_WRITE_CNT / 4u] == (read ? 4u : len + 4u));
            assert(f.regs[LQT_MX_READ_CNT / 4u] == (read ? len + 2u : 0u));
            assert(f.regs[LQT_CLK_CTL / 4u] == io.clk_ctl);
            assert(f.reset_count == 2 && !f.active && f.calls < 100);
        }
    }
}

static void
test_faults_and_bounds(void)
{
    /* Every MMIO failure in a complete read, including cleanup, must suppress
     * output commit. One-shot fault injection also exercises bounded recovery. */
    struct lqt_io io;
    uint8_t data[32];
    for (unsigned read = 0; read < 2; ++read) {
        struct fake base;
        size_t len = read ? 9u : 32u;
        size_t calls;
        io = make_io(&base); prepare_rx(&base, len);
        base.done_flags = read ? LQT_IN_SERVICE : LQT_OUT_SERVICE;
        memset(data, 0x5a, sizeof(data));
        assert(lqt_transfer(&io, 0x38, read != 0, data, len).status == LQT_OK);
        calls = base.calls;
        for (size_t fail = 1; fail <= calls; ++fail) {
            struct fake f;
            struct lqt_outcome r;
            io = make_io(&f); prepare_rx(&f, len); f.fail_at = fail;
            f.done_flags = read ? LQT_IN_SERVICE : LQT_OUT_SERVICE;
            memset(data, 0x5a, sizeof(data));
            r = lqt_transfer(&io, 0x38, read != 0, data, len);
            assert(r.status != LQT_OK || r.cleanup != LQT_OK);
            assert_untouched(data, sizeof(data));
            assert(f.calls <= calls + 4u && f.delays <= 16u);
        }
        printf("PASS injected fault at all %zu MMIO calls (%s)\n", calls, read ? "read" : "write");
    }
    for (unsigned scenario = 0; scenario < 13; ++scenario) {
        struct fake f;
        struct lqt_outcome r;
        enum lqt_status want = LQT_TRANSFER_TIMEOUT;
        io = make_io(&f); prepare_rx(&f, 9);
        memset(data, 0x5a, sizeof(data));
        switch (scenario) {
        case 0: f.inject_bus = LQT_NACK; want = LQT_NACK_ERROR; break;
        case 1: f.inject_core = 0x10; want = LQT_CONTROLLER_ERROR; break;
        case 2: f.done_flags = 0; break;
        case 3: f.done_flags = 0; f.freeze_time = true; break;
        case 4: f.stuck_bus = true; want = LQT_BUS_STUCK; break;
        case 5: f.invalid_state = true; want = LQT_STATE_TIMEOUT; break;
        case 6: f.no_master = true; want = LQT_STATE_TIMEOUT; break;
        case 7: f.reject_state = 2; want = LQT_STATE_TIMEOUT; break;
        case 8: f.reject_state = 4; want = LQT_STATE_TIMEOUT; break;
        case 9: f.done_flags = LQT_OUT_SERVICE | LQT_OUT_DONE; break; /* no RX */
        case 10: f.done_flags = LQT_IN_DONE; break; /* DONE without RX service */
        case 11: f.inject_bus = 0x80; want = LQT_CONTROLLER_ERROR; break;
        case 12: f.active_bus_stuck = true; want = LQT_BUS_STUCK; break;
        }
        r = lqt_transfer(&io, 0x38, true, data, 9);
        assert(r.status == want);
        assert_untouched(data, sizeof(data));
        assert(f.calls < 120 && f.delays <= 24u);
        assert(f.reset_count == 2);
        assert(r.cleanup == (scenario == 5 ? LQT_STATE_TIMEOUT : LQT_OK));
        if (scenario == 0) assert(r.bus_error == LQT_NACK && f.clear_bus == LQT_NACK);
        if (scenario == 1) assert(r.core_error == 0x10 && f.clear_core == 0x10);
        if (scenario >= 4 && scenario <= 8) assert(!f.fifo_writes);
    }
}

static void
test_clock_capacity_cleanup(void)
{
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        struct fake f;
        struct lqt_io io = make_io(&f);
        struct lqt_outcome r;
        uint8_t data[32];
        memset(data, 0x5a, sizeof(data));
        prepare_rx(&f, 9);
        switch (scenario) {
        case 0: f.capacity_mode = 0; break; /* 8-byte FIFO: RX requires 11 */
        case 1: f.capacity_mode = 3; break; /* reserved output block encoding */
        case 2: f.capacity_mode = 3u << 5; break;
        case 3: f.done_flags = 0; io.timeout_us = 2; break;
        case 4: f.done_flags = 0; f.now = UINT64_MAX - 1u; io.timeout_us = 3; break;
        case 5: f.done_flags = 0; f.backward_time = true; f.now = 10; break;
        case 6: f.inject_bus = LQT_NACK; f.fail_cleanup = true; break;
        case 7: f.inject_bus = LQT_NACK; f.fail_reset_write = true; break;
        case 8: f.fail_cleanup = true; break; /* complete read; reset unconfirmed */
        }
        r = lqt_transfer(&io, 0x38, true, data, 9);
        assert_untouched(data, sizeof(data));
        assert(f.calls < 120 && f.delays <= 24u);
        if (scenario < 3) {
            assert(r.status == LQT_FIFO_TOO_SMALL && !f.fifo_writes);
        } else if (scenario < 6) assert(r.status == LQT_TRANSFER_TIMEOUT);
        else if (scenario < 8) {
            assert(r.status == LQT_NACK_ERROR);
            assert(r.cleanup == (scenario == 6 ? LQT_STATE_TIMEOUT : LQT_MMIO_ERROR));
        } else assert(r.status == LQT_CLEANUP_FAILED && r.cleanup == LQT_STATE_TIMEOUT);
    }
}

static void
test_small_fifo_and_delayed_state(void)
{
    /* Upstream FIFO service completion must work even when DONE flags lag. */
    for (unsigned read = 0; read < 2; ++read) {
        struct fake f;
        struct lqt_io io = make_io(&f);
        uint8_t data = 0x5a;
        prepare_rx(&f, 1);
        f.done_flags = read ? LQT_IN_SERVICE : LQT_OUT_SERVICE;
        assert(lqt_transfer(&io, 0x38, read != 0, &data, 1).status == LQT_OK);
        assert(data == (read ? 0xa0 : 0x5a));
    }
    for (unsigned read = 0; read < 2; ++read) {
        for (unsigned over = 0; over < 2; ++over) {
            struct fake f;
            struct lqt_io io = make_io(&f);
            uint8_t data[8];
            size_t len = (read ? 6u : 4u) + over;
            struct lqt_outcome r;
            memset(data, 0x5a, sizeof(data));
            f.capacity_mode = 0;
            prepare_rx(&f, len);
            r = lqt_transfer(&io, 0x38, read != 0, data, len);
            assert(r.status == (over ? LQT_FIFO_TOO_SMALL : LQT_OK));
            if (over) assert_untouched(data, sizeof(data));
        }
    }
    for (unsigned limit = 1; limit <= 256; limit *= 256) {
        struct fake f;
        struct lqt_io io = make_io(&f);
        uint8_t data = 0x5a;
        io.max_polls = limit; f.invalid_reads = limit - 1u; io.timeout_us = 1000;
        assert(lqt_transfer(&io, 0x38, false, &data, 1).status == LQT_OK);
        assert(f.delays == limit - 1u && data == 0x5a);
    }
}

int main(void)
{
    test_packing();
    test_gate_and_validation();
    test_lengths_and_canaries();
    test_faults_and_bounds();
    test_clock_capacity_cleanup();
    test_small_fifo_and_delayed_state();
    puts("PASS qup-transfer: packing, 64 transfers, gates, every MMIO fault, bounded errors/cleanup");
    return 0;
}
