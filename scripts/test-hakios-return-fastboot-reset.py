#!/usr/bin/env python3
"""Host-only sparse-MMIO fault injection for reset-core.h; no device tooling.

Windows delegates to WSL/Ubuntu user jia. All generated artifacts stay in
evidence/fastboot-return-impl-20261120/reset-core/. Never changes the Git index.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/fastboot-return-impl-20261120/reset-core"
HEADER = ROOT / "ports/hakios-lithium/return-fastboot/reset-core.h"
RESEARCH = ROOT / "evidence/fastboot-return-impl-20261120/reset-research"

TEST = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ports/hakios-lithium/return-fastboot/reset-core.h"

static unsigned long checks, scenarios, injected, status_cases, recovery_cases;
static unsigned long owner_cases, halt_cases;
static const char *suite;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"FAIL %s:%d [%s] %s\n",__FILE__,__LINE__,suite,#x); \
    exit(1); } } while (0)
#define EQ(a,b) CHECK((a)==(b))
#define TICKET UINT64_C(0x7265736574303031)
#define EVENTS 8192u

struct event { uint32_t kind; uint64_t pa; uint32_t value; };
struct model {
    uint32_t map[256], owners[256], version, cfg[2][2], data[2][2], status[2][2];
    uint32_t imem, terminal, terminal_applied, commands, pmic_writes;
    uint32_t callbacks, fail_at, fail_at2, fail_after, fail_seen;
    uint32_t fault_cmd, fault_status, stuck_cmd, busy, remain[2][2];
    uint32_t corrupt_at, corrupt_seen;
    uint32_t restore_seen, success_delay300, enables, type_writes;
    uint32_t channel[2], record;
    uint32_t halt_calls, halted, owner_flip_cmd, mapping_flip_cmd;
    uint8_t pmic[2][256];
    uint64_t time, disable_at[2];
    struct event events[EVENTS];
};
static struct model m;
static struct lfbr_core c;
static uint32_t baseline_prepare, baseline_all, baseline_commands;
static uint32_t imem_verify_event;

static int event(uint32_t kind, uint64_t pa, uint32_t value)
{
    uint32_t n = ++m.callbacks;
    CHECK(n < EVENTS);
    if (m.record) {
        m.events[n].kind=kind; m.events[n].pa=pa; m.events[n].value=value;
    }
    if (n == m.fail_at || n == m.fail_at2) {
        ++m.fail_seen;
        return 0;
    }
    return 1;
}
static int window(uint64_t pa, uint32_t *n, uint32_t *obs, uint32_t *off)
{
    uint32_t i, o;
    for (i=0;i<2u;++i) for (o=0;o<2u;++o) {
        uint64_t b=(o ? LFBR_OBSERVER_PA : LFBR_WRITE_PA)+
                    LFBR_STRIDE*m.channel[i];
        if (pa>=b && pa<b+0x20u) {
            *n=i; *obs=o; *off=(uint32_t)(pa-b); return 1;
        }
    }
    return 0;
}
static int rd(void *opaque, uint64_t pa, uint32_t *v)
{
    uint32_t n=0,o=0,off=0,value=0;
    (void)opaque;
    EQ(m.halted,0u);
    EQ(pa&3u,0u);
    if (!event(1u,pa,0u)) return 0;
    if (pa==LFBR_CORE_PA) value=m.version;
    else if (pa>=LFBR_CORE_PA+0x800u && pa<=LFBR_CORE_PA+0xbfcu)
        value=m.map[(pa-(LFBR_CORE_PA+0x800u))/4u];
    else if (pa>=LFBR_CNFG_PA+0x700u && pa<=LFBR_CNFG_PA+0xafcu)
        value=m.owners[(pa-(LFBR_CNFG_PA+0x700u))/4u];
    else if (pa==LFBR_IMEM_PA) value=m.imem;
    else {
        CHECK(window(pa,&n,&o,&off));
        if (off==4u) value=m.cfg[n][o];
        else if (off==8u) {
            if (m.stuck_cmd && m.commands==m.stuck_cmd) value=0u;
            else if (m.remain[n][o]) { --m.remain[n][o]; value=0u; }
            else value=m.status[n][o];
        } else if (off==0x18u) { EQ(o,1u); value=m.data[n][o]; }
        else CHECK(0);
    }
    if (m.callbacks==m.corrupt_at) { value^=1u; ++m.corrupt_seen; }
    if (m.record) m.events[m.callbacks].value=value;
    *v=value;
    return 1;
}
static void apply(uint64_t pa, uint32_t v)
{
    uint32_t n=0,o=0,off=0;
    if (pa==LFBR_IMEM_PA) { m.imem=v; return; }
    if (pa==LFBR_PSHOLD_PA) { EQ(v,0u); ++m.terminal_applied; return; }
    CHECK(window(pa,&n,&o,&off));
    if (off==4u) { EQ(v,0u); m.cfg[n][o]=v; return; }
    if (off==0x10u) { EQ(o,0u); CHECK(v<=255u); m.data[n][o]=v; return; }
    EQ(off,0u);
    {
        uint32_t opcode=v>>27, reg=(v>>4)&0xffu;
        EQ(v & UINT32_C(0x07fff00f),0u); /* priority,SID,PID,bytecount */
        CHECK(reg==0x8fu || reg==0x5au || reg==0x5bu);
        EQ(opcode,o); /* independent READ1 observer / WRITE0 channel oracle */
        if (reg==0x8fu) EQ(n,0u);
        ++m.commands;
        m.remain[n][o]=m.busy;
        m.status[n][o]=(m.fault_cmd==m.commands ? m.fault_status : 1u);
        if (o) m.data[n][o]=m.pmic[n][reg];
        else {
            uint8_t b=(uint8_t)m.data[n][o];
            ++m.pmic_writes;
            if (reg==0x5bu && b==0u) m.disable_at[n]=m.time;
            if (reg==0x5au) {
                /* Independent native-sequence oracle for forward AND recovery. */
                EQ(m.pmic[0][0x5b],0u); EQ(m.pmic[1][0x5b],0u);
                CHECK(m.time>=m.disable_at[0]+300u);
                CHECK(m.time>=m.disable_at[1]+300u);
                ++m.type_writes;
            }
            if (reg==0x5bu && (b&0x80u)) ++m.enables;
            m.pmic[n][reg]=b;
        }
        if (m.owner_flip_cmd==m.commands) m.owners[m.channel[n]]=1u;
        if (m.mapping_flip_cmd==m.commands) m.map[m.channel[n]]^=1u;
    }
}
static int wr(void *opaque, uint64_t pa, uint32_t v)
{
    int ok;
    (void)opaque;
    if (m.halted) EQ(pa,LFBR_PSHOLD_PA);
    EQ(pa&3u,0u);
    if (pa==LFBR_PSHOLD_PA) ++m.terminal;
    ok=event(2u,pa,v);
    if (ok || m.fail_after) apply(pa,v);
    return ok;
}
static int delay(void *opaque, uint32_t us)
{
    (void)opaque;
    EQ(m.halted,0u);
    CHECK(us==1u || us==300u);
    if (!event(3u,0u,us)) return 0;
    m.time+=us;
    if (us==300u) ++m.success_delay300;
    return 1;
}
static int fence(void *opaque)
{
    (void)opaque;
    EQ(m.halted,0u);
    return event(4u,0u,0u);
}
static int halt(void *opaque)
{
    int ok;
    (void)opaque;
    EQ(m.halted,0u);
    ++m.halt_calls;
    ok=event(5u,0u,0u);
    if (ok || m.fail_after) m.halted=1u;
    return ok;
}
static void setup(uint32_t low)
{
    const struct lfbr_io io={0,rd,wr,delay,fence,0};
    const struct lfbr_config cfg={LFBR_API_VERSION,LFBR_Q_REQUIRED,0u,4u,0u};
    memset(&m,0,sizeof(m));
    ++scenarios;
    m.version=LFBR_V2_MIN;
    m.channel[0]=7u; m.channel[1]=201u;
    m.map[7]=0x00800u; m.map[201]=0x20800u;
    m.imem=UINT32_C(0x11223344);
    m.pmic[0][0x8f]=(uint8_t)low;
    m.pmic[0][0x5a]=1u; m.pmic[1][0x5a]=4u;
    m.pmic[0][0x5b]=0x80u; m.pmic[1][0x5b]=0x81u;
    EQ(lfbr_init(&c,&io,&cfg),LFBR_OK);
    EQ(m.callbacks,0u);
}
static void restored(void)
{
    EQ(c.dirty,0u);
    EQ(m.pmic[0][0x8f],0xb7u);
    EQ(m.pmic[0][0x5a],1u); EQ(m.pmic[1][0x5a],4u);
    EQ(m.pmic[0][0x5b],0x80u); EQ(m.pmic[1][0x5b],0x81u);
    EQ(m.imem,UINT32_C(0x11223344));
}
static void reject_reentry(void)
{
    uint32_t calls=m.callbacks;
    CHECK(lfbr_prepare(&c,TICKET)!=LFBR_OK);
    CHECK(lfbr_commit(&c,TICKET)!=LFBR_OK);
    CHECK(lfbr_abort(&c)!=LFBR_OK);
    EQ(m.callbacks,calls);
}
static void success(void)
{
    uint32_t i, j=0u;
    static const uint32_t regs[]={0x8f,0x5b,0x5b,0x5a,0x5a,0x5b,0x5b};
    static const uint32_t vals[]={0x0b,0,0,7,7,0x80,0x80};
    static const uint32_t ns[]={0,0,1,0,1,0,1};
    suite="exact native order/readback/terminal";
    setup(0xb7u); m.record=1u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    EQ(c.state,(uint32_t)LFBR_PREPARED); EQ(c.ticket,TICKET);
    EQ(c.reason_before,0xb7u); EQ(c.reason_after,0x0bu);
    EQ(c.imem_after,LFBR_IMEM_FASTBOOT);
    EQ(c.imem_before,UINT32_C(0x11223344));
    EQ(m.pmic_writes,7u); EQ(m.success_delay300,1u);
    EQ(m.terminal,0u);
    baseline_prepare=m.callbacks;
    baseline_commands=m.commands;
    imem_verify_event=m.callbacks;
    EQ(m.events[imem_verify_event].pa,LFBR_IMEM_PA);
    EQ(m.events[imem_verify_event].kind,1u);
    for (i=1;i<=baseline_prepare;++i) {
        uint32_t n=0,o=0,off=0;
        if (m.events[i].kind!=2u ||
            !window(m.events[i].pa,&n,&o,&off) || o || off) continue;
        CHECK(j<7u);
        EQ((m.events[i].value>>4)&255u,regs[j]);
        EQ(n,ns[j]);
        /* WDATA immediately precedes barrier then command. */
        CHECK(i>2u); EQ(m.events[i-2u].kind,2u);
        EQ(m.events[i-2u].value,vals[j]);
        ++j;
    }
    EQ(j,7u);
    EQ(lfbr_commit(&c,TICKET),LFBR_TERMINAL_RETURNED);
    baseline_all=m.callbacks;
    EQ(m.terminal,1u); EQ(m.terminal_applied,1u);
    EQ(m.events[baseline_all].kind,2u);
    EQ(m.events[baseline_all].pa,LFBR_PSHOLD_PA);
    EQ(c.state,(uint32_t)LFBR_CONSUMED); EQ(c.terminal_attempted,1u);
    EQ(c.ticket,0u); reject_reentry();
    suite="all 256 stale-reason encodings";
    for (i=0;i<256u;++i) {
        setup(i);
        EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
        EQ(c.reason_after,(i&3u)|8u);
        EQ(m.pmic[0][0x8f],(i&3u)|8u);
        EQ(lfbr_abort(&c),LFBR_OK);
        EQ(m.pmic[0][0x8f],i);
        EQ(c.dirty,0u); EQ(m.terminal,0u);
    }
}
static void gates(void)
{
    uint32_t bit, old;
    suite="preconditions/null/unsupported reject before MMIO";
    EQ(lfbr_init(0,0,0),LFBR_BAD_ARGUMENT);
    EQ(lfbr_prepare(0,TICKET),LFBR_BAD_ARGUMENT);
    EQ(lfbr_commit(0,TICKET),LFBR_BAD_ARGUMENT);
    EQ(lfbr_abort(0),LFBR_BAD_ARGUMENT);
    for (bit=0;bit<8u;++bit) {
        if (bit==2u) continue;
        setup(0xb7u); c.config.qualifications&=~(1u<<bit);
        EQ(lfbr_prepare(&c,TICKET),LFBR_PRECONDITION);
        EQ(m.callbacks,0u); reject_reentry();
    }
    for (bit=0;bit<8u;++bit) {
        setup(0xb7u);
        if (bit==0u) c.config.owner=1u;
        if (bit==1u) c.config.owner=UINT32_MAX;
        if (bit==2u) c.config.version=0u;
        if (bit==3u) c.config.polls=0u;
        if (bit==4u) c.config.polls=LFBR_MAX_POLLS+1u;
        if (bit==5u) c.config.arbiter_halt_required=1u;
        if (bit==6u) c.config.qualifications|=256u;
        CHECK(lfbr_prepare(&c,bit==7u ? 0u:TICKET)!=LFBR_OK);
        EQ(m.callbacks,0u); reject_reentry();
    }
    setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    old=m.callbacks; c.config.qualifications&=~LFBR_Q_MAPPINGS;
    EQ(lfbr_commit(&c,TICKET),LFBR_PRECONDITION);
    EQ(m.callbacks,old); CHECK(c.dirty!=0u); EQ(m.terminal,0u);
    setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    EQ(lfbr_commit(&c,TICKET+1u),LFBR_TICKET); restored();
    EQ(m.terminal,0u); reject_reentry();
}
static void topology(void)
{
    uint32_t i;
    static const uint32_t versions[]={0u,0x10000000u,0x20000000u,
                                     0x30000000u,UINT32_MAX};
    suite="v2 version/channel cardinality/config/mapping stability";
    for (i=0;i<sizeof(versions)/sizeof(versions[0]);++i) {
        setup(0xb7u); m.version=versions[i];
        EQ(lfbr_prepare(&c,TICKET),LFBR_VERSION);
        EQ(m.commands,0u); EQ(m.pmic_writes,0u); EQ(m.terminal,0u);
    }
    setup(0xb7u); m.map[7]=0u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_CHANNEL); EQ(m.commands,0u);
    setup(0xb7u); m.map[250]=m.map[7];
    EQ(lfbr_prepare(&c,TICKET),LFBR_CHANNEL); EQ(m.commands,0u);
    setup(0xb7u); m.map[250]=m.map[201];
    EQ(lfbr_prepare(&c,TICKET),LFBR_CHANNEL); EQ(m.commands,0u);
    setup(0xb7u); m.cfg[0][1]=1u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_PRECONDITION); EQ(m.commands,0u);
    setup(0xb7u); m.pmic[1][0x5b]=0u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_PRECONDITION); EQ(m.pmic_writes,0u);
    setup(0xb7u);
    m.map[255]=m.map[201]; m.map[201]=0u; m.channel[1]=255u;
    m.map[0]=m.map[7]; m.map[7]=0u; m.channel[0]=0u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_OK); EQ(c.channel[1],255u);
    EQ(lfbr_abort(&c),LFBR_OK); restored();
    setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    m.map[7]^=1u; i=m.commands;
    EQ(lfbr_commit(&c,TICKET),LFBR_CHANNEL); EQ(m.commands,i);
    CHECK(c.dirty!=0u); EQ(m.terminal,0u);
    setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    ++m.version; i=m.commands;
    EQ(lfbr_commit(&c,TICKET),LFBR_VERSION); EQ(m.commands,i);
    CHECK(c.dirty!=0u); EQ(m.terminal,0u);
}
static void ownership(void)
{
    uint32_t n,owner,cmd;
    suite="every ownership value on both APIDs, unrelated high bits";
    for (n=0;n<2u;++n) for (owner=0;owner<8u;++owner) {
        int e;
        setup(0xb7u); m.owners[m.channel[n]]=0xabc000u|owner;
        e=lfbr_prepare(&c,TICKET);
        if (!owner) {
            EQ(e,LFBR_OK); EQ(c.owner_before[n],0xabc000u);
            EQ(lfbr_abort(&c),LFBR_OK); restored();
        } else {
            EQ(e,LFBR_OWNER); EQ(m.commands,0u); EQ(m.pmic_writes,0u);
        }
        EQ(m.terminal,0u); ++owner_cases;
    }
    suite="ownership changes between prepare/commit block commands";
    for (n=0;n<2u;++n) for (owner=1;owner<8u;++owner) {
        setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
        m.owners[m.channel[n]]=owner; cmd=m.commands;
        EQ(lfbr_commit(&c,TICKET),LFBR_OWNER);
        EQ(m.terminal,0u); CHECK(c.dirty!=0u);
        /* SID0 is rechecked before any command; SID2 follows SID0 reads. */
        if (n==0u) EQ(m.commands,cmd);
        ++owner_cases; reject_reentry();
    }
    suite="owner/map changes after EACH command are rechecked";
    for (cmd=1;cmd<=baseline_commands+5u;++cmd) {
        int e;
        setup(0xb7u); m.owner_flip_cmd=cmd;
        e=lfbr_prepare(&c,TICKET);
        if (!e) e=lfbr_commit(&c,TICKET);
        EQ(e,LFBR_OWNER); EQ(m.terminal,0u); ++owner_cases;
        setup(0xb7u); m.mapping_flip_cmd=cmd;
        e=lfbr_prepare(&c,TICKET);
        if (!e) e=lfbr_commit(&c,TICKET);
        EQ(e,LFBR_CHANNEL); EQ(m.terminal,0u); ++owner_cases;
    }
}
static void halting(void)
{
    uint32_t i, after, end, calls;
    suite="required halt callback missing rejects before MMIO";
    setup(0xb7u); c.config.arbiter_halt_required=1u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_PRECONDITION); EQ(m.callbacks,0u);
    setup(0xb7u); c.config.arbiter_halt_required=2u; c.io.halt_arbiter=halt;
    EQ(lfbr_prepare(&c,TICKET),LFBR_PRECONDITION); EQ(m.callbacks,0u);
    suite="optional halt available0 does not call provided callback";
    setup(0xb7u); c.io.halt_arbiter=halt;
    EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    EQ(lfbr_commit(&c,TICKET),LFBR_TERMINAL_RETURNED); EQ(m.halt_calls,0u);
    suite="available1 single halt immediately before terminal, no subsequent I/O";
    setup(0xb7u); m.record=1u;
    c.config.arbiter_halt_required=1u; c.io.halt_arbiter=halt;
    EQ(lfbr_prepare(&c,TICKET),LFBR_OK); EQ(m.halt_calls,0u);
    EQ(lfbr_commit(&c,TICKET),LFBR_TERMINAL_RETURNED);
    EQ(m.halt_calls,1u); EQ(m.terminal,1u);
    EQ(c.halt_attempted,1u); EQ(c.halt_succeeded,1u);
    EQ(m.events[m.callbacks-2u].kind,4u); /* final barrier */
    EQ(m.events[m.callbacks-1u].kind,5u); /* halt */
    EQ(m.events[m.callbacks].pa,LFBR_PSHOLD_PA);
    end=m.callbacks; EQ(end,baseline_all+1u); reject_reentry();
    suite="fault every required-halt commit callback including ambiguous halt";
    for (after=0;after<2u;++after)
        for (i=baseline_prepare+1u;i<=end;++i) {
            int e;
            setup(0xb7u); c.config.arbiter_halt_required=1u;
            c.io.halt_arbiter=halt;
            EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
            m.fail_at=i; m.fail_after=after;
            e=lfbr_commit(&c,TICKET);
            CHECK(e!=LFBR_OK); EQ(m.fail_seen,1u); ++halt_cases;
            if (i==end-1u) {
                EQ(e,LFBR_HALT); EQ(c.state,(uint32_t)LFBR_FAILED);
                EQ(m.halt_calls,1u); EQ(m.callbacks,i);
                EQ(m.terminal,0u); EQ(c.recovery_attempted,0u);
                EQ(c.halt_attempted,1u); EQ(c.halt_succeeded,0u);
                CHECK(c.dirty!=0u);
                calls=m.callbacks;
                EQ(lfbr_recover(&c),LFBR_BAD_STATE);
                EQ(m.callbacks,calls);
            } else if (i==end) {
                EQ(e,LFBR_TERMINAL_IO); EQ(m.terminal,1u);
                EQ(m.halt_calls,1u); EQ(m.callbacks,i);
            } else { EQ(m.halt_calls,0u); EQ(m.terminal,0u); }
            reject_reentry();
        }
}
static void fault_sweep(void)
{
    uint32_t i, after;
    suite="every prepare callback failure before/after write effect";
    for (after=0;after<2u;++after) for (i=1;i<=baseline_prepare;++i) {
        setup(0xb7u); m.fail_at=i; m.fail_after=after;
        CHECK(lfbr_prepare(&c,TICKET)!=LFBR_OK);
        EQ(m.fail_seen,1u); ++injected;
        EQ(c.state,(uint32_t)LFBR_FAILED); EQ(m.terminal,0u);
        EQ(c.ticket,0u); reject_reentry();
        if (!c.dirty) restored();
        if (c.transport_uncertain) CHECK(c.recovery_attempted<=1u);
    }
    suite="every commit callback failure before/after write effect";
    for (after=0;after<2u;++after)
        for (i=baseline_prepare+1u;i<=baseline_all;++i) {
            int e;
            setup(0xb7u);
            EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
            m.fail_at=i; m.fail_after=after;
            e=lfbr_commit(&c,TICKET);
            CHECK(e!=LFBR_OK); EQ(m.fail_seen,1u); ++injected;
            if (i==baseline_all) {
                EQ(e,LFBR_TERMINAL_IO); EQ(m.terminal,1u);
                EQ(m.callbacks,i); EQ(c.terminal_attempted,1u);
            } else EQ(m.terminal,0u);
            reject_reentry();
            if (!c.dirty) restored();
        }
}
static void statuses(void)
{
    static const uint32_t bad[]={2u,3u,4u,5u,8u,9u,15u,17u,UINT32_MAX};
    uint32_t cmd,i;
    suite="all commands: known/unknown status bits and bounded timeouts";
    for (cmd=1;cmd<=baseline_commands+5u;++cmd) {
        for (i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
            int e;
            setup(0xb7u); m.fault_cmd=cmd; m.fault_status=bad[i];
            e=lfbr_prepare(&c,TICKET);
            if (!e) e=lfbr_commit(&c,TICKET);
            EQ(e,LFBR_SPMI_STATUS); EQ(m.terminal,0u);
            ++status_cases; reject_reentry();
            if (!c.dirty) restored();
        }
        setup(0xb7u); m.stuck_cmd=cmd;
        {
            int e=lfbr_prepare(&c,TICKET);
            if (!e) e=lfbr_commit(&c,TICKET);
            EQ(e,LFBR_TIMEOUT); EQ(c.transport_uncertain,1u);
            EQ(m.commands,cmd); EQ(m.terminal,0u); ++status_cases;
        }
    }
    setup(0xb7u); m.busy=3u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    EQ(c.poll_count,m.commands*4u); EQ(lfbr_abort(&c),LFBR_OK); restored();
    setup(0xb7u); c.config.polls=1u; m.stuck_cmd=1u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_TIMEOUT); EQ(c.poll_count,1u);
    setup(0xb7u); c.config.polls=LFBR_MAX_POLLS; m.stuck_cmd=1u;
    EQ(lfbr_prepare(&c,TICKET),LFBR_TIMEOUT); EQ(c.poll_count,LFBR_MAX_POLLS);
}
static void recoveries(void)
{
    uint32_t i, end, after;
    suite="readback mismatch restores all state with native sequence";
    setup(0xb7u); m.corrupt_at=imem_verify_event;
    EQ(lfbr_prepare(&c,TICKET),LFBR_VERIFY); EQ(m.corrupt_seen,1u);
    EQ(c.recovery_attempted,1u); EQ(c.recovery_error,0u); restored();
    end=m.callbacks; CHECK(end>imem_verify_event);
    suite="fault every recovery callback, no second recovery or terminal";
    for (after=0;after<2u;++after)
        for (i=imem_verify_event+1u;i<=end;++i) {
            setup(0xb7u); m.corrupt_at=imem_verify_event;
            m.fail_at=i; m.fail_after=after;
            EQ(lfbr_prepare(&c,TICKET),LFBR_VERIFY);
            EQ(m.fail_seen,1u); EQ(c.recovery_attempted,1u);
            CHECK(c.recovery_error!=0u); CHECK(c.dirty!=0u);
            EQ(m.terminal,0u); ++recovery_cases; reject_reentry();
        }
    suite="abort success/failure is consumed";
    setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    EQ(lfbr_abort(&c),LFBR_OK); restored(); reject_reentry();
    setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
    m.fail_at=m.callbacks+1u;
    CHECK(lfbr_abort(&c)!=LFBR_OK);
    EQ(c.state,(uint32_t)LFBR_ABORTED); CHECK(c.dirty!=0u);
    EQ(m.imem,UINT32_C(0x11223344)); reject_reentry();
    suite="tampering between prepare and commit restores originals";
    for (i=0;i<6u;++i) {
        setup(0xb7u); EQ(lfbr_prepare(&c,TICKET),LFBR_OK);
        if (i==0u) m.pmic[0][0x8f]^=1u;
        if (i==1u) m.pmic[0][0x5a]^=1u;
        if (i==2u) m.pmic[1][0x5a]^=1u;
        if (i==3u) m.pmic[0][0x5b]^=1u;
        if (i==4u) m.pmic[1][0x5b]^=1u;
        if (i==5u) m.imem^=1u;
        EQ(lfbr_commit(&c,TICKET),LFBR_VERIFY); restored();
        EQ(m.terminal,0u); reject_reentry();
    }
}
int main(void)
{
    suite="startup";
    success(); gates(); topology(); ownership();
    fault_sweep(); statuses(); recoveries(); halting();
    printf("{\"checks\":%lu,\"scenarios\":%lu,\"callback_faults\":%lu,"
           "\"status_cases\":%lu,\"recovery_faults\":%lu,"
           "\"prepare_callbacks\":%u,\"total_callbacks\":%u,"
           "\"prepare_commands\":%u,\"owner_cases\":%lu,\"halt_faults\":%lu}\n",checks,scenarios,injected,
           status_cases,recovery_cases,baseline_prepare,baseline_all,
           baseline_commands,owner_cases,halt_cases);
    return 0;
}
'''

PROBE = r'''
#include "ports/hakios-lithium/return-fastboot/reset-core.h"
int reset_probe_init(struct lfbr_core *c, const struct lfbr_io *io,
                     const struct lfbr_config *cfg) { return lfbr_init(c,io,cfg); }
int reset_probe_prepare(struct lfbr_core *c,uint64_t t) { return lfbr_prepare(c,t); }
int reset_probe_commit(struct lfbr_core *c,uint64_t t) { return lfbr_commit(c,t); }
int reset_probe_abort(struct lfbr_core *c) { return lfbr_abort(c); }
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--cc", default="clang")
    args = ap.parse_args()
    if os.name == "nt" and not args.native:
        drive, tail = os.path.splitdrive(str(Path(__file__).resolve()))
        linux = "/mnt/" + drive[0].lower() + tail.replace("\\", "/")
        return subprocess.run(["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--",
                               "python3", linux, "--native", "--cc", args.cc],
                              check=False).returncode
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = OUT / "tmp"
    tmp.mkdir(exist_ok=True)
    for var in ("TMPDIR", "TMP", "TEMP"):
        os.environ[var] = str(tmp)
    source, probe = OUT / "reset-tests.c", OUT / "freestanding-probe.c"
    source.write_text(TEST.lstrip(), encoding="utf-8")
    probe.write_text(PROBE.lstrip(), encoding="utf-8")
    report = {
        "status": "running", "started_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "host only: callback emulator, sanitizers and cross-compile; no device or Git writes",
        "header_sha256": digest(HEADER),
        "test_script_sha256": digest(Path(__file__).resolve()),
        "test_source_sha256": digest(source),
        "commands": [], "sanitizer_runs": [], "freestanding": [],
        "unverified": [
            "Runtime SPMI/PMIC permissions and truth/continued stability of MMIO owner observations",
            "Firmware arbiter exclusivity and installed board identity",
            "Firmware mappings, ordering, bounded MMIO and real >=300us delay",
            "Optional secure-world arbiter-halt requirements",
            "Installed stock identity, boot-chain policy and reset reason retention",
            "Physical hard reset, return to Fastboot and subsequent touch operation",
        ],
    }
    transcript: list[str] = []

    def run(argv: list[str], env=None) -> str:
        cp = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, check=False,
                            timeout=60)
        report["commands"].append({"argv": argv, "exit_code": cp.returncode})
        transcript.append("$ " + " ".join(argv) + "\n" + cp.stdout)
        if cp.returncode:
            raise RuntimeError(f"command failed ({cp.returncode}): {' '.join(argv)}\n{cp.stdout}")
        return cp.stdout

    try:
        report["compiler"] = run([args.cc, "--version"]).splitlines()[0]
        strict = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                  "-Wconversion", "-Wshadow", "-Wvla", "-I", str(ROOT)]
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        for opt in ("-O1", "-O2"):
            exe = OUT / ("reset-tests-" + opt[1:])
            run([args.cc, *strict, opt, "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                 str(source), "-o", str(exe)])
            result = json.loads(run([str(exe)], env).strip())
            result["optimization"] = opt
            report["sanitizer_runs"].append(result)
            print(f"ASan/UBSan {opt}: {result['scenarios']} scenarios, "
                  f"{result['checks']} checks", flush=True)
        nm = shutil.which("llvm-nm") or shutil.which("nm")
        if not nm:
            raise RuntimeError("nm/llvm-nm required")
        for label, target in [
            ("host", []), ("aarch64-elf", ["--target=aarch64-none-elf"]),
            ("aarch64-uefi", ["--target=aarch64-unknown-windows-msvc"]),
        ]:
            obj = OUT / ("freestanding-" + label + ".o")
            run([args.cc, *target, *strict, "-O2", "-ffreestanding", "-fno-builtin",
                 "-fno-stack-protector", "-fstack-usage", "-c", str(probe),
                 "-o", str(obj)])
            undefined = run([nm, "--undefined-only", str(obj)]).strip()
            if undefined:
                raise RuntimeError(f"{label}: unexpected externals: {undefined}")
            report["freestanding"].append({
                "target": label, "undefined_symbols": [], "sha256": digest(obj),
                "stack_usage_file": str(obj.with_suffix(".su").relative_to(ROOT)),
            })
            print(f"Freestanding {label}: zero undefined symbols", flush=True)
        refs = [
            "lk-platform__msm_shared__spmi.c",
            "lk-platform__msm_shared__include__spmi.h",
            "lk-platform__msm_shared__include__spmi_v2.h",
            "lk-platform__msm8996__platform.c",
            "lk-dev__pmic__pm8x41__pm8x41.c",
            "lk-platform__msm_shared__reboot.c",
        ]
        manifest = json.loads((RESEARCH / "source-manifest.json").read_text(encoding="utf-8"))
        expected = {Path(p["local_path"]).name: p["sha256"] for p in manifest["public_sources"]}
        hashes = []
        for name in refs:
            path = RESEARCH / name
            sha = digest(path)
            if sha != expected[name]:
                raise RuntimeError("primary source hash mismatch: " + name)
            hashes.append({"path": str(path.relative_to(ROOT)), "sha256": sha,
                           "matches_research_manifest": True})
        (OUT / "source-hashes.json").write_text(json.dumps({
            "commit": manifest["commit"], "sources": hashes,
            "stock_binary_evidence": str((RESEARCH / "binary-evidence-index.json").relative_to(ROOT)),
            "stock_binary_evidence_sha256": digest(RESEARCH / "binary-evidence-index.json"),
            "stock_dt_metadata_sha256": digest(ROOT / "evidence/fastboot-return-audit-20261120/reset/stock-dt-evidence.json"),
            "linux_v6_12": json.loads((OUT / "linux-source.json").read_text(encoding="utf-8")),
        }, indent=2) + "\n", encoding="utf-8")
        linux_meta = json.loads((OUT / "linux-source.json").read_text(encoding="utf-8"))
        if digest(OUT / "linux-v6.12-spmi-pmic-arb.c") != linux_meta["sha256"]:
            raise RuntimeError("Linux primary source hash mismatch")
        if digest(HEADER) != report["header_sha256"] or digest(Path(__file__).resolve()) != report["test_script_sha256"]:
            raise RuntimeError("implementation or runner changed during test")
        report["status"] = "passed"
    except Exception as exc:
        report["status"], report["error"] = "failed", str(exc)
        print(str(exc), file=sys.stderr, flush=True)
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (OUT / "test-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (OUT / "test-output.txt").write_text("\n".join(transcript), encoding="utf-8")
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
