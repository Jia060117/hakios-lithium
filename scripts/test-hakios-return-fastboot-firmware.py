#!/usr/bin/env python3
"""Compile and exercise the actual LithiumFastbootDxe with host UEFI mocks.

Only writes evidence/fastboot-return-impl-20261120/firmware-review/.
No device commands, USB, Git mutations, production edits, or copied backend.
Windows invokes Clang in the existing Ubuntu WSL environment.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/fastboot-return-impl-20261120/firmware-review"
DRIVER = ROOT / "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"
CORE = ROOT / "ports/hakios-lithium/return-fastboot/reset-core.h"
PROTOCOL = CORE.with_name("firmware-protocol.h")

HEADERS = {
    "ReturnFastboot/firmware-protocol.h": '#include "ports/hakios-lithium/return-fastboot/firmware-protocol.h"\n',
    "ReturnFastboot/reset-core.h": '#include "ports/hakios-lithium/return-fastboot/reset-core.h"\n',
    "Uefi.h": r"""
#ifndef HOST_MOCK_UEFI_H
#define HOST_MOCK_UEFI_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t INT32;
typedef intptr_t INTN;
typedef uintptr_t UINTN;
typedef uint64_t EFI_STATUS;
typedef unsigned char BOOLEAN;
typedef void VOID;
typedef void *EFI_HANDLE;
typedef struct { UINT32 Data1; UINT16 Data2, Data3; UINT8 Data4[8]; } EFI_GUID;
typedef struct { UINTN Unused; } EFI_SYSTEM_TABLE;
#define STATIC static
#define EFIAPI
#define TRUE ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define STATIC_ASSERT(c,m) _Static_assert(c,m)
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER UINT64_C(0x8000000000000002)
#define EFI_UNSUPPORTED UINT64_C(0x8000000000000003)
#define EFI_DEVICE_ERROR UINT64_C(0x8000000000000007)
#define EFI_ACCESS_DENIED UINT64_C(0x800000000000000f)
#define EFI_ERROR(s) (((EFI_STATUS)(s) & UINT64_C(0x8000000000000000)) != 0)
#define EFI_NATIVE_INTERFACE 0u
typedef struct {
  EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *, EFI_GUID *, UINT32, VOID *);
  EFI_STATUS (EFIAPI *Stall)(UINTN);
} EFI_BOOT_SERVICES;
#endif
""",
    "Library/BaseLib.h": "#include <Uefi.h>\n",
    "Library/ArmLib.h": r"""
#include <Uefi.h>
UINT64 ArmReadCntvCt(VOID);
UINTN ArmReadCntFrq(VOID);
UINTN ArmReadMpidr(VOID);
VOID ArmDataSynchronizationBarrier(VOID);
""",
    "Library/BaseMemoryLib.h": "#include <Uefi.h>\nVOID *ZeroMem(VOID *, UINTN);\n",
    "Library/IoLib.h": r"""
#include <Uefi.h>
UINT32 MmioRead32(UINTN);
UINT32 MmioWrite32(UINTN, UINT32);
""",
    "Library/ArmSmcLib.h": r"""
