/* SPDX-License-Identifier: GPL-2.0-only */
/* Focused host tests for the real header, no device or QEMU needed.
 * From the repository root:
 * clang -std=c11 -Wall -Wextra -Werror -O1 -g \
 *   -fsanitize=address,undefined -fno-omit-frame-pointer \
 *   maintenance/hakios-lithium/peripherals/touch-live-qup-test.c \
 *   -o /tmp/touch-live-qup-test && /tmp/touch-live-qup-test
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../../../ports/hakios-lithium/peripherals/touch-live-qup.h"

enum fault {
    NONE, NACK, ARBITRATION, BUS_ERROR, CORE_ERROR, DONE_TIMEOUT,
    RX_TIMEOUT, STOP_TIMEOUT, RX_ERROR, RESET_TIMEOUT, STATE_TIMEOUT
};

struct mock {
    uint32_t regs[0x410 / 4];
    struct { uint32_t offset, value; } writes[64];
    unsigned nwrites, nreads, starts, resets, state_writes, runs, rx_words;
    unsigned data_bytes, stop_polls, delay_calls, stall_state_write;
    uint64_t time;
    int freeze, backwards, cleanup_stall;
    enum fault fault;
};

static unsigned cases;

static uint8_t
payload(unsigned n)
{
    return (uint8_t)(0xe3u + n * 37u);
}

static uint32_t
mock_read(void *cookie, uint32_t off)
{
    struct mock *m = cookie;
    assert(off < 0x410 && !(off & 3u));
    ++m->nreads;
    if (off == 4) {
        if ((m->fault == RESET_TIMEOUT && !m->state_writes) ||
            (m->fault == STATE_TIMEOUT &&
             m->state_writes == m->stall_state_write) ||
            (m->cleanup_stall && m->resets >= 2))
            return 0;
        return 0x11cu | m->regs[off / 4];
    }
    if (off == 0x404 && m->runs >= 2) {
        if (m->fault == NACK)
            return 0x0d1300c8; /* retain NACK + FAIL(3) + INVALID_READ_ADDR */
        if (m->fault == ARBITRATION)
            return 0x0c110010;
        if (m->fault == BUS_ERROR)
            return 0x0c110004;
        if (m->fault == RX_ERROR && m->rx_words)
            return 0x0c110080; /* late error before caller data commit */
        if (m->rx_words == (m->data_bytes + 5u) / 4u) {
            ++m->stop_polls;
            if (m->fault != STOP_TIMEOUT && m->stop_polls >= 3)
                return 0x0c110000;
        }
        return 0x0c110300;
    }
    if (off == 0x18 && m->runs >= 2) {
        if (m->fault == NACK || m->fault == BUS_ERROR ||
            m->fault == ARBITRATION)
            return 0x10;
        if (m->fault == DONE_TIMEOUT)
            return 0x20; /* data visible alone is not input completion */
        if (m->fault == RX_TIMEOUT && m->rx_words)
            return 0x800;
        /* Producer cannot finish until the consumer drains the FIFO.
         * In particular, len=63 means 65 stream bytes > 64 FIFO bytes. */
        return ((m->rx_words == (m->data_bytes + 5u) / 4u) ? 0x800 : 0) |
            ((m->rx_words < (m->data_bytes + 5u) / 4u) ? 0x20 : 0);
    }
    if (off == 0x1c && m->runs >= 2 && m->fault == CORE_ERROR)
        return 0x2c;
    if (off == 0x218) {
        uint32_t word = 0;
        unsigned base = m->rx_words * 4u, j;
        assert(m->runs == 2);
        assert(m->rx_words < (m->data_bytes + 5u) / 4u);
        for (j = 0; j < 4; ++j) {
            unsigned index = base + j;
            /* Deliberately arbitrary overhead; not interpreted as data. */
            uint8_t byte = index < 2 ? (uint8_t)(0x90 + index) :
                payload(index - 2);
            word |= (uint32_t)byte << (8u * j);
        }
        ++m->rx_words;
        return word;
    }
    return m->regs[off / 4];
}

