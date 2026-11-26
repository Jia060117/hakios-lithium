#!/usr/bin/env python3
"""One-command, host-only ASan/UBSan tests for the isolated return contract.

Windows: python scripts/test-hakios-return-fastboot-contract.py
Linux:   python3 scripts/test-hakios-return-fastboot-contract.py
Windows delegates only to WSL Ubuntu user jia, clang; no device tooling.
All generated source, objects, executables and evidence stay under
evidence/fastboot-return-impl-20261120/contract-fixes/. No Git writes are performed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zlib
from datetime import datetime, timezone


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/fastboot-return-impl-20261120/contract-fixes"
HEADER = ROOT / "ports/hakios-lithium/return-fastboot/contract.h"

C_TESTS = r'''
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ports/hakios-lithium/return-fastboot/contract.h"

static unsigned long checks, parser_fuzz_cases, wire_fuzz_cases, policy_cases;
static char wire_fixtures[2][RFBT_WIRE_SIZE * 2u + 1u];
static const char *suite = "startup";
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d %s: %s\n", __FILE__, __LINE__, suite, #x); \
    exit(1); } } while (0)
#define EQ(a,b) CHECK((a) == (b))
#define COOKIE UINT64_C(0x8f01020304050607)
#define TARGET UINT64_C(0x100000012)

static uint32_t parse(const char *s, struct rfbt_config *c)
{ return rfbt_parse_args(s, strlen(s), 1, c); }

static uint32_t eligible(const struct rfbt_record *r, const struct rfbt_config *c)
{ return rfbt_eligible(r, c, COOKIE, RFBT_RETURN_REASON, 0); }

static void make_ready(struct rfbt_record *r, struct rfbt_config *c, uint32_t storage)
{
    EQ(parse("return_fastboot=1", c), RFBT_OK);
    EQ(rfbt_record_init(r, c, COOKIE), RFBT_OK);
    EQ(rfbt_kernel_finish(r, RFBT_REASON_NORMAL_TIMER, storage,
                         TARGET, RFBT_DRAIN_OK, TARGET), RFBT_OK);
    EQ(rfbt_tail_finish(r, TARGET, RFBT_DRAIN_OK, TARGET), RFBT_OK);
}

static void repair_crc(uint8_t *wire)
{
    uint32_t crc = rfbt_crc32(wire, 112);
    rfbt_put32(wire + 112, crc);
    rfbt_put32(wire + 116, ~crc);
}

static uint32_t rng_state = UINT32_C(0x20f66a31);
static uint32_t random32(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void parser_tables(void)
{
    static const struct { const char *s; uint32_t on, delay; } valid[] = {
        {"", 0, 10}, {" \t\r\n\v\f", 0, 10}, {"ufs=0 bootcpu=1 cpus=1", 0, 10},
        {"return_fastboot=0", 0, 10}, {"return_fastboot=1", 1, 10},
        {"return_fastboot_delay=0", 0, 0}, {"return_fastboot_delay=60", 0, 60},
        {"return_fastboot=1 return_fastboot_delay=0", 1, 0},
        {"return_fastboot_delay=60\treturn_fastboot=1", 1, 60},
        {"return_fastboot_delay=00 return_fastboot=1", 1, 0},
        {"return_fastboot_delay=09 return_fastboot=0", 0, 9},
        {"xreturn_fastboot=1", 0, 10}, {"foo=return_fastboot=1", 0, 10},
        {"return_fastboo=1", 0, 10}, {"Return_fastboot=1", 0, 10},
        {"ufs=0\nreturn_fastboot=1\r\ncpus=1\freturn_fastboot_delay=10\v", 1, 10}
    };
    static const char *bad[] = {
        "return_fastboot", "return_fastboot=", "return_fastboot=2",
        "return_fastboot=01", "return_fastboot=+1", "return_fastboot=-0",
        "return_fastboot=true", "return_fastboot=0x1", "return_fastboot=1junk",
        "return_fastboot=1=0", "return_fastboot =1", "return_fastboot= 1",
        "return_fastboot_delay", "return_fastboot_delay=", "return_fastboot_delay=61",
        "return_fastboot_delay=-1", "return_fastboot_delay=+1", "return_fastboot_delay=1.0",
        "return_fastboot_delay=999999999999999999999999999999999999999999999999999",
        "return_fastboot_delay=000", "return_fastboot_delay=0x3c",
        "return_fastboot=1 return_fastboot=1", "return_fastboot=1 return_fastboot=0",
        "return_fastboot=0 return_fastboot=1", "return_fastboot=0 return_fastboot=0",
        "return_fastboot_delay=0 return_fastboot_delay=0",
        "return_fastboot_delay=60 return_fastboot_delay=1",
        "return_fastboot=1 return_fastboot_delaye=1", "return_fastboot_extra=1",
        "return_fastboot=1 return_fastboot_delay=10;foo=1", "return_fastboot=1;",
        "return_fastboot='1'", "return_fastboot=\"1\"",
        "return_fastboot=1 \"return_fastboot=0\"",
        "return_fastboot=1 'return_fastboot=0'",
        "return_fastboot=1 return_fastboot_delay=\"10\"",
        "return_fastboot=1\x7f", "return_fastboot=1\x1b", "return_fastboot=1\xc2\xa0",
        "return_fastboot=1 \xe3\x80\x80"
    };
    struct rfbt_config c;
    size_t i;
    char b[96];
    suite = "parser tables/bounds/default off";
    for (i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
        EQ(parse(valid[i].s, &c), RFBT_OK);
        EQ(c.enabled, valid[i].on); EQ(c.delay_seconds, valid[i].delay);
        EQ(rfbt_config_check(&c), RFBT_OK);
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint32_t e = parse(bad[i], &c);
        CHECK(e != RFBT_OK); EQ(c.enabled, 0u); EQ(c.delay_seconds, 10u);
        EQ(c.error, e); EQ(c.seen, 0u); EQ(rfbt_config_check(&c), RFBT_E_CONFIG);
    }
    for (i = 0; i < 100; ++i) {
        int n = snprintf(b, sizeof b, "return_fastboot=1 return_fastboot_delay=%zu", i);
        uint32_t e = rfbt_parse_args(b, (size_t)n, 1, &c);
        EQ(e, i <= 60 ? RFBT_OK : RFBT_E_VALUE);
        EQ(c.enabled, i <= 60 ? 1u : 0u);
        if (e == RFBT_OK) EQ(c.delay_seconds, (uint32_t)i);
    }
    EQ(rfbt_parse_args(NULL, 0, 1, &c), RFBT_OK);
    EQ(rfbt_parse_args(NULL, 1, 1, &c), RFBT_E_ARGUMENT);
    EQ(rfbt_parse_args(NULL, RFBT_ARGS_MAX + 1u, 1, &c), RFBT_E_ARGS_TOO_LONG);
    EQ(rfbt_parse_args("return_fastboot=1", 16, 0, &c), RFBT_E_ARGS_TRUNCATED);
    EQ(c.enabled, 0u);
    EQ(rfbt_parse_args("", 0, 2, &c), RFBT_E_ARGS_TRUNCATED);
    EQ(rfbt_parse_args("", 0, -1, &c), RFBT_E_ARGS_TRUNCATED);
    EQ(rfbt_parse_args("", 0, 1, NULL), RFBT_E_ARGUMENT);
    {
        const char nul[] = "return_fastboot=1\0return_fastboot=0";
        EQ(rfbt_parse_args(nul, sizeof nul - 1u, 1, &c), RFBT_E_TOKEN);
        EQ(c.enabled, 0u);
    }
    /* Every cut prefix explicitly marked incomplete must fail, even at a digit. */
    for (i = 0; i <= strlen("return_fastboot=1 return_fastboot_delay=60"); ++i) {
        EQ(rfbt_parse_args("return_fastboot=1 return_fastboot_delay=60", i, 0, &c),
           RFBT_E_ARGS_TRUNCATED);
        EQ(c.enabled, 0u);
    }
    rfbt_config_default(&c); c.enabled = 1;
    EQ(rfbt_config_check(&c), RFBT_E_CONFIG);
    rfbt_config_default(&c); c.delay_seconds = 0;
    EQ(rfbt_config_check(&c), RFBT_E_CONFIG);
    rfbt_config_default(&c); c.seen = UINT32_MAX;
    EQ(rfbt_config_check(&c), RFBT_E_CONFIG);
    rfbt_config_default(&c); c.error = 1;
    EQ(rfbt_config_check(&c), RFBT_E_CONFIG);
    EQ(rfbt_config_check(NULL), RFBT_E_CONFIG);
}

static void parser_fuzz(void)
{
    char b[201];
    struct rfbt_config c;
    size_t i, j;
    suite = "deterministic hostile parser fuzz";
    for (i = 0; i < 40000; ++i) {
        size_t n = random32() % sizeof b;
        int complete = (int)(random32() % 3u);
        uint32_t e;
        for (j = 0; j < n; ++j) b[j] = (char)random32();
        if (n >= 16u && i % 3u == 0) memcpy(b, "return_fastboot=1", 16);
        e = rfbt_parse_args(b, n, complete, &c);
        if (e == RFBT_OK) {
            EQ(complete, 1); EQ(rfbt_config_check(&c), RFBT_OK);
            CHECK(c.enabled <= 1u); CHECK(c.delay_seconds <= 60u);
            if (c.enabled != 0) CHECK((c.seen & RFBT_SEEN_SWITCH) != 0);
        } else {
            EQ(c.enabled, 0u); EQ(c.delay_seconds, 10u); EQ(c.error, e); EQ(c.seen, 0u);
        }
        ++parser_fuzz_cases;
    }
}

static void protected_bounds(void)
{
    long page = sysconf(_SC_PAGESIZE);
    uint8_t *mem, *edge;
    size_t n;
    struct rfbt_config c;
    struct rfbt_record r;
    suite = "guard pages and every truncated length";
    CHECK(page >= (long)RFBT_ARGS_MAX);
    mem = mmap(NULL, (size_t)page * 2u, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mem != MAP_FAILED);
    EQ(mprotect(mem + page, (size_t)page, PROT_NONE), 0);
    edge = mem + page;
    for (n = 0; n <= RFBT_ARGS_MAX; ++n) {
        if (n != 0) edge[-(ptrdiff_t)n] = ' ';
        EQ(rfbt_parse_args((const char *)(edge - n), n, 1, &c), RFBT_OK);
        EQ(c.enabled, 0u);
    }
    for (n = 0; n < RFBT_WIRE_SIZE; ++n) {
        EQ(rfbt_decode(edge - n, n, &r), RFBT_E_WIRE_LENGTH);
        EQ(rfbt_encode(&r, edge - n, n), RFBT_E_WIRE_LENGTH);
    }
    make_ready(&r, &c, RFBT_STORAGE_RAM_NONE);
    EQ(rfbt_encode(&r, edge - RFBT_WIRE_SIZE, RFBT_WIRE_SIZE), RFBT_OK);
    EQ(rfbt_decode(edge - RFBT_WIRE_SIZE, RFBT_WIRE_SIZE, &r), RFBT_OK);
    EQ(munmap(mem, (size_t)page * 2u), 0);
}

static void initial_cookie_binding(void)
{
    static const uint8_t template[RFBT_WIRE_SIZE] = RFBT_WIRE_INITIALIZER;
    uint8_t w[RFBT_WIRE_SIZE], saved[RFBT_WIRE_SIZE];
    struct rfbt_config c;
    struct rfbt_record r;
    uint64_t cookie = UINT64_MAX;
    size_t i, bit;
    suite = "loader-seeded INITIAL cookie shared with XNU";
    memcpy(w, template, sizeof w);
    EQ(rfbt_initial_cookie(w, sizeof w, &cookie), RFBT_E_COOKIE);
    EQ(cookie, 0u);
    EQ(rfbt_seed_initial(w, sizeof w, 0), RFBT_E_COOKIE);
    EQ(memcmp(w, template, sizeof w), 0);
    EQ(rfbt_seed_initial(w, sizeof w, COOKIE), RFBT_OK);
    EQ(rfbt_get64(w + 48), COOKIE);
    EQ(rfbt_get64(w + 120), 0u);
    EQ(rfbt_initial_cookie(w, sizeof w, &cookie), RFBT_OK);
    EQ(cookie, COOKIE);
    memcpy(saved, w, sizeof w);
    EQ(rfbt_seed_initial(w, sizeof w, COOKIE + 1u), RFBT_E_COOKIE);
    EQ(memcmp(w, saved, sizeof w), 0);
    EQ(rfbt_decode(w, sizeof w, &r), RFBT_E_COMMIT);
    EQ(parse("return_fastboot=1", &c), RFBT_OK);
    EQ(rfbt_record_init(&r, &c, cookie), RFBT_OK);
    EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
    EQ(rfbt_initial_cookie(w, sizeof w, &cookie), RFBT_E_STATE);
    EQ(cookie, 0u);
    for (i = 0; i < sizeof w; ++i) {
        EQ(rfbt_seed_initial(w, i, COOKIE), RFBT_E_WIRE_LENGTH);
        for (bit = 0; bit < 8; ++bit) {
            memcpy(w, template, sizeof w);
            w[i] ^= (uint8_t)(1u << bit);
            memcpy(saved, w, sizeof w);
            CHECK(rfbt_seed_initial(w, sizeof w, COOKIE) != RFBT_OK);
            EQ(memcmp(w, saved, sizeof w), 0);
        }
    }
    EQ(rfbt_initial_cookie(NULL, 0, &cookie), RFBT_E_ARGUMENT);
    EQ(rfbt_initial_cookie(w, sizeof w, NULL), RFBT_E_ARGUMENT);
}

static void wire_format(void)
{
    static uint8_t initial[RFBT_WIRE_SIZE]
        __attribute__((aligned(RFBT_RECORD_ALIGNMENT))) = RFBT_WIRE_INITIALIZER;
    struct rfbt_record r, decoded;
    struct rfbt_config c;
    uint8_t buffer[RFBT_WIRE_SIZE + 2u], valid[RFBT_WIRE_SIZE], other[RFBT_WIRE_SIZE];
    uint8_t *w = buffer + 1;
    size_t i, j;
    suite = "wire layout CRC little endian alignment and tearing";
    EQ(sizeof(struct rfbt_record), 72u);
    EQ(sizeof(struct rfbt_once), 80u);
    EQ((uintptr_t)initial % RFBT_RECORD_ALIGNMENT, 0u);
    EQ(rfbt_get64(initial), RFBT_MAGIC);
    EQ(rfbt_get32(initial + 8), RFBT_VERSION);
    EQ(rfbt_get32(initial + 12), RFBT_WIRE_SIZE);
    EQ(rfbt_get32(initial + 20), RFBT_PHASE_INITIAL);
    EQ(rfbt_decode(initial, sizeof initial, &decoded), RFBT_E_COMMIT);
    EQ(rfbt_crc32((const uint8_t *)"123456789", 9), UINT32_C(0xcbf43926));
    make_ready(&r, &c, RFBT_STORAGE_UFS_SYNC_OK);
    memset(buffer, 0xa5, sizeof buffer);
    EQ(rfbt_encode(&r, w, RFBT_WIRE_SIZE), RFBT_OK);
    EQ(buffer[0], 0xa5); EQ(buffer[sizeof buffer - 1u], 0xa5);
    EQ(memcmp(w, "HKFBRET1", 8), 0);
    EQ(rfbt_get64(w + 48), COOKIE); EQ(rfbt_get64(w + 64), TARGET);
    EQ(rfbt_get64(w + 120), RFBT_COMMIT);
    EQ(rfbt_decode(w, RFBT_WIRE_SIZE, &decoded), RFBT_OK);
    EQ(eligible(&decoded, &c), RFBT_OK);
    EQ(rfbt_encode(&decoded, valid, sizeof valid), RFBT_OK);
    EQ(memcmp(w, valid, sizeof valid), 0);
    for (i = 0; i < sizeof valid; ++i) {
        for (j = 0; j < 8; ++j) {
            memcpy(w, valid, sizeof valid); w[i] ^= (uint8_t)(1u << j);
            CHECK(rfbt_decode(w, RFBT_WIRE_SIZE, &decoded) != RFBT_OK);
        }
    }
    /* Any prefix copied into a zeroed full-size record remains ineligible. */
    for (i = 0; i < sizeof valid; ++i) {
        memset(w, 0, sizeof valid); memcpy(w, valid, i);
        CHECK(rfbt_decode(w, RFBT_WIRE_SIZE, &decoded) != RFBT_OK);
    }
    /* Mix a previous boot with this boot at every byte boundary. */
    r.boot_cookie ^= 0x102u;
    EQ(rfbt_encode(&r, other, sizeof other), RFBT_OK);
    for (i = 1; i < sizeof valid; ++i) {
        uint32_t e;
        memcpy(w, valid, i); memcpy(w + i, other + i, sizeof valid - i);
        e = rfbt_decode(w, RFBT_WIRE_SIZE, &decoded);
        /* Identical regions can yield a whole old/new image; context still binds. */
        if (e == RFBT_OK) {
            CHECK(memcmp(w, valid, sizeof valid) == 0 ||
                  memcmp(w, other, sizeof valid) == 0);
            if (decoded.boot_cookie != COOKIE) EQ(eligible(&decoded, &c), RFBT_E_COOKIE);
        }
    }
    EQ(rfbt_decode(NULL, 0, &decoded), RFBT_E_ARGUMENT);
    EQ(rfbt_decode(valid, sizeof valid, NULL), RFBT_E_ARGUMENT);
    EQ(rfbt_decode(valid, sizeof valid + 1u, &decoded), RFBT_E_WIRE_LENGTH);
    EQ(rfbt_encode(&r, NULL, 0), RFBT_E_ARGUMENT);
    memcpy(w, valid, sizeof valid); r.flags |= UINT32_C(0x80000000);
    EQ(rfbt_encode(&r, w, RFBT_WIRE_SIZE), RFBT_E_FLAGS);
    EQ(rfbt_get64(w + 120), 0u); /* Do not leave an older READY commit behind. */
}

static void semantic_wire(void)
{
    static const struct { size_t offset; uint64_t value; unsigned int width; uint32_t e; } bad[] = {
        {0, 0, 8, RFBT_E_MAGIC}, {8, 0, 4, RFBT_E_VERSION},
        {8, 2, 4, RFBT_E_VERSION}, {8, UINT32_MAX, 4, RFBT_E_VERSION},
        {12, 64, 4, RFBT_E_SIZE}, {12, 129, 4, RFBT_E_SIZE},
        {16, UINT32_MAX, 4, RFBT_E_FLAGS},
        {20, 0, 4, RFBT_E_STATE}, {20, 99, 4, RFBT_E_STATE},
        {24, 0, 4, RFBT_E_FLAGS}, {24, 2, 4, RFBT_E_FLAGS},
        {28, 2, 4, RFBT_E_FLAGS}, {28, 3, 4, RFBT_E_FLAGS},
        {32, 0, 4, RFBT_E_FLAGS}, {36, 2, 4, RFBT_E_FLAGS},
        {40, 61, 4, RFBT_E_DELAY}, {40, UINT32_MAX, 4, RFBT_E_DELAY},
        {44, 1, 4, RFBT_E_STATE}, {48, 0, 8, RFBT_E_COOKIE},
        {56, TARGET + 1u, 8, RFBT_E_LOG_BOUNDS},
        {56, 0, 8, RFBT_E_LOG_BOUNDS},
        {64, TARGET + 1u, 8, RFBT_E_TAIL_DRAIN},
        {72, TARGET - 1u, 8, RFBT_E_KERNEL_DRAIN},
        {72, TARGET + 1u, 8, RFBT_E_LOG_BOUNDS},
        {80, TARGET - 1u, 8, RFBT_E_TAIL_DRAIN},
        {88, 1, 8, RFBT_E_RESERVED}, {104, UINT64_MAX, 8, RFBT_E_RESERVED},
        {120, 0, 8, RFBT_E_COMMIT}
    };
    struct rfbt_record r, decoded;
    struct rfbt_config c;
    uint8_t valid[RFBT_WIRE_SIZE], w[RFBT_WIRE_SIZE];
    size_t i;
    suite = "rechecksummed malicious fields conflicting flags";
    make_ready(&r, &c, RFBT_STORAGE_UFS_SYNC_OK);
    EQ(rfbt_encode(&r, valid, sizeof valid), RFBT_OK);
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        memcpy(w, valid, sizeof w);
        if (bad[i].width == 8u) rfbt_put64(w + bad[i].offset, bad[i].value);
        else rfbt_put32(w + bad[i].offset, (uint32_t)bad[i].value);
        repair_crc(w);
        EQ(rfbt_decode(w, sizeof w, &decoded), bad[i].e);
    }
    for (i = 0; i < 32; ++i) {
        memcpy(w, valid, sizeof w);
        rfbt_put32(w + 16, r.flags ^ (UINT32_C(1) << (unsigned int)i));
        repair_crc(w);
        CHECK(rfbt_decode(w, sizeof w, &decoded) != RFBT_OK);
    }
    /* Correcting flags does not turn a panic/checkpoint/unknown into a timer. */
    for (i = 0; i < 4; ++i) {
        r.reason = (uint32_t)i; r.flags = rfbt_observed_flags(&r);
        EQ(rfbt_record_check(&r), i == 1 ? RFBT_OK : RFBT_E_REASON);
    }
    make_ready(&r, &c, RFBT_STORAGE_RAM_NONE);
    r.flags |= RFBT_F_UFS_SYNC_OK;
    EQ(rfbt_record_check(&r), RFBT_E_FLAGS);
    r.flags = rfbt_observed_flags(&r); r.failure = 1;
    EQ(rfbt_record_check(&r), RFBT_E_STATE);
}

static void policy_matrix(void)
{
    uint32_t reason, storage, kd, ld;
    int kt, lt, ft;
    struct rfbt_record r, decoded;
    struct rfbt_config c;
    uint8_t w[RFBT_WIRE_SIZE];
    suite = "complete timer/storage/two-drain policy matrix";
    EQ(parse("return_fastboot=1", &c), RFBT_OK);
    for (reason = 0; reason <= 4; ++reason)
    for (storage = 0; storage <= 4; ++storage)
    for (kd = 0; kd <= 3; ++kd)
    for (kt = -1; kt <= 1; ++kt) {
        uint64_t tail = kt < 0 ? TARGET - 1u : TARGET + (uint64_t)kt;
        int kernel_ok = reason == 1u && (storage == 1u || storage == 2u) &&
                        kd == 1u && kt >= 0;
        uint32_t e;
        EQ(rfbt_record_init(&r, &c, COOKIE), RFBT_OK);
        e = rfbt_kernel_finish(&r, reason, storage, TARGET, kd, tail);
        EQ(e == RFBT_OK, kernel_ok);
        EQ(rfbt_record_check(&r), RFBT_OK);
        EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
        EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
        CHECK(eligible(&decoded, &c) != RFBT_OK); /* First drain alone is insufficient. */
        EQ(rfbt_kernel_finish(&r, 1, 1, TARGET, 1, TARGET), RFBT_E_CONSUMED);
        if (!kernel_ok) {
            EQ(r.phase, RFBT_PHASE_FAILED); EQ(r.failure, e);
            EQ(rfbt_tail_finish(&r, TARGET, RFBT_DRAIN_OK, TARGET), RFBT_E_CONSUMED);
        } else for (ld = 0; ld <= 3; ++ld)
        for (ft = -1; ft <= 2; ++ft)
        for (lt = -1; lt <= 3; ++lt) {
            struct rfbt_record candidate = r;
            uint64_t tail_tail = lt < 0 ? TARGET - 1u : TARGET + (uint64_t)lt;
            uint64_t final_target = ft < 0 ? TARGET - 1u : TARGET + (uint64_t)ft;
            int ok = ld == 1u && ft >= 0 && ft >= kt && lt >= ft && lt >= kt;
            uint32_t le = rfbt_tail_finish(&candidate, final_target, ld, tail_tail);
            EQ(le == RFBT_OK, ok);
            EQ(rfbt_record_check(&candidate), RFBT_OK);
            EQ(rfbt_encode(&candidate, w, sizeof w), RFBT_OK);
            EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
            EQ(eligible(&decoded, &c) == RFBT_OK, ok);
            EQ(rfbt_tail_finish(&candidate, TARGET, 1, TARGET), RFBT_E_CONSUMED);
            ++policy_cases;
        }
        ++policy_cases;
    }
    EQ(parse("return_fastboot=0", &c), RFBT_OK);
    EQ(rfbt_record_init(&r, &c, COOKIE), RFBT_OK);
    EQ(rfbt_kernel_finish(&r, 1, 1, TARGET, 1, TARGET), RFBT_E_DISABLED);
    EQ(rfbt_record_init(&r, &c, 0), RFBT_E_COOKIE);
    EQ(rfbt_record_init(NULL, &c, COOKIE), RFBT_E_ARGUMENT);
    EQ(rfbt_record_init(&r, NULL, COOKIE), RFBT_E_CONFIG);
    EQ(parse("return_fastboot=1", &c), RFBT_OK);
    EQ(rfbt_record_init(&r, &c, COOKIE), RFBT_OK);
    EQ(rfbt_kernel_finish(&r, 1, 1, 0, 1, 0), RFBT_E_LOG_BOUNDS);
    EQ(rfbt_record_init(&r, &c, COOKIE), RFBT_OK);
    EQ(rfbt_kernel_finish(&r, 1, 1, UINT64_MAX, 1, UINT64_MAX), RFBT_OK);
    EQ(rfbt_tail_finish(&r, 0, 1, 0), RFBT_E_LOG_BOUNDS);
}

static void cross_drain_bounds(void)
{
    static const uint64_t finals[] = {1500, 1999, 2000, 2001, UINT64_MAX};
    struct rfbt_record primary, r, decoded;
    struct rfbt_config c;
    struct rfbt_once once;
    uint8_t w[RFBT_WIRE_SIZE], saved[RFBT_WIRE_SIZE];
    size_t i;
    uint32_t phase;
    suite = "final target captured after primary delivered tail";
    EQ(parse("return_fastboot=1", &c), RFBT_OK);
    EQ(rfbt_record_init(&primary, &c, COOKIE), RFBT_OK);
    EQ(rfbt_kernel_finish(&primary, 1, 2, 1000, 1, 2000), RFBT_OK);
    for (i = 0; i < sizeof finals / sizeof finals[0]; ++i) {
        uint32_t e = finals[i] < 2000u ? RFBT_E_LOG_BOUNDS : RFBT_OK;
        r = primary;
        EQ(rfbt_tail_finish(&r, finals[i], 1, UINT64_MAX), e);
        EQ(r.phase, e == RFBT_OK ? RFBT_PHASE_READY : RFBT_PHASE_FAILED);
        EQ(r.failure, e);
        EQ(r.final_target, finals[i]); EQ(r.kernel_tail, 2000u);
        EQ(rfbt_record_check(&r), RFBT_OK); /* Failed evidence retains raw bounds. */
    }
    r = primary;
    EQ(rfbt_tail_finish(&r, 2000, 1, 2000), RFBT_OK);
    EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
    /* Independently forge the impossible READY wire, with a fresh valid CRC. */
    rfbt_put64(w + 64, 1500); repair_crc(w); memcpy(saved, w, sizeof saved);
    EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_E_LOG_BOUNDS);
    rfbt_once_init(&once);
    EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE,
                        RFBT_RETURN_REASON, 0, 10000), RFBT_E_LOG_BOUNDS);
    EQ(once.used, 1u); EQ(once.result, RFBT_E_LOG_BOUNDS);
    EQ(memcmp(w, saved, sizeof w), 0);
    for (phase = RFBT_PHASE_READY; phase <= RFBT_PHASE_CLAIMED; ++phase) {
        r.phase = phase; r.final_target = 1500;
        if (phase == RFBT_PHASE_CLAIMED) r.flags |= RFBT_F_ATTEMPTED;
        EQ(rfbt_record_check(&r), RFBT_E_LOG_BOUNDS);
        EQ(eligible(&r, &c), RFBT_E_LOG_BOUNDS);
        EQ(rfbt_encode(&r, w, sizeof w), RFBT_E_LOG_BOUNDS);
        EQ(rfbt_get64(w + 120), 0u);
    }
    EQ(rfbt_record_init(&r, &c, COOKIE), RFBT_OK);
    EQ(rfbt_kernel_finish(&r, 1, 2, 1, 1, UINT64_MAX), RFBT_OK);
    EQ(rfbt_tail_finish(&r, UINT64_MAX, 1, UINT64_MAX), RFBT_OK);
    EQ(rfbt_record_check(&r), RFBT_OK);
}

/* These constants were frozen with Python struct/zlib, not the production codec. */
static void fixed_wire_fixtures(void)
{
    static const char *const hex[] = {
        "484b46425245543101000000800000005b000000020000000100000002000000"
        "01000000000000000a00000000000000070605040302018fe803000000000000"
        "0000000000000000ea0300000000000000000000000000000000000000000000"
        "000000000000000000000000000000006360284f9c9fd7b052464254434d4431",
        "484b4642524554310100000080000000db020000050000000100000002000000"
        "01000000020000000a00000016000000070605040302018fe803000000000000"
        "4c04000000000000ea030000000000004b040000000000000000000000000000"
        "00000000000000000000000000000000e9fa20d11605df2e52464254434d4431"
    };
    static const struct rfbt_record expected[] = {
        {0x5b, 2, 1, 2, 1, 0, 10, 0, COOKIE, 1000, 0, 1002, 0},
        {0x2db, 5, 1, 2, 1, 2, 10, 22, COOKIE, 1000, 1100, 1002, 1099}
    };
    static const char digits[] = "0123456789abcdef";
    uint8_t fixed[RFBT_WIRE_SIZE], encoded[RFBT_WIRE_SIZE];
    struct rfbt_record decoded;
    size_t i, j;
    suite = "independent fixed PRIMARY_DONE and FAILED wire encode/decode";
    for (i = 0; i < 2u; ++i) {
        EQ(strlen(hex[i]), RFBT_WIRE_SIZE * 2u);
        for (j = 0; j < sizeof fixed; ++j) {
            const char *hi = strchr(digits, hex[i][j * 2u]);
            const char *lo = strchr(digits, hex[i][j * 2u + 1u]);
            CHECK(hi != NULL && lo != NULL);
            fixed[j] = (uint8_t)((hi - digits) * 16 + (lo - digits));
        }
        EQ(rfbt_encode(&expected[i], encoded, sizeof encoded), RFBT_OK);
        EQ(memcmp(encoded, fixed, sizeof fixed), 0); /* Known record -> fixed wire. */
        EQ(rfbt_decode(fixed, sizeof fixed, &decoded), RFBT_OK); /* Known wire -> record. */
        EQ(decoded.flags, expected[i].flags); EQ(decoded.phase, expected[i].phase);
        EQ(decoded.reason, 1u); EQ(decoded.storage, 2u);
        EQ(decoded.kernel_drain, 1u); EQ(decoded.tail_drain, i == 0 ? 0u : 2u);
        EQ(decoded.delay_seconds, 10u); EQ(decoded.failure, expected[i].failure);
        EQ(decoded.boot_cookie, COOKIE); EQ(decoded.primary_target, 1000u);
        EQ(decoded.final_target, expected[i].final_target); EQ(decoded.kernel_tail, 1002u);
        EQ(decoded.tail_tail, expected[i].tail_tail);
        for (j = 0; j < sizeof encoded; ++j) {
            wire_fixtures[i][j * 2u] = digits[encoded[j] >> 4];
            wire_fixtures[i][j * 2u + 1u] = digits[encoded[j] & 15u];
        }
        wire_fixtures[i][RFBT_WIRE_SIZE * 2u] = '\0';
    }
}

static void rejected_claim(const struct rfbt_record *r, const struct rfbt_config *c,
                           uint64_t cookie, uint64_t esr, uint32_t expected)
{
    struct rfbt_once once;
    struct rfbt_record decoded;
    uint8_t w[RFBT_WIRE_SIZE], saved[RFBT_WIRE_SIZE];
    EQ(rfbt_encode(r, w, sizeof w), RFBT_OK);
    rfbt_once_init(&once);
    EQ(rfbt_loader_claim(&once, w, sizeof w, c, cookie,
                        RFBT_RETURN_REASON, esr, 10000), expected);
    EQ(once.used, 1u); EQ(once.result, expected);
    EQ(once.summary.phase, RFBT_PHASE_FAILED); EQ(once.summary.failure, expected);
    CHECK((once.summary.flags & (RFBT_F_FAILED | RFBT_F_ATTEMPTED)) == RFBT_F_FAILED);
    EQ(once.summary.boot_cookie, r->boot_cookie);
    EQ(once.summary.primary_target, r->primary_target);
    EQ(once.summary.final_target, r->final_target);
    EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
    EQ(decoded.phase, RFBT_PHASE_FAILED); EQ(decoded.failure, expected);
    EQ(decoded.flags, once.summary.flags); EQ(decoded.boot_cookie, r->boot_cookie);
    memcpy(saved, w, sizeof saved);
    EQ(rfbt_loader_claim(&once, w, sizeof w, c, COOKIE,
                        RFBT_RETURN_REASON, 0, UINT64_MAX), RFBT_E_CONSUMED);
    EQ(once.result, expected); EQ(memcmp(saved, w, sizeof w), 0);
}

static void context_and_one_shot(void)
{
    static const uint64_t bad_reasons[] = {0, 1, 2, UINT64_MAX,
        RFBT_RETURN_REASON + 1u, RFBT_RETURN_REASON | (UINT64_C(1) << 32)};
    struct rfbt_record r, decoded;
    struct rfbt_config c, disabled, invalid;
    struct rfbt_once once, next;
    uint8_t w[RFBT_WIRE_SIZE], saved[RFBT_WIRE_SIZE];
    size_t i;
    uint32_t delay;
    char args[80];
    suite = "independent x2/esr context binding and sticky consumption";
    make_ready(&r, &c, RFBT_STORAGE_RAM_NONE);
    for (i = 0; i < sizeof bad_reasons / sizeof bad_reasons[0]; ++i) {
        EQ(rfbt_eligible(&r, &c, COOKIE, bad_reasons[i], 0), RFBT_E_CTX_REASON);
        EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
        rfbt_once_init(&once);
        EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, bad_reasons[i], 0, 10000),
           RFBT_E_CTX_REASON);
        EQ(once.used, 1u); EQ(once.summary.failure, RFBT_E_CTX_REASON);
        EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
        EQ(decoded.phase, RFBT_PHASE_FAILED);
        EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, 10000),
           RFBT_E_CONSUMED);
    }
    for (i = 0; i < 64; ++i) {
        uint64_t esr = UINT64_C(1) << (unsigned int)i;
        EQ(rfbt_eligible(&r, &c, COOKIE, RFBT_RETURN_REASON, esr), RFBT_E_CTX_ESR);
        rejected_claim(&r, &c, COOKIE, esr, RFBT_E_CTX_ESR);
    }
    EQ(rfbt_eligible(&r, &c, 0, RFBT_RETURN_REASON, 0), RFBT_E_COOKIE);
    EQ(rfbt_eligible(&r, &c, COOKIE + 1u, RFBT_RETURN_REASON, 0), RFBT_E_COOKIE);
    rejected_claim(&r, &c, 0, 0, RFBT_E_COOKIE);
    rejected_claim(&r, &c, COOKIE + 1u, 0, RFBT_E_COOKIE);
    rfbt_config_default(&disabled);
    EQ(eligible(&r, &disabled), RFBT_E_DISABLED);
    rejected_claim(&r, &disabled, COOKIE, 0, RFBT_E_DISABLED);
    EQ(rfbt_parse_args("return_fastboot=1", 17, 0, &invalid), RFBT_E_ARGS_TRUNCATED);
    rejected_claim(&r, &invalid, COOKIE, 0, RFBT_E_CONFIG);
    invalid = c; invalid.seen |= RFBT_SEEN_DELAY; invalid.delay_seconds = 11;
    EQ(rfbt_config_check(&invalid), RFBT_OK); /* Valid config, wrong boot's delay. */
    rejected_claim(&r, &invalid, COOKIE, 0, RFBT_E_CONFIG);
    for (i = 0; i < 5u; ++i) {
        invalid = c;
        if (i == 0) invalid.enabled = 2;
        if (i == 1) invalid.delay_seconds = RFBT_MAX_DELAY + 1u;
        if (i == 2) invalid.seen |= UINT32_C(0x80000000);
        if (i == 3) invalid.seen = 0;
        if (i == 4) invalid.delay_seconds = 0; /* Delay without the seen bit. */
        rejected_claim(&r, &invalid, COOKIE, 0, RFBT_E_CONFIG);
    }
    for (delay = 0; delay <= 60; ++delay) {
        int n = snprintf(args, sizeof args, "return_fastboot=1 return_fastboot_delay=%u", delay);
        uint64_t ms = (uint64_t)delay * 1000u;
        EQ(rfbt_parse_args(args, (size_t)n, 1, &c), RFBT_OK);
        EQ(rfbt_record_init(&r, &c, COOKIE), RFBT_OK);
        EQ(rfbt_kernel_finish(&r, 1, 1, TARGET, 1, TARGET), RFBT_OK);
        EQ(rfbt_tail_finish(&r, TARGET, 1, TARGET), RFBT_OK);
        EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
        rfbt_once_init(&once);
        EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, ms), RFBT_OK);
        EQ(once.used, 1u); EQ(once.summary.phase, RFBT_PHASE_CLAIMED);
        EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
        EQ(decoded.phase, RFBT_PHASE_CLAIMED);
        memcpy(saved, w, sizeof w);
        EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, ms),
           RFBT_E_CONSUMED);
        EQ(once.result, RFBT_OK); EQ(memcmp(saved, w, sizeof w), 0);
        rfbt_once_init(&next);
        EQ(rfbt_loader_claim(&next, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, ms),
           RFBT_E_CONSUMED);
        EQ(memcmp(saved, w, sizeof w), 0);
        EQ(rfbt_handoff_returned(&once, w, sizeof w), RFBT_E_HANDOFF_RETURNED);
        EQ(once.result, RFBT_E_HANDOFF_RETURNED);
        EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
        EQ(decoded.failure, RFBT_E_HANDOFF_RETURNED);
        CHECK((decoded.flags & (RFBT_F_FAILED | RFBT_F_ATTEMPTED)) ==
              (RFBT_F_FAILED | RFBT_F_ATTEMPTED));
        EQ(rfbt_handoff_returned(&once, w, sizeof w), RFBT_E_CONSUMED);
        if (delay != 0) {
            EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
            rfbt_once_init(&once);
            EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, ms - 1u),
               RFBT_E_DELAY);
            EQ(once.summary.failure, RFBT_E_DELAY);
            EQ(rfbt_decode(w, sizeof w, &decoded), RFBT_OK);
            EQ(decoded.phase, RFBT_PHASE_FAILED);
        }
    }
    make_ready(&r, &c, RFBT_STORAGE_UFS_SYNC_OK);
    EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
    rfbt_once_init(&once);
    EQ(rfbt_loader_claim(&once, w, sizeof w - 1u, &c, COOKIE, RFBT_RETURN_REASON, 0, 10000),
       RFBT_E_WIRE_LENGTH);
    EQ(once.result, RFBT_E_WIRE_LENGTH);
    EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, 10000),
       RFBT_E_CONSUMED);
    rfbt_once_init(&once);
    rfbt_put32(w + 8, 2); repair_crc(w); memcpy(saved, w, sizeof w);
    EQ(rfbt_loader_claim(&once, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, 10000),
       RFBT_E_VERSION);
    EQ(memcmp(w, saved, sizeof w), 0);
    EQ(rfbt_encode(&r, w, sizeof w), RFBT_OK);
    rfbt_once_init(&once);
    EQ(rfbt_loader_claim(&once, w, sizeof w, NULL, COOKIE, RFBT_RETURN_REASON, 0, 10000),
       RFBT_E_CONFIG);
    EQ(once.summary.failure, RFBT_E_CONFIG);
    EQ(rfbt_loader_claim(NULL, w, sizeof w, &c, COOKIE, RFBT_RETURN_REASON, 0, 10000),
       RFBT_E_ARGUMENT);
    EQ(rfbt_handoff_returned(NULL, w, sizeof w), RFBT_E_ARGUMENT);
    rfbt_once_init(&once);
    EQ(rfbt_handoff_returned(&once, w, sizeof w), RFBT_E_CONSUMED);
}

static void overlapping_storage(void)
{
    union {
        struct rfbt_record record;
        struct rfbt_config config;
        struct rfbt_once once;
        uint8_t bytes[256];
    } arena;
    struct {
        uint64_t prefix[6], cookie;
        uint8_t suffix[72];
    } seed;
    static const uint8_t initial[RFBT_WIRE_SIZE] = RFBT_WIRE_INITIALIZER;
    static const size_t offsets[] = {0, 1, 64, 71};
    struct rfbt_record r;
    struct rfbt_config c;
    struct rfbt_once once, once_saved;
    uint8_t saved[sizeof arena], wire[RFBT_WIRE_SIZE], wire_saved[RFBT_WIRE_SIZE];
    uint8_t seed_saved[sizeof seed];
    size_t i;
    suite = "overlap rejected before any input output or latch mutation";
    make_ready(&r, &c, RFBT_STORAGE_RAM_NONE);
    for (i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        memset(&arena, 0xa5, sizeof arena);
        EQ(rfbt_encode(&r, arena.bytes + offsets[i], RFBT_WIRE_SIZE), RFBT_OK);
        memcpy(saved, &arena, sizeof saved);
        EQ(rfbt_decode(arena.bytes + offsets[i], RFBT_WIRE_SIZE, &arena.record),
           RFBT_E_ARGUMENT);
        EQ(memcmp(saved, &arena, sizeof saved), 0);
        arena.record = r; memcpy(saved, &arena, sizeof saved);
        EQ(rfbt_encode(&arena.record, arena.bytes + offsets[i], RFBT_WIRE_SIZE),
           RFBT_E_ARGUMENT);
        EQ(memcmp(saved, &arena, sizeof saved), 0);
    }
    /* Touching ranges are disjoint, in either address order. */
    EQ(rfbt_encode(&r, arena.bytes + sizeof r, RFBT_WIRE_SIZE), RFBT_OK);
    EQ(rfbt_decode(arena.bytes + sizeof r, RFBT_WIRE_SIZE, &arena.record), RFBT_OK);
    EQ(arena.record.boot_cookie, COOKIE);
    arena.record = r;
    EQ(rfbt_encode(&arena.record, arena.bytes + sizeof r, RFBT_WIRE_SIZE), RFBT_OK);
    CHECK(!rfbt_ranges_overlap(arena.bytes, 72, arena.bytes + 72, 128));
    CHECK(!rfbt_ranges_overlap(arena.bytes + 72, 128, arena.bytes, 72));
    CHECK(!rfbt_ranges_overlap(arena.bytes, 0, arena.bytes, 128));
    CHECK(rfbt_ranges_overlap(arena.bytes + 8, SIZE_MAX, arena.bytes + 9, 1));
    CHECK(rfbt_ranges_overlap(arena.bytes + 9, 1, arena.bytes + 8, SIZE_MAX));
    /* A typed cookie member at wire offset 48 reproduces the reviewed alias. */
    memcpy(&seed, initial, sizeof seed);
    EQ(rfbt_seed_initial(&seed, sizeof seed, COOKIE), RFBT_OK);
    memcpy(seed_saved, &seed, sizeof seed_saved);
    EQ(rfbt_initial_cookie(&seed, sizeof seed, &seed.cookie), RFBT_E_ARGUMENT);
    EQ(seed.cookie, COOKIE); EQ(memcmp(seed_saved, &seed, sizeof seed_saved), 0);
    for (i = 0; i < 2u; ++i) {
        memset(&arena, 0, sizeof arena);
        rfbt_once_init(&arena.once);
        EQ(rfbt_encode(&r, arena.bytes + 8, RFBT_WIRE_SIZE), RFBT_OK);
        if (i != 0) rfbt_put32(arena.bytes + 16, 2); /* Unknown wire version. */
        memcpy(saved, &arena, sizeof saved);
        EQ(rfbt_loader_claim(&arena.once, arena.bytes + 8, RFBT_WIRE_SIZE, &c,
                            COOKIE, RFBT_RETURN_REASON, 0, 10000), RFBT_E_ARGUMENT);
        EQ(memcmp(saved, &arena, sizeof saved), 0); EQ(arena.once.used, 0u);
    }
    rfbt_once_init(&arena.once);
    arena.once.used = 1; arena.once.result = RFBT_OK; arena.once.summary = r;
    arena.once.summary.phase = RFBT_PHASE_CLAIMED;
    arena.once.summary.flags |= RFBT_F_ATTEMPTED;
    memcpy(saved, &arena, sizeof saved);
    EQ(rfbt_handoff_returned(&arena.once, arena.bytes + 8, RFBT_WIRE_SIZE),
       RFBT_E_ARGUMENT);
    EQ(memcmp(saved, &arena, sizeof saved), 0);
    /* Config aliases wire; the otherwise separate once must also stay untouched. */
    EQ(rfbt_encode(&r, arena.bytes, RFBT_WIRE_SIZE), RFBT_OK);
    memcpy(saved, &arena, sizeof saved);
    rfbt_once_init(&once); once_saved = once;
    EQ(rfbt_loader_claim(&once, arena.bytes, RFBT_WIRE_SIZE, &arena.config,
                        COOKIE, RFBT_RETURN_REASON, 0, 10000), RFBT_E_ARGUMENT);
    EQ(memcmp(saved, &arena, sizeof saved), 0);
    EQ(memcmp(&once_saved, &once, sizeof once), 0);
    /* Config aliases once, and record_init's output aliases its config input. */
    arena.config = c; memcpy(saved, &arena, sizeof saved);
    EQ(rfbt_encode(&r, wire, sizeof wire), RFBT_OK);
    memcpy(wire_saved, wire, sizeof wire_saved);
    EQ(rfbt_loader_claim(&arena.once, wire, sizeof wire, &arena.config,
                        COOKIE, RFBT_RETURN_REASON, 0, 10000), RFBT_E_ARGUMENT);
    EQ(memcmp(saved, &arena, sizeof saved), 0);
    EQ(memcmp(wire_saved, wire, sizeof wire_saved), 0);
    EQ(rfbt_record_init(&arena.record, &arena.config, COOKIE), RFBT_E_ARGUMENT);
    EQ(memcmp(saved, &arena, sizeof saved), 0);
    memcpy(arena.bytes + 8, "return_fastboot=1", 17);
    memcpy(saved, &arena, sizeof saved);
    EQ(rfbt_parse_args((const char *)arena.bytes + 8, 17, 1, &arena.config),
       RFBT_E_ARGUMENT);
    EQ(memcmp(saved, &arena, sizeof saved), 0);
    memcpy(arena.bytes + sizeof c, "return_fastboot=1", 17);
    EQ(rfbt_parse_args((const char *)arena.bytes + sizeof c, 17, 1, &arena.config),
       RFBT_OK);
    EQ(arena.config.enabled, 1u);
}

static void wire_fuzz(void)
{
    struct rfbt_record r, decoded;
    struct rfbt_config c;
    uint8_t w[RFBT_WIRE_SIZE], valid[RFBT_WIRE_SIZE];
    size_t i, j;
    suite = "seeded wire fuzz including rechecksummed mutation";
    make_ready(&r, &c, RFBT_STORAGE_UFS_SYNC_OK);
    EQ(rfbt_encode(&r, valid, sizeof valid), RFBT_OK);
    for (i = 0; i < 50000; ++i) {
        uint32_t e;
        if (i % 3u == 0) {
            for (j = 0; j < sizeof w; ++j) w[j] = (uint8_t)random32();
        } else {
            size_t pos = random32() % sizeof w;
            memcpy(w, valid, sizeof w);
            w[pos] ^= (uint8_t)(1u << (random32() % 8u));
            if (i % 3u == 1) repair_crc(w);
        }
        e = rfbt_decode(w, sizeof w, &decoded);
        if (e == RFBT_OK) {
            EQ(rfbt_record_check(&decoded), RFBT_OK);
            if (eligible(&decoded, &c) == RFBT_OK) {
                EQ(decoded.reason, RFBT_REASON_NORMAL_TIMER);
                CHECK(decoded.storage == RFBT_STORAGE_UFS_SYNC_OK ||
                      decoded.storage == RFBT_STORAGE_RAM_NONE);
                EQ(decoded.kernel_drain, RFBT_DRAIN_OK);
                EQ(decoded.tail_drain, RFBT_DRAIN_OK);
                CHECK(decoded.kernel_tail >= decoded.primary_target);
                CHECK(decoded.tail_tail >= decoded.final_target);
                CHECK(decoded.tail_tail >= decoded.kernel_tail);
                CHECK(decoded.final_target >= decoded.primary_target);
                CHECK(decoded.final_target >= decoded.kernel_tail);
                CHECK(decoded.primary_target > 0);
                EQ(decoded.boot_cookie, COOKIE);
            }
        } else CHECK(e <= RFBT_E_CTX_ESR);
        ++wire_fuzz_cases;
    }
}

int main(void)
{
    parser_tables(); parser_fuzz(); protected_bounds(); initial_cookie_binding(); wire_format();
    semantic_wire(); policy_matrix(); cross_drain_bounds(); fixed_wire_fixtures();
    context_and_one_shot(); overlapping_storage(); wire_fuzz();
    printf("{\"checks\":%lu,\"parser_fuzz_cases\":%lu,\"wire_fuzz_cases\":%lu,"
           "\"policy_cases\":%lu,\"suites\":12,\"wire_fixtures\":[\"%s\",\"%s\"],"
           "\"status\":\"passed\"}\n",
           checks, parser_fuzz_cases, wire_fuzz_cases, policy_cases,
           wire_fixtures[0], wire_fixtures[1]);
    return 0;
}
'''

PROBE = r'''
#include "ports/hakios-lithium/return-fastboot/contract.h"
uint32_t probe_parse(const char *s, size_t n, int complete, struct rfbt_config *c)
{ return rfbt_parse_args(s, n, complete, c); }
uint32_t probe_init(struct rfbt_record *r, const struct rfbt_config *c, uint64_t cookie)
{ return rfbt_record_init(r, c, cookie); }
uint32_t probe_kernel(struct rfbt_record *r, uint32_t reason, uint32_t storage,
                      uint64_t target, uint32_t drain, uint64_t tail)
{ return rfbt_kernel_finish(r, reason, storage, target, drain, tail); }
uint32_t probe_tail(struct rfbt_record *r, uint64_t target, uint32_t drain, uint64_t tail)
{ return rfbt_tail_finish(r, target, drain, tail); }
uint32_t probe_seed(void *w, size_t n, uint64_t cookie)
{ return rfbt_seed_initial(w, n, cookie); }
uint32_t probe_cookie(const void *w, size_t n, uint64_t *cookie)
{ return rfbt_initial_cookie(w, n, cookie); }
uint32_t probe_encode(const struct rfbt_record *r, void *w, size_t n)
{ return rfbt_encode(r, w, n); }
uint32_t probe_decode(const void *w, size_t n, struct rfbt_record *r)
{ return rfbt_decode(w, n, r); }
void probe_once(struct rfbt_once *o) { rfbt_once_init(o); }
uint32_t probe_claim(struct rfbt_once *o, void *w, size_t n,
                     const struct rfbt_config *c, uint64_t cookie,
                     uint64_t reason, uint64_t esr, uint64_t waited_ms)
{ return rfbt_loader_claim(o, w, n, c, cookie, reason, esr, waited_ms); }
uint32_t probe_returned(struct rfbt_once *o, void *w, size_t n)
{ return rfbt_handoff_returned(o, w, n); }
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_wire_fixtures(hex_fixtures: list[str]) -> list[dict]:
    """Independent stdlib ABI and CRC oracle, without production byte helpers."""
    if len(hex_fixtures) != 2:
        raise RuntimeError("missing independently checked PRIMARY_DONE/FAILED fixtures")
    results = []
    for index, text in enumerate(hex_fixtures):
        wire = bytes.fromhex(text)
        expected32 = {8: 1, 12: 128, 16: 0x5b if index == 0 else 0x2db,
                      20: 2 if index == 0 else 5, 24: 1, 28: 2, 32: 1,
                      36: 0 if index == 0 else 2, 40: 10, 44: 0 if index == 0 else 22}
        expected64 = {0: 0x3154455242464b48, 48: 0x8f01020304050607,
                      56: 1000, 64: 0 if index == 0 else 1100,
                      72: 1002, 80: 0 if index == 0 else 1099,
                      120: 0x31444d4354424652}
        if len(wire) != 128:
            raise RuntimeError("independent wire length mismatch")
        for fmt, expected in (("<I", expected32), ("<Q", expected64)):
            for offset, value in expected.items():
                if struct.unpack_from(fmt, wire, offset)[0] != value:
                    raise RuntimeError(f"independent fixture {index} ABI mismatch at {offset}")
        crc = zlib.crc32(wire[:112])
        if wire[88:112] != bytes(24) or struct.unpack_from("<II", wire, 112) != (
                crc, crc ^ 0xffffffff):
            raise RuntimeError(f"independent fixture {index} reserved/CRC mismatch")
        results.append({"phase": "PRIMARY_DONE" if index == 0 else "FAILED",
                        "sha256": hashlib.sha256(wire).hexdigest(),
                        "crc32_zlib": f"{crc:08x}", "layout_crc": "passed"})
    return results


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--cc", default="clang")
    args = ap.parse_args()
    if os.name == "nt" and not args.native:
        script_path = str(Path(__file__).resolve())
        drive, tail = os.path.splitdrive(script_path)
        linux_path = "/mnt/" + drive[0].lower() + tail.replace("\\", "/")
        return subprocess.run([
            "wsl.exe", "-d", "Ubuntu", "-u", "jia", "--", "python3",
            linux_path, "--native", "--cc", args.cc,
        ], check=False).returncode
    OUT.mkdir(parents=True, exist_ok=True)
    temporary = OUT / "tmp"
    temporary.mkdir(exist_ok=True)
    for variable in ("TMPDIR", "TMP", "TEMP"):
        os.environ[variable] = str(temporary)
    source = OUT / "contract_tests.c"
    probe = OUT / "freestanding_probe.c"
    source.write_text(C_TESTS.lstrip(), encoding="utf-8")
    probe.write_text(PROBE.lstrip(), encoding="utf-8")
    report = {
        "status": "running",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "host-only pure C contract; no device operations or Git writes",
        "header_sha256": digest(HEADER),
        "test_script_sha256": digest(Path(__file__).resolve()),
        "test_source_sha256": digest(source),
        "commands": [],
        "sanitizer_runs": [],
        "freestanding": [],
        "unverified": [
            "Kernel/loader/firmware integration, cache/barrier publication and single-instance scan",
            "Truth of timer, UFS sync/flush, RAM isolation and USB drain observations",
            "Trusted current-boot cookie and monotonic post-validation delay source",
            "Actual hardware Fastboot entry, USB enumeration and host log persistence",
        ],
    }
    transcript: list[str] = []

    def run(command: list[str], *, env: dict | None = None) -> str:
        cp = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, check=False)
        report["commands"].append({"argv": command, "exit_code": cp.returncode})
        transcript.append("$ " + " ".join(command) + "\n" + cp.stdout)
        if cp.returncode:
            raise RuntimeError(f"command failed ({cp.returncode}): {' '.join(command)}\n{cp.stdout}")
        return cp.stdout

    try:
        report["compiler"] = run([args.cc, "--version"]).splitlines()[0]
        strict = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                  "-Wconversion", "-Wshadow", "-Wvla", "-I", str(ROOT)]
        san_env = os.environ.copy()
        san_env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
        san_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        for opt in ("-O1", "-O2"):
            binary = OUT / ("contract-tests-" + opt[1:])
            run([args.cc, *strict, opt, "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                 str(source), "-o", str(binary)])
            result = json.loads(run([str(binary)], env=san_env).strip())
            result["optimization"] = opt
            result["independent_python_wire_oracle"] = check_wire_fixtures(
                result["wire_fixtures"])
            report["sanitizer_runs"].append(result)
            print(f"ASan/UBSan {opt}: {result['checks']} checks passed; "
                  f"{result['parser_fuzz_cases']} parser / {result['wire_fuzz_cases']} wire fuzz",
                  flush=True)
        nm = shutil.which("llvm-nm") or shutil.which("nm")
        if nm is None:
            raise RuntimeError("nm/llvm-nm required to verify zero external symbols")
        targets = [("host", []), ("aarch64-elf", ["--target=aarch64-none-elf"]),
                   ("aarch64-uefi", ["--target=aarch64-unknown-windows-msvc"])]
        for label, target in targets:
            obj = OUT / ("freestanding-" + label + ".o")
            su = obj.with_suffix(".su")
            if su.exists():
                su.unlink()
            run([args.cc, *target, *strict, "-O2", "-ffreestanding", "-fno-builtin",
                 "-fno-stack-protector", "-fstack-usage", "-Wframe-larger-than=256",
                 "-c", str(probe), "-o", str(obj)])
            undefined = run([nm, "--undefined-only", str(obj)]).strip()
            if undefined:
                raise RuntimeError(f"unexpected freestanding external symbols ({label}): {undefined}")
            frames: list[int] = []
            if su.exists():
                for line in su.read_text(encoding="utf-8").splitlines():
                    parts = line.split("\t")
                    if len(parts) >= 3 and re.fullmatch(r"\d+", parts[1]):
                        frames.append(int(parts[1]))
            if not frames:
                raise RuntimeError(f"missing compiler stack-usage evidence for {label}")
            if max(frames) > 256:
                raise RuntimeError(f"stack frame exceeds 256 bytes for {label}")
            report["freestanding"].append({
                "target": label, "undefined_symbols": [],
                "largest_function_frame_bytes": max(frames), "object_sha256": digest(obj),
                "stack_usage": str(su.relative_to(ROOT)),
            })
            print(f"Freestanding {label}: zero undefined symbols; largest frame {max(frames)} bytes",
                  flush=True)
        # A concurrent edit during testing invalidates the evidence rather than certifying it.
        if report["header_sha256"] != digest(HEADER):
            raise RuntimeError("contract header changed during tests; rerun required")
        if report["test_script_sha256"] != digest(Path(__file__).resolve()):
            raise RuntimeError("test script changed during tests; rerun required")
        report["status"] = "passed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr)
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (OUT / "test-results.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        (OUT / "test-output.txt").write_text("\n".join(transcript), encoding="utf-8")
    print(f"Evidence: {OUT / 'test-results.json'}", flush=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