#include <Uefi.h>
typedef struct {
 UINTN Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7, Arg8;
 UINTN Arg9, Arg10, Arg11, Arg12, Arg13, Arg14, Arg15, Arg16, Arg17;
} ARM_SMC_ARGS;
VOID ArmCallSmc(ARM_SMC_ARGS *);
""",
    "Library/UefiBootServicesTableLib.h": "#include <Uefi.h>\nextern EFI_BOOT_SERVICES *gBS;\n",
}

TEST = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* This is the production translation unit, including its real reset core.
 * No replacement Prepare/Commit/lfbr_* implementation is linked. */
#include "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"

#define EVENTS 32768u
enum { EV_READ=1, EV_WRITE, EV_SMC_QUERY, EV_SMC_HALT, EV_STALL, EV_BARRIER };
struct event { unsigned kind; uint64_t pa; uint32_t value; };
struct model {
 uint32_t map[256], owner[256], version, cfg[2][2], data[2][2], status[2][2];
 uint8_t pon[2][256];
 uint32_t channel[2], imem, reads, writes, commands, pon_writes, terminal;
 uint32_t queries, halts, installs, stalls, barriers, mpidr_reads, freq_reads;
 uint32_t counters, expire_at, rewind_at, fault_command, fault_status;
 uint32_t corrupt_data_command, corrupt_imem, stuck_command, busy_polls;
 uint32_t pending[2][2], owner_flip_command, owner_flip_target;
 uint32_t map_flip_command, map_flip_target;
 uint32_t query_error, halt_error, event_count, raw_probe, expired, record;
 uint32_t events_after_terminal, short_stall, frozen_clock;
 uint64_t availability, now, frequency, query_ticks, halt_ticks;
 uint64_t disabled_at[2], last_pa, stall_ticks, terminal_counter;
 uint64_t install_status, stall_status, last_write_value;
 void *installed;
 struct event events[EVENTS];
};
static struct model m;
static unsigned long checks, scenarios, prepare_expiries, commit_expiries;
static unsigned long injected_statuses, rejected_owners, verified_smcs;
static unsigned baseline_prepare_counters, baseline_commit_counters;
static unsigned baseline_prepare_commands;
#if LITHIUM_RAM_DIAGNOSTIC
static unsigned baseline_prepare_events;
#endif
static unsigned baseline_all_events;
static struct event baseline_trace[EVENTS];
static const char *suite = "startup";

#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr,"FAIL %s:%d [%s] %s (scenario=%lu counters=%u events=%u)\n", \
 __FILE__,__LINE__,suite,#x,scenarios,m.counters,m.event_count); exit(1); } } while (0)
#define EQ(a,b) CHECK((a)==(b))
#define TEST(name) do { suite=(name); ++scenarios; } while (0)

static void event(unsigned kind, uint64_t pa, uint32_t value)
{
 ++m.event_count;
 CHECK(m.event_count < EVENTS);
 if (m.terminal) ++m.events_after_terminal;
 if (m.record) m.events[m.event_count]=(struct event){kind,pa,value};
 m.last_pa=pa;
}

static int window(uint64_t pa, unsigned *n, unsigned *obs, unsigned *off)
{
 for (unsigned i=0; i<2; ++i) for (unsigned j=0; j<2; ++j) {
  uint64_t base=(j ? UINT64_C(0x04c00000) : UINT64_C(0x04400000))
      + UINT64_C(0x8000)*m.channel[i];
  if (pa>=base && pa<base+0x20u) {
   *n=i; *obs=j; *off=(unsigned)(pa-base); return 1;
  }
 }
 return 0;
}

/* Independent address oracle: no use of the driver's Writable/Readable. */
static int write_whitelist(uint64_t pa)
{
 if (pa==UINT64_C(0x066bf65c) || pa==UINT64_C(0x004ab000)) return 1;
 for (unsigned j=0; j<2; ++j) {
  uint64_t base=j ? UINT64_C(0x04c00000) : UINT64_C(0x04400000);
  if (pa>=base && pa<base+UINT64_C(0x800000)) {
   uint64_t off=(pa-base)%UINT64_C(0x8000);
   return off==0 || off==4 || (!j && off==0x10);
  }
 }
 return 0;
}

UINT64 ArmReadCntvCt(VOID)
{
 ++m.counters;
 if (m.counters==m.expire_at) {
  m.now+=2*m.frequency+1; m.expired=1;
 }
 if (m.counters==m.rewind_at) { m.now=0; m.expired=1; }
 uint64_t result=m.now;
 if (!m.frozen_clock) ++m.now;
 return result;
}
UINTN ArmReadCntFrq(VOID) { ++m.freq_reads; return (UINTN)m.frequency; }
UINTN ArmReadMpidr(VOID) { ++m.mpidr_reads; return (UINTN)UINT64_C(0x80000101); }
VOID ArmDataSynchronizationBarrier(VOID)
{
 ++m.barriers; event(EV_BARRIER,0,0);
}
VOID *ZeroMem(VOID *p, UINTN n) { return memset(p,0,n); }

UINT32 MmioRead32(UINTN address)
{
 uint64_t pa=(uint64_t)address;
 unsigned n=0,o=0,off=0;
 uint32_t v=0;
 EQ(pa&3u,0u);
 ++m.reads;
 if (m.raw_probe) { event(EV_READ,pa,0); return 0; }
 if (pa==UINT64_C(0x0400f000)) v=m.version;
 else if (pa>=UINT64_C(0x0400f800) && pa<=UINT64_C(0x0400fbfc)) {
  unsigned i=(unsigned)((pa-UINT64_C(0x0400f800))/4u);
  if (m.map_flip_command && m.commands>=m.map_flip_command &&
      i==m.channel[m.map_flip_target]) {
   m.map[i]^=1; m.map_flip_command=0;
  }
  v=m.map[i];
 }
 else if (pa>=UINT64_C(0x0400a700) && pa<=UINT64_C(0x0400aafc)) {
  unsigned i=(unsigned)((pa-UINT64_C(0x0400a700))/4u);
  if (m.owner_flip_command && m.commands>=m.owner_flip_command &&
      i==m.channel[m.owner_flip_target]) m.owner[i]=1;
  v=m.owner[i];
 } else if (pa==UINT64_C(0x066bf65c)) {
  v=m.imem;
  if (m.corrupt_imem && v==UINT32_C(0x77665500)) { v^=1; m.corrupt_imem=0; }
 } else {
  CHECK(window(pa,&n,&o,&off));
  if (off==4) v=m.cfg[n][o];
  else if (off==8) {
   if (m.stuck_command && m.commands==m.stuck_command) v=0;
   else if (m.pending[n][o]) { --m.pending[n][o]; v=0; }
   else v=m.status[n][o];
  } else if (off==0x18) {
   EQ(o,1u); v=m.data[n][o];
   if (m.commands==m.corrupt_data_command) { v^=1; m.corrupt_data_command=0; }
  } else CHECK(0);
 }
 event(EV_READ,pa,v);
 return v;
}

UINT32 MmioWrite32(UINTN address, UINT32 value)
{
 uint64_t pa=(uint64_t)address;
 unsigned n=0,o=0,off=0;
 CHECK(write_whitelist(pa)); EQ(pa&3u,0u);
 ++m.writes;
 event(EV_WRITE,pa,value);
 m.last_write_value=value;
 if (m.raw_probe) return value;
 if (pa==UINT64_C(0x066bf65c)) { m.imem=value; return value; }
 if (pa==UINT64_C(0x004ab000)) {
  EQ(value,0u); EQ(m.terminal,0u);
  EQ(mCore.state,(uint32_t)LFBR_CONSUMED); EQ(mCore.ticket,0u);
  EQ(mCore.terminal_attempted,1u);
  ++m.terminal; m.terminal_counter=m.counters; return value;
 }
 CHECK(window(pa,&n,&o,&off));
 EQ(m.owner[m.channel[n]]&7u,0u);
 if (off==4) { EQ(value,0u); m.cfg[n][o]=value; return value; }
 if (off==0x10) { EQ(o,0u); CHECK(value<=255u); m.data[n][o]=value; return value; }
 EQ(off,0u);
 unsigned opcode=value>>27, reg=(value>>4)&0xffu;
 EQ(value&UINT32_C(0x07fff00f),0u);
 EQ(opcode,o);
 CHECK(reg==0x8f || reg==0x5a || reg==0x5b);
 if (reg==0x8f) EQ(n,0u);
 ++m.commands;
 m.status[n][o]=m.commands==m.fault_command ? m.fault_status : 1u;
 m.pending[n][o]=m.busy_polls;
 if (o) m.data[n][o]=m.pon[n][reg];
 else {
  uint8_t b=(uint8_t)m.data[n][o];
  if (reg==0x5b && b==0) m.disabled_at[n]=m.now;
  if (reg==0x5a) {
   EQ(m.pon[0][0x5b],0u); EQ(m.pon[1][0x5b],0u);
   CHECK(m.now>=m.disabled_at[0]+300);
   CHECK(m.now>=m.disabled_at[1]+300);
  }
  m.pon[n][reg]=b; ++m.pon_writes;
 }
 return value;
}

VOID ArmCallSmc(ARM_SMC_ARGS *args)
{
 CHECK(args!=NULL);
 EQ(args->Arg1,1u);
 EQ(args->Arg3,0u); EQ(args->Arg4,0u); EQ(args->Arg5,0u);
 EQ(args->Arg6,0u); EQ(args->Arg7,0u);
 EQ(args->Arg8,0u); EQ(args->Arg9,0u); EQ(args->Arg10,0u);
 EQ(args->Arg11,0u); EQ(args->Arg12,0u); EQ(args->Arg13,0u);
 EQ(args->Arg14,0u); EQ(args->Arg15,0u); EQ(args->Arg16,0u); EQ(args->Arg17,0u);
 if (args->Arg0==UINT32_C(0x02000601)) {
  EQ(args->Arg2,UINT32_C(0x02000901));
  EQ(m.reads,0u); EQ(m.writes,0u);
  ++m.queries; event(EV_SMC_QUERY,0,(uint32_t)args->Arg2);
  args->Arg0=m.query_error; args->Arg1=m.availability;
  m.now+=m.query_ticks;
 } else {
  EQ(args->Arg0,UINT32_C(0x82000901)); EQ(args->Arg2,0u);
  EQ(mCore.ticket,0u); EQ(mCore.state,(uint32_t)LFBR_CONSUMED);
  EQ(mCore.halt_attempted,1u); EQ(mCore.terminal_attempted,0u);
  EQ(m.availability,1u);
  EQ(m.halts,0u); EQ(m.terminal,0u);
  ++m.halts; event(EV_SMC_HALT,0,0);
  args->Arg0=m.halt_error; m.now+=m.halt_ticks;
 }
 ++verified_smcs;
}

static EFI_STATUS EFIAPI install(EFI_HANDLE *handle, EFI_GUID *guid,
                                 UINT32 interface_type, VOID *protocol)
{
 static const EFI_GUID expected={LFB_PROTOCOL_GUID_VALUES};
 CHECK(handle!=NULL); EQ(*handle,NULL); CHECK(guid!=NULL);
 EQ(memcmp(guid,&expected,sizeof expected),0);
 EQ(interface_type,EFI_NATIVE_INTERFACE);
 EQ(protocol,&mProtocol);
 ++m.installs; m.installed=protocol;
 *handle=(EFI_HANDLE)(uintptr_t)0x1234;
 return m.install_status;
}
static EFI_STATUS EFIAPI stall(UINTN us)
{
 ++m.stalls; event(EV_STALL,0,(uint32_t)us);
 CHECK(us==1 || us==300 || us==5000000);
 if (us==5000000) EQ(mCore.terminal_attempted,1u);
 uint64_t ticks=((uint64_t)us*m.frequency+999999)/1000000;
 if (!m.short_stall && !m.frozen_clock) { m.now+=ticks; m.stall_ticks+=ticks; }
 return m.stall_status;
}
static EFI_BOOT_SERVICES services={install,stall};
EFI_BOOT_SERVICES *gBS=&services;

static void reset(void)
{
 memset(&m,0,sizeof m); memset(&mCore,0,sizeof mCore);
 mUsed=0; mStartCounter=0; mFrequency=0; mScmStatus=0; mAvailability=0;
 m.frequency=1000000; m.now=1000000;
 m.version=UINT32_C(0x20010000);
 m.channel[0]=7; m.channel[1]=201;
 m.map[7]=0x00800; m.map[201]=0x20800;
 m.imem=UINT32_C(0x11223344);
 m.pon[0][0x8f]=0xb7;
 m.pon[0][0x5a]=1; m.pon[1][0x5a]=4;
 m.pon[0][0x5b]=0x80; m.pon[1][0x5b]=0x81;
}
static struct lithium_fastboot_result result_input(void)
{
 struct lithium_fastboot_result result;
 memset(&result,0xa5,sizeof result);
 result.revision=LFB_PROTOCOL_REVISION; result.size=sizeof result;
 return result;
}
#if LITHIUM_RAM_DIAGNOSTIC
static EFI_STATUS prepare(struct lithium_fastboot_result *result)
{
 *result=result_input();
 return mProtocol.prepare(&mProtocol,result);
}
#endif
static void no_hardware(void)
{
 EQ(m.reads,0u); EQ(m.writes,0u); EQ(m.queries,0u); EQ(m.halts,0u);
 EQ(m.stalls,0u); EQ(m.barriers,0u); EQ(m.counters,0u);
 EQ(m.freq_reads,0u); EQ(m.mpidr_reads,0u);
}
#if LITHIUM_RAM_DIAGNOSTIC
static void prepared(const struct lithium_fastboot_result *out)
{
 EQ(out->revision,LFB_PROTOCOL_REVISION); EQ(out->size,LFB_RESULT_SIZE);
 EQ(out->state,LFB_STATE_PREPARED); EQ(out->backend,LFB_BACKEND_LITHIUM);
 EQ(out->flags,LFB_FLAG_IMEM_VERIFIED|LFB_FLAG_PMIC_VERIFIED);
 EQ(out->imem_before,UINT32_C(0x11223344)); EQ(out->imem_after,UINT32_C(0x77665500));
 EQ(out->pmic_before,0xb7u); EQ(out->pmic_after,0x0bu);
 EQ(out->detail,0u); EQ(out->reserved,0u); EQ(out->affinity,0x101u);
 CHECK(out->ticket!=0); EQ(out->ticket,mCore.ticket);
 EQ(mCore.owner_before[0]&7u,0u); EQ(mCore.owner_before[1]&7u,0u);
 EQ(m.terminal,0u); EQ(m.halts,0u); EQ(m.queries,1u);
}
static void reject_commit(uint64_t ticket)
{
 EQ(mProtocol.commit(&mProtocol,ticket),EFI_DEVICE_ERROR);
 EQ(m.terminal,0u);
 unsigned events=m.event_count;
 EQ(mProtocol.commit(&mProtocol,ticket),EFI_ACCESS_DENIED);
 struct lithium_fastboot_result out=result_input();
 EQ(mProtocol.prepare(&mProtocol,&out),EFI_ACCESS_DENIED);
 EQ(m.event_count,events);
}
#endif

static void entry_and_arguments(void)
{
 TEST("entry installs protocol without MMIO/SMC/time reads"); reset();
 EQ(LithiumFastbootEntry(NULL,NULL),EFI_SUCCESS);
 EQ(m.installs,1u); EQ(m.installed,&mProtocol); no_hardware();
 EQ(mProtocol.revision,LFB_PROTOCOL_REVISION); EQ(mProtocol.size,sizeof mProtocol);
#if LITHIUM_RAM_DIAGNOSTIC
 EQ(mProtocol.profile,LFB_PROFILE_RAM_ISOLATED);
 EQ(mProtocol.capabilities,LFB_CAP_NO_PHYSICAL_UFS);
#else
 EQ(mProtocol.profile,LFB_PROFILE_UFS); EQ(mProtocol.capabilities,0u);
#endif
 TEST("entry propagates install failure without hardware"); reset();
 m.install_status=EFI_DEVICE_ERROR;
 EQ(LithiumFastbootEntry(NULL,NULL),EFI_DEVICE_ERROR); no_hardware();
 for (unsigned i=0;i<9;++i) {
  TEST("bad This/out ABI rejected before hardware"); reset();
  struct lithium_fastboot_result out=result_input(), copy;
  struct lithium_fastboot_protocol wrong=mProtocol;
  if (i==2) out.revision--;
  if (i==3) out.revision++;
  if (i==4) out.size=0;
  if (i==5) out.size=63;
  if (i==6) out.size=65;
  if (i==7) out.size=UINT32_MAX;
  copy=out;
  EFI_STATUS status;
  if (i==0) status=mProtocol.prepare(&wrong,&out);
  else if (i==8) status=mProtocol.prepare(NULL,&out);
  else if (i==1) status=mProtocol.prepare(&mProtocol,NULL);
  else status=mProtocol.prepare(&mProtocol,&out);
  EQ(status,EFI_INVALID_PARAMETER); EQ(memcmp(&out,&copy,sizeof out),0);
  EQ(mUsed,0u); no_hardware();
  EQ(mProtocol.commit(i==0 ? &wrong : i==8 ? NULL : &mProtocol,1),EFI_ACCESS_DENIED);
  no_hardware();
 }
}

#if LITHIUM_RAM_DIAGNOSTIC
static void success_and_tickets(void)
{
 for (unsigned availability=0;availability<2;++availability) {
  TEST("actual Prepare/query/readbacks/one-shot Commit/optional halt"); reset();
  m.availability=availability; m.record=1;
  struct lithium_fastboot_result out;
  EQ(prepare(&out),EFI_SUCCESS); prepared(&out);
  uint64_t ticket=out.ticket;
  EQ(mCore.config.qualifications,LFBR_Q_REQUIRED);
  EQ(mCore.config.owner,0u);
  EQ(mCore.config.arbiter_halt_required,availability);
  EQ(mCore.command_count,19u); EQ(m.pon_writes,7u);
  if (!availability) {
   baseline_prepare_counters=m.counters;
   baseline_prepare_commands=m.commands;
   baseline_prepare_events=m.event_count;
  }
  unsigned reads=m.reads,writes=m.writes,smcs=m.queries+m.halts;
  struct lithium_fastboot_result again=result_input(),before=again;
  EQ(mProtocol.prepare(&mProtocol,&again),EFI_ACCESS_DENIED);
  EQ(memcmp(&again,&before,sizeof again),0);
  struct lithium_fastboot_protocol wrong=mProtocol;
  EQ(mProtocol.commit(&wrong,ticket),EFI_ACCESS_DENIED);
  EQ(m.reads,reads); EQ(m.writes,writes); EQ(m.queries+m.halts,smcs);
  unsigned before_commit=m.counters;
  EQ(mProtocol.commit(&mProtocol,ticket),EFI_DEVICE_ERROR);
  EQ(m.terminal,1u); EQ(m.halts,availability);
  EQ(mCore.state,(uint32_t)LFBR_CONSUMED); EQ(mCore.ticket,0u);
  EQ(mCore.error,(uint32_t)LFBR_TERMINAL_RETURNED);
  EQ(mUsed,2u);
  /* The only post-PSHOLD mock calls are the adapter's DSB and 5s stall.
   * CNTVCT is separately counted; no further MMIO/SMC is permitted. */
  EQ(m.events_after_terminal,2u);
  EQ(m.events[m.event_count].kind,(unsigned)EV_STALL);
  EQ(m.events[m.event_count].value,5000000u);
  for (unsigned i=baseline_prepare_events+1;i<=m.event_count;++i) {
   if (m.events[i].kind==EV_WRITE && m.events[i].pa==UINT64_C(0x004ab000)) {
    for (unsigned j=i+1;j<=m.event_count;++j)
     CHECK(m.events[j].kind==EV_BARRIER || m.events[j].kind==EV_STALL);
   }
  }
  if (!availability) {
   baseline_commit_counters=m.counters-before_commit;
   baseline_all_events=m.event_count;
   memcpy(baseline_trace,m.events,sizeof baseline_trace);
  }
  unsigned calls=m.event_count;
  EQ(mProtocol.commit(&mProtocol,ticket),EFI_ACCESS_DENIED);
  EQ(mProtocol.prepare(&mProtocol,&again),EFI_ACCESS_DENIED);
  EQ(m.event_count,calls);
 }
 for (unsigned i=0;i<3;++i) {
  TEST("zero/wrong/stale ticket never reaches PSHOLD and consumes attempt"); reset();
  struct lithium_fastboot_result out; EQ(prepare(&out),EFI_SUCCESS);
  uint64_t wrong=i==0 ? 0 : out.ticket^(UINT64_C(1)<<(i*9));
  reject_commit(wrong); EQ(mCore.error,(uint32_t)LFBR_TICKET);
 }
}

static void smc_and_frequency(void)
{
 static const uint64_t avail[]={0,1,2,UINT64_C(0x100000000),UINT64_MAX};
 static const uint32_t statuses[]={0,1,UINT32_MAX,UINT32_C(0xfffffffc)};
 for (unsigned a=0;a<sizeof avail/sizeof *avail;++a)
  for (unsigned s=0;s<sizeof statuses/sizeof *statuses;++s) {
   if (a<2 && !s) continue;
   TEST("unknown/negative/error SCM availability rejects before MMIO"); reset();
   m.availability=avail[a]; m.query_error=statuses[s];
   struct lithium_fastboot_result out;
   EQ(prepare(&out),EFI_UNSUPPORTED);
   EQ(m.queries,1u); EQ(m.reads,0u); EQ(m.writes,0u); EQ(m.halts,0u);
   EQ(out.flags,0u); EQ(out.ticket,0u);
   reject_commit(1);
  }
 for (unsigned s=1;s<sizeof statuses/sizeof *statuses;++s) {
  TEST("required halt failure consumes ticket without recovery or PSHOLD"); reset();
  m.availability=1; m.halt_error=statuses[s]; m.record=1;
  struct lithium_fastboot_result out; EQ(prepare(&out),EFI_SUCCESS);
  unsigned writes=m.writes;
  reject_commit(out.ticket);
  EQ(m.halts,1u); EQ(mCore.error,(uint32_t)LFBR_HALT);
  EQ(mCore.halt_attempted,1u); EQ(mCore.halt_succeeded,0u);
  EQ(mCore.terminal_attempted,0u); EQ(mCore.ticket,0u);
  /* Five byte reads each issue polling CONFIG plus observer READ CMD,
   * followed by a final SMC. No PMIC byte or IMEM data is written. */
  EQ(m.writes,writes+10u);
  EQ(m.events[m.event_count].kind,(unsigned)EV_SMC_HALT);
 }
 TEST("availability query expires without register access"); reset();
 m.query_ticks=2000001;
 struct lithium_fastboot_result out;
 EQ(prepare(&out),EFI_UNSUPPORTED); EQ(m.reads,0u); EQ(m.writes,0u);
 reject_commit(1);
 TEST("halt call expires: no terminal store or recovery"); reset();
 m.availability=1; m.halt_ticks=2000001;
 EQ(prepare(&out),EFI_SUCCESS); reject_commit(out.ticket);
 EQ(mCore.error,(uint32_t)LFBR_HALT); EQ(m.halts,1u);
 static const uint64_t frequencies[]={0,UINT64_C(0x100000000),UINT64_MAX};
 for (unsigned i=0;i<sizeof frequencies/sizeof *frequencies;++i) {
  TEST("invalid frequency prevents SCM/MMIO"); reset(); m.frequency=frequencies[i];
  EQ(prepare(&out),EFI_UNSUPPORTED); EQ(m.queries,0u); EQ(m.reads,0u); EQ(m.writes,0u);
  reject_commit(1);
 }
 TEST("counter wraps after prepare start and rejects query"); reset();
 m.now=UINT64_MAX;
 EQ(prepare(&out),EFI_UNSUPPORTED); EQ(m.reads,0u); EQ(m.writes,0u);
}

static void ownership_and_core_errors(void)
{
 for (unsigned n=0;n<2;++n) for (unsigned owner=1;owner<8;++owner) {
  TEST("hardware EE owner low3 mismatch before data mutation"); reset();
  m.owner[m.channel[n]]=UINT32_C(0xabc00000)|owner;
  struct lithium_fastboot_result out;
  EQ(prepare(&out),EFI_DEVICE_ERROR); EQ(mCore.error,(uint32_t)LFBR_OWNER);
  EQ(m.reads,258u+n*3u); EQ(m.writes,0u);
  EQ(out.flags,0u); EQ(out.ticket,0u); reject_commit(1); ++rejected_owners;
 }
 TEST("owner register upper bits preserved; only sourced low3 EE0 tested"); reset();
 m.owner[7]=UINT32_C(0xfffffff8); m.owner[201]=UINT32_C(0xa5a5a5a0);
 struct lithium_fastboot_result out;
 EQ(prepare(&out),EFI_SUCCESS); prepared(&out);
 EQ(mCore.owner_before[0],UINT32_C(0xfffffff8));
 EQ(mCore.owner_before[1],UINT32_C(0xa5a5a5a0));
 for (unsigned n=0;n<2;++n) {
  TEST("ownership changes after preparation: commit refused"); reset();
  EQ(prepare(&out),EFI_SUCCESS);
  m.owner[m.channel[n]]=1; reject_commit(out.ticket); ++rejected_owners;
 }
 for (unsigned n=0;n<2;++n) {
  TEST("ownership changes during preparation: no false prepared flags"); reset();
  m.owner_flip_command=6; m.owner_flip_target=n;
  EQ(prepare(&out),EFI_DEVICE_ERROR);
  EQ(out.flags,0u); EQ(out.ticket,0u); reject_commit(1); ++rejected_owners;
 }
 for (unsigned n=0;n<2;++n) {
  TEST("ownership changes after last byte read: final route check refuses commit"); reset();
  EQ(prepare(&out),EFI_SUCCESS);
  m.owner_flip_command=baseline_prepare_commands+5; m.owner_flip_target=n;
  reject_commit(out.ticket);
  EQ(mCore.error,(uint32_t)LFBR_OWNER); ++rejected_owners;
 }
 for (unsigned n=0;n<2;++n) {
  TEST("mapping changes after last byte read: final route check refuses commit"); reset();
  EQ(prepare(&out),EFI_SUCCESS);
  m.map_flip_command=baseline_prepare_commands+5; m.map_flip_target=n;
  reject_commit(out.ticket);
  EQ(mCore.error,(uint32_t)LFBR_CHANNEL);
 }
 for (unsigned i=0;i<11;++i) {
  TEST("actual core discovery/snapshot rejection never commits"); reset();
  switch(i) {
   case 0:m.version=0;break;
   case 1:m.version=UINT32_C(0x2000ffff);break;
   case 2:m.version=UINT32_C(0x30000000);break;
   case 3:m.map[7]=0;break;
   case 4:m.map[201]=0;break;
   case 5:m.map[100]=0x00800;break;
   case 6:m.map[101]=0x20800;break;
   case 7:m.cfg[0][0]=1;break;
   case 8:m.cfg[1][1]=1;break;
   case 9:m.pon[0][0x5b]=0;break;
   case 10:m.pon[1][0x5b]=0;break;
  }
  EQ(prepare(&out),EFI_DEVICE_ERROR);
  EQ(out.state,LFB_STATE_FAILED); EQ(out.flags,0u); EQ(out.ticket,0u);
  EQ((uint32_t)out.detail,mCore.error);
  EQ((uint32_t)(out.detail>>32),mCore.recovery_error);
  CHECK(mCore.error!=LFBR_OK); reject_commit(1);
 }
}

static void partial_and_status_errors(void)
{
 static const uint32_t statuses[]={0,2,3,4,5,8,9,0x10,0x80000001u,0xffffffffu};
 struct lithium_fastboot_result out;
 for (unsigned command=1;command<=baseline_prepare_commands;++command)
  for (unsigned s=0;s<sizeof statuses/sizeof *statuses;++s) {
   TEST("partial SPMI command status failure never reports/commits ready"); reset();
   m.fault_command=command; m.fault_status=statuses[s];
   EQ(prepare(&out),EFI_DEVICE_ERROR);
   EQ(out.flags,0u); EQ(out.ticket,0u); EQ(out.state,LFB_STATE_FAILED);
   CHECK(mCore.error==LFBR_TIMEOUT || mCore.error==LFBR_SPMI_STATUS);
   EQ((uint32_t)out.detail,mCore.error);
   reject_commit(1); ++injected_statuses;
  }
 static const unsigned checked_commands[]={7,9,11,13,15,17,19};
 for (unsigned i=0;i<sizeof checked_commands/sizeof *checked_commands;++i) {
  TEST("exact byte readback corruption prevents prepare publication"); reset();
  m.corrupt_data_command=checked_commands[i];
  EQ(prepare(&out),EFI_DEVICE_ERROR); EQ(mCore.error,(uint32_t)LFBR_VERIFY);
  EQ(out.flags,0u); EQ(out.ticket,0u); reject_commit(1);
 }
 TEST("IMEM readback corruption prevents prepare publication"); reset();
 m.corrupt_imem=1;
 EQ(prepare(&out),EFI_DEVICE_ERROR); EQ(mCore.error,(uint32_t)LFBR_VERIFY);
 EQ(out.flags,0u); EQ(out.ticket,0u); reject_commit(1);
 for (unsigned i=0;i<4;++i) {
  TEST("stall error/short delay/frozen timer prevents prepared result"); reset();
  if (i==0) m.stall_status=EFI_DEVICE_ERROR;
  if (i==1) m.short_stall=1;
  if (i==2) m.frozen_clock=1;
  if (i==3) { m.busy_polls=1; m.short_stall=1; m.frozen_clock=1; }
  EQ(prepare(&out),EFI_DEVICE_ERROR);
  EQ(out.flags,0u); EQ(out.ticket,0u); reject_commit(1);
 }
 for (unsigned i=0;i<7;++i) {
  TEST("changed prepared hardware/core state cannot reach terminal store"); reset();
  EQ(prepare(&out),EFI_SUCCESS);
  switch(i) {
   case 0:m.imem^=1;break;
   case 1:m.pon[0][0x8f]^=1;break;
   case 2:m.pon[0][0x5a]=1;break;
   case 3:m.pon[1][0x5b]=0;break;
   case 4:m.map[7]^=1;break;
   case 5:m.version^=1;break;
   case 6:mCore.config.qualifications=0;break;
  }
  reject_commit(out.ticket);
 }
}

static void budget_sweeps(void)
{
 struct lithium_fastboot_result out;
 for (unsigned counter=2;counter<=baseline_prepare_counters;++counter) {
  TEST("expire each actual prepare counter checkpoint; no commit"); reset();
  m.expire_at=counter;
  CHECK(EFI_ERROR(prepare(&out)));
  EQ(m.expired,1u); EQ(out.flags,0u); EQ(out.ticket,0u);
  reject_commit(1); ++prepare_expiries;
 }
 for (unsigned counter=2;counter<=baseline_prepare_counters;++counter) {
  TEST("rewind each actual prepare counter checkpoint; no commit"); reset();
  m.rewind_at=counter;
  CHECK(EFI_ERROR(prepare(&out)));
  EQ(m.expired,1u); EQ(out.flags,0u); EQ(out.ticket,0u);
  reject_commit(1); ++prepare_expiries;
 }
 /* Commit starts its own documented operation budget; no wall-clock TTL is
  * specified for tickets. Inject every checkpoint AFTER that new start.
  * Expiry after the actual PSHOLD write cannot undo an effectful store. */
 for (unsigned counter=2;counter<=baseline_commit_counters;++counter) {
  TEST("expire actual commit checkpoints and preserve effect boundary"); reset();
  EQ(prepare(&out),EFI_SUCCESS); m.expire_at=m.counters+counter;
  EQ(mProtocol.commit(&mProtocol,out.ticket),EFI_DEVICE_ERROR);
  EQ(m.expired,1u);
  if (m.terminal) {
   EQ(m.terminal,1u);
   CHECK(m.terminal_counter<m.expire_at);
  }
  unsigned events=m.event_count;
  EQ(mProtocol.commit(&mProtocol,out.ticket),EFI_ACCESS_DENIED);
  EQ(m.event_count,events); ++commit_expiries;
 }
}
#endif

static void adapter_boundaries(void)
{
 /* Direct calls exercise the actual adapter callbacks, not an imitation.
  * They are private in firmware; this TU has access solely for host tests. */
 static const uint64_t bad_writes[]={
  0,0x004ab004,0x066bf658,0x066bf660,0x0400a700,0x0400f000,
  0x04400008,0x0440000c,0x04400018,0x04408008,0x04c00008,
  0x04c00010,0x04c00018,0x04bffffc,0x05400000,0x04400001,
  0x075b5000,0x01000000,UINT64_MAX
 };
 for (unsigned i=0;i<sizeof bad_writes/sizeof *bad_writes;++i) {
  TEST("actual write callback rejects addresses outside exact whitelist"); reset();
  mFrequency=m.frequency; mStartCounter=m.now;
  EQ(Write32(NULL,bad_writes[i],0),0); no_hardware();
 }
 for (unsigned n=0;n<256;++n) for (unsigned o=0;o<2;++o)
  for (unsigned k=0;k<(o ? 2u : 3u);++k) {
   TEST("all channel whitelist offsets are explicit; no unrelated MMIO"); reset();
   m.raw_probe=1; mFrequency=m.frequency; mStartCounter=m.now;
   uint64_t base=o ? UINT64_C(0x04c00000) : UINT64_C(0x04400000);
   static const unsigned offsets[]={0,4,0x10};
   uint64_t pa=base+UINT64_C(0x8000)*n+offsets[k];
   EQ(Write32(NULL,pa,0),1); EQ(m.writes,1u); EQ(m.last_pa,0u);
  }
 static const uint64_t bad_reads[]={0,0x04009ffc,0x0400d000,0x04010000,
   0x04400001,0x05400000,0x066bf658,0x066bf660,0x004ab000,0x075b5000,UINT64_MAX};
 for (unsigned i=0;i<sizeof bad_reads/sizeof *bad_reads;++i) {
  TEST("actual read callback rejects unmapped/unaligned addresses"); reset();
  mFrequency=m.frequency; mStartCounter=m.now;
  uint32_t value=UINT32_C(0xdecafbad);
  EQ(Read32(NULL,bad_reads[i],&value),0); EQ(value,UINT32_C(0xdecafbad));
  no_hardware();
 }
 TEST("null read output does not access hardware"); reset();
 EQ(Read32(NULL,0x0400f000,NULL),0); no_hardware();
 TEST("delay over one millisecond rejects before timer/boot services"); reset();
 EQ(DelayUs(NULL,1001),0); no_hardware();
}

int main(int argc, char **argv)
{
 if (argc==2 && strcmp(argv[1],"--repro-read32-output")==0) {
  /* Diagnose the documented read32 output-on-success-only contract.
   * Nonzero exit is the reproduced violation, not a sanitizer crash. */
  reset(); mFrequency=m.frequency; mStartCounter=m.now; m.expire_at=2;
  uint32_t value=UINT32_C(0xdecafbad),before=value;
  int ok=Read32(NULL,UINT64_C(0x0400f000),&value);
  printf("{\"callback_return\":%d,\"output_before\":%u,\"output_after\":%u,"
         "\"mmio_reads\":%u,\"deadline_expired\":%u,\"contract_violated\":%s}\n",
         ok,before,value,m.reads,m.expired,(!ok && value!=before) ? "true" : "false");
  if (ok || m.reads!=1 || m.expired!=1) return 2;
  return !ok && value!=before ? 1 : 0;
 }
 entry_and_arguments();
#if LITHIUM_RAM_DIAGNOSTIC
 success_and_tickets(); smc_and_frequency(); ownership_and_core_errors();
 partial_and_status_errors(); budget_sweeps();
#else
 TEST("normal UFS/default-off prepare is denied without hardware"); reset();
 struct lithium_fastboot_result out=result_input(),copy=out;
 EQ(mProtocol.prepare(&mProtocol,&out),EFI_ACCESS_DENIED);
 EQ(memcmp(&out,&copy,sizeof out),0); EQ(mUsed,0u); no_hardware();
 EQ(mProtocol.commit(&mProtocol,1),EFI_ACCESS_DENIED); no_hardware();
#endif
 adapter_boundaries();
 if (baseline_all_events) {
  for (unsigned i=1;i<=baseline_all_events;++i)
   printf("TRACE %u %u 0x%llx 0x%x\n",i,baseline_trace[i].kind,
      (unsigned long long)baseline_trace[i].pa,baseline_trace[i].value);
 }
 printf("{\"checks\":%lu,\"scenarios\":%lu,\"prepare_deadline_faults\":%lu,"
  "\"commit_deadline_faults\":%lu,\"spmi_status_faults\":%lu,"
  "\"owner_rejections\":%lu,\"smc_args_verified\":%lu,"
  "\"prepare_counter_checkpoints\":%u,\"commit_counter_checkpoints\":%u,"
  "\"prepare_spmi_commands\":%u,\"profile\":%u}\n",
  checks,scenarios,prepare_expiries,commit_expiries,injected_statuses,
  rejected_owners,verified_smcs,baseline_prepare_counters,
  baseline_commit_counters,baseline_prepare_commands,mProtocol.profile);
 return 0;
}
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--cc", default="clang")
    ap.add_argument("--run-label", default="latest", help="Evidence subdirectory")
    args = ap.parse_args()
    if not args.run_label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in args.run_label):
        ap.error("--run-label must contain only letters, digits, underscore or hyphen")
    if os.name == "nt" and not args.native:
        drive, tail = os.path.splitdrive(str(Path(__file__).resolve()))
        linux = "/mnt/" + drive[0].lower() + tail.replace("\\", "/")
        return subprocess.run(
            ["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--exec", "python3",
             linux, "--native", "--cc", args.cc, "--run-label", args.run_label],
            check=False,
        ).returncode
    run_dir = OUT / args.run_label
    run_dir.mkdir(parents=True, exist_ok=True)
    mock = run_dir / "mock-uefi"
    for name, text in HEADERS.items():
        path = mock / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text.lstrip(), encoding="utf-8")
    tmp = run_dir / "tmp"
    tmp.mkdir(exist_ok=True)
    env = os.environ.copy()
    for var in ("TMPDIR", "TMP", "TEMP"):
        env[var] = str(tmp)
    env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    source = run_dir / "firmware-tests.c"
    source.write_text(TEST.lstrip(), encoding="utf-8")
    inputs = [DRIVER, DRIVER.with_suffix(".inf"), CORE, PROTOCOL, Path(__file__).resolve()]
    hashes_before = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    report = {
        "status": "running",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "production_source_included_directly": str(DRIVER.relative_to(ROOT)),
        "source_sha256": hashes_before,
        "mock_header_sha256": {str(p.relative_to(ROOT)): digest(p) for p in sorted(mock.rglob("*.h"))},
        "harness_sha256": digest(source),
        "sanitizer_runs": [],
        "cross_compiles": [],
        "diagnostics": [],
        "commands": [],
        "limitations": [
            "Host model only: no physical MMIO/SCM/reset, ownership, timer, USB or Fastboot measured.",
            "Directly includes actual production C and actual reset-core.h; only platform/UEFI services are mocked.",
            "Sparse memory enforces the address/byte-command oracle but does not simulate electrical hardware.",
            "UEFI mock service layout is minimal and is not evidence for real firmware linking or image packaging.",
            "Normal-return/finalization/USB qualification is a trusted loader caller precondition, not measured by DXE.",
            "Private adapter callbacks and BSS state are visible in this test translation unit for fault injection.",
            "The 2-second limit is per Prepare/Commit operation; no separate ticket lifetime is specified.",
            "Expiry after a real MMIO write cannot undo its side effects, including a returned terminal write.",
            "No assertion that a bounded timer can interrupt a permanently hung MMIO or SMC.",
            "No device/USB/ADB/Fastboot/QEMU commands, no production writes, no Git index updates.",
        ],
    }
    transcript: list[str] = []

    def run(argv: list[str], *, log_name: str | None = None,
            accepted_codes: tuple[int, ...] = (0,)) -> str:
        cp = subprocess.run(argv, cwd=ROOT, env=env, text=True, encoding="utf-8",
                            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            check=False, timeout=180)
        report["commands"].append({"argv": argv, "exit_code": cp.returncode})
        transcript.append("$ " + shlex.join(argv) + "\n" + cp.stdout)
        if log_name:
            (run_dir / log_name).write_text(cp.stdout, encoding="utf-8")
        if cp.returncode not in accepted_codes:
            raise RuntimeError(f"command failed ({cp.returncode}): {shlex.join(argv)}\n{cp.stdout[-14000:]}")
        return cp.stdout

    try:
        report["compiler"] = run([args.cc, "--version"]).splitlines()[0]
        strict = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                  "-Wconversion", "-Wshadow", "-Wvla", "-I", str(mock), "-I", str(ROOT)]
        for profile, define in (
            ("ram", ["-DLITHIUM_RAM_DIAGNOSTIC=1"]),
            ("normal", ["-DLITHIUM_RAM_DIAGNOSTIC=0"]),
            ("default-off", []),
        ):
            for opt in ("-O1", "-O2"):
                name = f"firmware-tests-{profile}-{opt[1:]}"
                exe = run_dir / name
                run([args.cc, *strict, *define, opt, "-g", "-fno-omit-frame-pointer",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                     str(source), "-o", str(exe)], log_name=name + "-compile.log")
                output = run([str(exe)], log_name=name + ".log")
                result = json.loads(output.strip().splitlines()[-1])
                result.update({"optimization": opt, "profile_build": profile,
                               "executable_sha256": digest(exe),
                               "output_sha256": digest(run_dir / (name + ".log"))})
                report["sanitizer_runs"].append(result)
                print(f"ASan/UBSan {profile} {opt}: {result['scenarios']} scenarios, "
                      f"{result['checks']} checks", flush=True)
        repro = json.loads(run([str(run_dir / "firmware-tests-ram-O1"), "--repro-read32-output"],
                              log_name="read32-output-contract-repro.json", accepted_codes=(0, 1)))
        report["diagnostics"].append({
            "name": "read32_output_on_failed_post_mmio_budget",
            "severity": "P3",
            "status": "open" if repro["contract_violated"] else "fixed",
            "production_file": str(DRIVER.relative_to(ROOT)),
            "observed": repro,
            "impact": "Historical callback contract issue; fixed iff the failed read preserves the sentinel. No erroneous commit was observed before the fix.",
        })
        if repro["contract_violated"]:
            raise RuntimeError("Read32 post-budget output contract regression reproduced.")
        # A freestanding compile of the production C, rather than the harness.
        # Real EDK2 linking is outside this independent host review.
        for label, target in [
            ("aarch64-elf", "--target=aarch64-none-elf"),
            ("aarch64-uefi", "--target=aarch64-unknown-windows-msvc"),
        ]:
            obj = run_dir / ("LithiumFastbootDxe-" + label + ".o")
            run([args.cc, target, *strict, "-DLITHIUM_RAM_DIAGNOSTIC=1", "-O2",
                 "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-c", str(DRIVER), "-o", str(obj)],
                log_name=label + "-compile.log")
            report["cross_compiles"].append({"target": label, "object_sha256": digest(obj)})
        # Verify declarations against the available actual Mu headers too.
        # These are read-only external inputs, not copies or edited firmware.
        mu = Path("/home/jia/ios-mix1-build/Mu-Silicium")
        mde = mu / "Mu_Basecore/MdePkg/Include"
        if mde.is_dir():
            actual_include = run_dir / "actual-uefi-include/ReturnFastboot"
            actual_include.mkdir(parents=True, exist_ok=True)
            for name in ("firmware-protocol.h", "reset-core.h"):
                (actual_include / name).write_text(HEADERS["ReturnFastboot/" + name], encoding="utf-8")
            real_headers = [mde / "Uefi.h", mde / "Library/ArmLib.h",
                            mde / "Library/ArmSmcLib.h", mde / "Library/IoLib.h",
                            mde / "AArch64/ProcessorBind.h"]
            report["actual_mu_header_sha256"] = {str(p): digest(p) for p in real_headers}
            obj = run_dir / "LithiumFastbootDxe-actual-mu-headers.o"
            run([args.cc, "--target=aarch64-none-elf", "-std=c11", "-Wall", "-Wextra",
                 "-Werror", "-Wno-pragma-pack", "-Wno-unknown-pragmas",
                 "-I", str(mde), "-I", str(mde / "AArch64"),
                 "-I", str(mu / "Mu_Basecore/ArmPkg/Include"),
                 "-I", str(actual_include.parent), "-I", str(ROOT),
                 "-DLITHIUM_RAM_DIAGNOSTIC=1", "-O2", "-fshort-wchar",
                 "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                 "-c", str(DRIVER), "-o", str(obj)],
                log_name="actual-mu-headers-compile.log")
            report["cross_compiles"].append({
                "target": "aarch64-elf-actual-mu-headers", "object_sha256": digest(obj),
                "uses_uefi_stubs": False, "linked": False,
            })
        hashes_after = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
        report["source_sha256_after"] = hashes_after
        if hashes_before != hashes_after:
            raise RuntimeError("Production inputs or runner changed during this run; rerun a stable revision.")
        report["status"] = "passed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr, flush=True)
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (run_dir / "test-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (run_dir / "test-output.txt").write_text("\n".join(transcript), encoding="utf-8")
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