static void
mock_write(void *cookie, uint32_t off, uint32_t value)
{
    struct mock *m = cookie;
    assert(off < 0x410 && !(off & 3u));
    assert(m->nwrites < sizeof(m->writes) / sizeof(m->writes[0]));
    m->writes[m->nwrites].offset = off;
    m->writes[m->nwrites++].value = value;
    if (off == 0xc) {
        assert(value == 1);
        ++m->starts;
        m->regs[4 / 4] = 0;
        return;
    }
    if (off == 4) {
        ++m->state_writes;
        if (value == 1)
            ++m->runs;
        if (!value) {
            ++m->resets;
            m->runs = 0; /* cleanup erases live error registers */
        }
    }
    if (off == 0x208) {
        assert(value >= 3 && value <= 65);
        m->data_bytes = value - 2;
    }
    if (off == 0x110)
        assert(m->regs[4 / 4] == 3); /* FIFO writes only during PAUSE */
    m->regs[off / 4] = value;
}

static uint64_t
mock_now(void *cookie)
{
    struct mock *m = cookie;
    return m->time;
}

static void
mock_delay(void *cookie, uint32_t us)
{
    struct mock *m = cookie;
    ++m->delay_calls;
    if (!m->freeze)
        m->time += m->backwards ? -(uint64_t)us : us;
}

static struct ltlq_io
setup(struct mock *m)
{
    memset(m, 0, sizeof(*m));
    m->regs[0x8 / 4] = 0x1fca5; /* old mode/shift bits must be cleared */
    return (struct ltlq_io){m, mock_read, mock_write, mock_now, mock_delay};
}

static void
check_order(const struct mock *m, uint8_t reg, unsigned len)
{
    const uint32_t offsets[] = {
        0xc, 4, 0, 0x20, 0x28, 0x408, 0x200, 0x100, 0x208, 0x150,
        8, 4, 0x400, 4, 0x110, 0x110, 0x110, 4, 4
    };
    uint32_t values[] = {
        1, 0, 0x207, 0x3c, 0x300, 1, 0, 0, len + 2, 9,
        0xc0a5, 1, 0x003e007c, 3, 0x01827081,
        0x87718100u | reg, len, 1, 0
    };
    unsigned i;
    assert(m->nwrites == sizeof(offsets) / sizeof(offsets[0]));
    for (i = 0; i < m->nwrites; ++i) {
        assert(m->writes[i].offset == offsets[i]);
        assert(m->writes[i].value == values[i]);
    }
}

static void
success_cases(void)
{
    unsigned len, i;
    for (len = 1; len <= 63; ++len) {
        struct mock m;
        struct ltlq_io io = setup(&m);
        struct ltlq_result r;
        uint8_t out[65];
        memset(out, 0x5a, sizeof(out));
        /* Also exercise natural clock rollover on an actual delayed STOP. */
        if (len == 63)
            m.time = UINT64_MAX - 4;
        assert(ltlq_read_register(&io, 0xa3, out + 1, len, &r) == 0);
        assert(r.error == 0 && r.status == 0x0c110000 && r.core == 0);
        assert(m.starts == 1 && m.resets == 2);
        assert(m.rx_words == (len + 5) / 4);
        /* Completion sampling consumes the first bus-active observation;
         * STOP then sees one active poll followed by idle. */
        assert(m.delay_calls == 1 && r.polls > 0);
        assert(out[0] == 0x5a && out[len + 1] == 0x5a);
        for (i = 0; i < len; ++i)
            assert(out[i + 1] == payload(i));
        check_order(&m, 0xa3, len);
        ++cases;
    }
    puts("PASS: lengths 1..63, aggregate tags/counts/order, drain-before-done, wrap");
}

static void
invalid_cases(void)
{
    struct mock m;
    struct ltlq_io io = setup(&m), bad;
    struct ltlq_result r;
    uint8_t out[64];
    unsigned invalid[] = {0, 64, 65, UINT_MAX}, i;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(ltlq_read_register(&io, 0, out, invalid[i], &r) == LTLQ_EINVAL);
        assert(r.error == LTLQ_EINVAL && !r.polls);
        ++cases;
    }
    assert(ltlq_read_register(NULL, 0, out, 1, &r) == LTLQ_EINVAL);
    assert(ltlq_read_register(&io, 0, NULL, 1, &r) == LTLQ_EINVAL);
    assert(ltlq_read_register(&io, 0, out, 1, NULL) == LTLQ_EINVAL);
    for (i = 0; i < 4; ++i) {
        bad = io;
        if (i == 0) bad.read = NULL;
        if (i == 1) bad.write = NULL;
        if (i == 2) bad.now_us = NULL;
        if (i == 3) bad.delay_us = NULL;
        assert(ltlq_read_register(&bad, 0, out, 1, &r) == LTLQ_EINVAL);
    }
    assert(!m.nreads && !m.nwrites);
    cases += 7;
    puts("PASS: invalid lengths/pointers/callbacks cause no MMIO");
}

static void
failure_cases(void)
{
    const struct {
        enum fault fault;
        int error;
        uint32_t status, core;
    } failures[] = {
        {NACK, LTLQ_ENACK, 0x0d1300c8, 0},
        {ARBITRATION, LTLQ_EARBITRATION, 0x0c110010, 0},
        {BUS_ERROR, LTLQ_EIO, 0x0c110004, 0},
        {CORE_ERROR, LTLQ_EIO, 0x0c110300, 0x2c},
        {DONE_TIMEOUT, LTLQ_ETIMEOUT, 0x0c110000, 0},
        {RX_TIMEOUT, LTLQ_ETIMEOUT, 0x0c110300, 0},
        {STOP_TIMEOUT, LTLQ_ETIMEOUT, 0x0c110300, 0},
        {RX_ERROR, LTLQ_EIO, 0x0c110080, 0}
    };
    unsigned i, j;
    for (i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        struct mock m;
        struct ltlq_io io = setup(&m);
        struct ltlq_result r;
        uint8_t out[12];
        memset(out, 0xcc, sizeof(out));
        m.fault = failures[i].fault;
        assert(ltlq_read_register(&io, 0x02, out, sizeof(out), &r) ==
               failures[i].error);
        assert(r.error == failures[i].error && r.status == failures[i].status);
        assert(r.core == failures[i].core);
        assert(m.starts == 1 && m.resets == 2 && m.runs == 0);
        assert(m.nwrites == 19); /* exactly one final/cleanup RESET, no retry */
        for (j = 0; j < sizeof(out); ++j)
            assert(out[j] == 0xcc);
        if (m.fault == NACK) {
            assert(r.op == 0x10 && m.rx_words == 0);
            assert((r.status & 0x010000c8u) == 0x010000c8u);
        }
        ++cases;
    }
    puts("PASS: NACK/co-errors, arbitration, core/bus errors, all transfer waits");
}

static void
state_and_clock_cases(void)
{
    unsigned i;
    for (i = 0; i < 6; ++i) {
        struct mock m;
        struct ltlq_io io = setup(&m);
        struct ltlq_result r;
        uint8_t out = 0x5a;
        m.fault = i == 0 ? RESET_TIMEOUT : STATE_TIMEOUT;
        m.stall_state_write = i;
        assert(ltlq_read_register(&io, 0xa3, &out, 1, &r) == LTLQ_ETIMEOUT);
        assert(out == 0x5a && r.error == LTLQ_ETIMEOUT);
        assert(m.starts == 1);
        /* A failed terminal RESET is not attempted a second time. */
        if (i == 5)
            assert(m.state_writes == 5 && m.resets == 2);
        ++cases;
    }
    for (i = 0; i < 2; ++i) {
        struct mock m;
        struct ltlq_io io = setup(&m);
        struct ltlq_result r;
        uint8_t out = 0x5a;
        m.fault = DONE_TIMEOUT;
        m.freeze = i == 0;
        m.backwards = i == 1;
        assert(ltlq_read_register(&io, 0, &out, 1, &r) == LTLQ_ETIMEOUT);
        assert(out == 0x5a && m.starts == 1 && m.resets == 2);
        assert(r.polls < 14000);
        ++cases;
    }
    {
        struct mock m;
        struct ltlq_io io = setup(&m);
        struct ltlq_result r;
        uint8_t out = 0x5a;
        m.fault = NACK;
        m.cleanup_stall = 1;
        assert(ltlq_read_register(&io, 0, &out, 1, &r) == LTLQ_ENACK);
        assert(r.status == 0x0d1300c8 && r.op == 0x10);
        assert(out == 0x5a && m.resets == 2 && m.starts == 1);
        ++cases;
    }
    puts("PASS: every STATE transition, stalled/backwards time, cleanup failure");
}

int
main(void)
{
    success_cases();
    invalid_cases();
    failure_cases();
    state_and_clock_cases();
    printf("PASS: touch-live-qup (%u focused cases)\n", cases);
    return 0;
}
