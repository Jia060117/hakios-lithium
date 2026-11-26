#!/usr/bin/env python3
"""Host-only production-include Fastboot return integration tests.

Windows: python scripts/test-hakios-return-fastboot-integration.py
Linux: python3 scripts/test-hakios-return-fastboot-integration.py

Writes only evidence/fastboot-return-impl-20261120/integration-review/.
No device tools, Git commands, firmware copies, or production writes.
Kernel ARM instructions have three exact host substitutions, recorded in JSON.
All policy, parsing, scan, timer/caller and callback logic is production code.
"""
from __future__ import annotations

import argparse
import ast
from datetime import datetime, timezone
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "evidence/fastboot-return-impl-20261120/integration-review"
SRC = ROOT / "ports/hakios-lithium/return-fastboot"
COMMON = ROOT / "evidence/fastboot-return-impl-20261120/main"
PREP = ROOT / "scripts/prepare-hakios-return-fastboot.py"
PATCH = ROOT / "ports/hakios-lithium/patches/0023-lithium-return-fastboot.patch"
NAMES = ("xnu/files/osfmk/arm64/loader_return.c", "xnuloader/loader.c",
         "drivers/storage/hakios.ufs.c")

C_PREFIX = r'''
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <unistd.h>
#include "profile.h"

static unsigned checks, scenarios;
static const char *test_name;
#define CHECK(x) do { ++checks; if (!(x)) { \
 fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, test_name, #x); \
 exit(1); } } while (0)
#define EQ(x,y) CHECK((x)==(y))
#define TEST(s) do { test_name=(s); ++scenarios; } while (0)

static char events[16384], ktext[8192], ltext[32768];
static unsigned nevents, klen, llen;
static void ev(char c) { CHECK(nevents+1 < sizeof events); events[nevents++]=c; events[nevents]=0; }
static void text(char *s, unsigned *n, size_t cap, const char *format) {
 size_t z=strlen(format); CHECK(*n+z+1 < cap);
 memcpy(s+*n,format,z+1); *n+=(unsigned)z;
}
static uint64_t ud_fb_session, ud_fb_delivered, ud_fb_lost, ud_fb_done_session;
static uint64_t mpidr, usb_total, ticks, frequency;
static unsigned session_loads;
static bool tear_session;
static unsigned maxcpu, reg_reads, drain_calls, kernel_clean, barriers;
static unsigned locate_calls, prepare_calls, commit_calls, stalls, pets, cleans, flushes;
static unsigned retained_logs, panic_calls, quiesces, write_calls;
static uint64_t jump_reason, jump_code, drain_target[2], pre_firmware_ticks, ticket_seen;
static bool panic_flag, loader_return_entered;
static char argsbuf[4097];
static const char *boot_args;
static const char *debug_buf_base, *debug_buf_ptr;
static uint64_t lithium_log_contention_dropped_bytes;
static jmp_buf jump_env;
enum injection {
 NONE, PRIMARY_FAIL, TAIL_FAIL, PRIMARY_SHORT, TAIL_SHORT, TAIL_REGRESS,
 PRIMARY_SESSION, TAIL_SESSION, END_SESSION, PRIMARY_LOST, TAIL_LOST,
 END_LOST, END_CONTENTION, END_STORAGE, END_CPU, END_MPIDR
};
static enum injection inject;
static int clock_mode, fw_mode, locate_mode;
static bool reenter_prepare;

static void *ufs_page, *ufs_buffer;
static unsigned ufs_parts;
static bool ufs_ready, ufs_data_used, ufs_write_busy, ufs_write_out, ufs_write_was_native;
static int ufs_write_part, ufs_var_save, ufs_var_busy_pid;
static uint64_t ufs_requests, ufs_writes, ufs_write_errors, ufs_write_refused;
#define VAR_SAVE_NONE 0
#include "storage.inc"

static const char *PE_boot_args(void) { return boot_args; }
static unsigned ml_get_max_cpu_number(void) { return maxcpu; }
static bool panic_active(void) { return panic_flag; }
static uint64_t mock_mpidr(const char *name) { EQ(strcmp(name,"MPIDR_EL1"),0); ++reg_reads; return mpidr; }
static uint64_t mock_atomic_load(uint64_t *p) {
 if (p==&ud_fb_session && tear_session && ++session_loads==2) ++ud_fb_session;
 return __atomic_load_n(p,__ATOMIC_ACQUIRE);
}
#define os_atomic_load(p,order) mock_atomic_load(p)
#define os_atomic_store(p,v,order) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#define os_atomic_add(p,v,order) __atomic_add_fetch((p),(v),__ATOMIC_ACQ_REL)
#include "usb_getters.inc"
#undef os_atomic_load
#undef os_atomic_store
#undef os_atomic_add
#define session ud_fb_session
#define done_session ud_fb_done_session
#define delivered ud_fb_delivered
#define lost ud_fb_lost
static int kernel_printf(const char *fmt, ...) {
 CHECK(drain_calls<2); text(ktext,&klen,sizeof ktext,fmt); usb_total+=strlen(fmt); ev('k'); return 0;
}
static void mock_kernel_clean(uintptr_t p) { CHECK(p!=0); ++kernel_clean; ev('c'); }
static void mock_kernel_barrier(void) { ++barriers; ev('b'); }
#define __builtin_arm_rsr64 mock_mpidr
#define printf kernel_printf
#include "kernel_host.inc"
#undef printf
#undef __builtin_arm_rsr64

typedef uint64_t efi_status;
typedef struct { uint32_t a; uint16_t b,c; uint8_t d[8]; } efi_guid;
typedef struct { int dummy; } efi_block_io;
#define EFIAPI
#define EFI_ERROR(s) (((s)>>63)!=0)
#define EFI_FAIL UINT64_C(0x8000000000000007)
struct services {
 efi_status (*locate_protocol)(const efi_guid *, void *, void **);
 efi_status (*stall)(uint64_t);
};
static struct services services;
static struct services *BS=&services;
static bool block_enabled, block_write_requested, hc_requested;
static uint64_t read_sysreg(unsigned r) {
 ++reg_reads;
 if (r==1) return frequency;
 return ticks;
}
#define CNTFRQ_EL0 1
#define CNTVCT_EL0 2
#define READ_SYSREG(r) read_sysreg(r)
static void print(const char *fmt, ...) { text(ltext,&llen,sizeof ltext,fmt); ev('l'); }
static void clean_dcache(uint64_t low,uint64_t high) { CHECK(high>low); ++cleans; ev('C'); }
static void flush_dcache(uint64_t low,uint64_t high) { CHECK(high>low); ++flushes; ev('F'); }
static void wdt_pet(void) { ++pets; ev('w'); }
#include "loader.inc"

static struct lithium_fastboot_protocol firmware;
static efi_status mock_locate(const efi_guid *g,void *registration,void **out) {
 static const efi_guid want={LFB_PROTOCOL_GUID_VALUES};
 ++locate_calls; ev('L'); CHECK(memcmp(g,&want,sizeof want)==0); EQ(registration,NULL);
 *out=locate_mode==2 ? NULL : &firmware;
 return locate_mode==1 ? EFI_FAIL : 0;
}
static efi_status mock_stall(uint64_t us) {
 EQ(us,100000); ++stalls; ev('s');
 if (clock_mode==0) ticks+=frequency/10;
 else if (clock_mode==2) ticks=1;
 return 0;
}
static efi_status mock_prepare(struct lithium_fastboot_protocol *f,struct lithium_fastboot_result *r) {
 struct rfbt_record claimed;
 EQ(f,&firmware); ++prepare_calls; ev('P'); EQ(prepare_calls,1u);
 EQ(r->revision,LFB_PROTOCOL_REVISION); EQ(r->size,sizeof *r);
 EQ(rfbt_decode(lithium_fastboot_handoff,RFBT_WIRE_SIZE,&claimed),RFBT_OK);
 EQ(claimed.phase,RFBT_PHASE_CLAIMED); EQ(lithium_fastboot_once.used,1u);
 CHECK(cleans>=2); EQ(stalls,pets);
 if (reenter_prepare) lithium_fastboot_after_return(RFBT_RETURN_REASON,0);
 r->revision=LFB_PROTOCOL_REVISION; r->size=sizeof *r;
 r->state=LFB_STATE_PREPARED; r->backend=LFB_BACKEND_LITHIUM;
 r->flags=LFB_FLAG_IMEM_VERIFIED|LFB_FLAG_PMIC_VERIFIED; r->ticket=0x1234;
 switch (fw_mode) {
 case 1:return EFI_FAIL;
 case 2:r->revision++;break;
 case 3:r->size--;break;
 case 4:r->state=LFB_STATE_FAILED;break;
 case 5:r->backend=LFB_BACKEND_NONE;break;
 case 6:r->ticket=0;break;
 case 7:r->flags=LFB_FLAG_IMEM_VERIFIED;break;
 case 8:r->flags=LFB_FLAG_PMIC_VERIFIED;break;
 case 9:r->flags|=4;break;
 default:break;
 }
 pre_firmware_ticks=ticks; return 0;
}
static efi_status mock_commit(struct lithium_fastboot_protocol *f,uint64_t ticket) {
 EQ(f,&firmware); ++commit_calls; ev('M'); EQ(commit_calls,1u);
 EQ(ticket,0x1234u); ticket_seen=ticket;
 return fw_mode==10 ? EFI_FAIL : 0;
}

/* Dependencies of the exact patched kernel caller functions below. */
#define LITHIUM_ARMV8_0 1
#define HAKIOS_LITHIUM_USB_LOG 1
#define HAKIOS_LITHIUM 1
#define HAKIOS_LITHIUM_RAM_DIAGNOSTIC 1
#define __unused __attribute__((unused))
#define NSEC_PER_MSEC 1000000u
#define KERN_SUCCESS 0
typedef void *thread_call_param_t;
static bool lithium_observation_extend(void) { return false; }
static void loader_log_putc(char c) { (void)c; ++retained_logs; }
static void loader_log_puts(const char *s) { (void)s; ++retained_logs; }
static void loader_log_linearize(void) { ++retained_logs; ev('r'); }
static void loader_log_append_msgbuf(void) { ++retained_logs; }
static uint64_t lithium_log_position(void) { return usb_total; }
static bool lithium_usb_log_drain(uint64_t target,unsigned timeout) {
 unsigned i=drain_calls++; CHECK(i<2); EQ(timeout,i ? 250u : 5000u);
 drain_target[i]=target; ev(i ? '2' : '1'); delivered=target; done_session=session;
 if (!i && inject==PRIMARY_SHORT) delivered=target-1;
 if (i && inject==TAIL_SHORT) delivered=target-1;
 if (i && inject==TAIL_REGRESS) delivered=drain_target[0]-1;
 if ((!i && inject==PRIMARY_SESSION) || (i && inject==TAIL_SESSION)) ++session;
 if ((!i && inject==PRIMARY_LOST) || (i && inject==TAIL_LOST)) ++lost;
 return !((!i && inject==PRIMARY_FAIL) || (i && inject==TAIL_FAIL));
}
void lithium_usb_quiesce(void) {
 ++quiesces; ev('q');
 if (inject==END_SESSION) ++session;
 if (inject==END_LOST) ++lost;
 if (inject==END_CONTENTION) ++lithium_log_contention_dropped_bytes;
 if (inject==END_STORAGE) ufs_ready=true;
 if (inject==END_CPU) maxcpu=1;
 if (inject==END_MPIDR) mpidr=0x100;
}
#define MOCK_VOID(n) static void n(void) { ev('v'); }
MOCK_VOID(hakios_ufs_quiesce)
MOCK_VOID(hakios_adreno_quiesce)
MOCK_VOID(hakios_wifi_quiesce)
MOCK_VOID(hakios_adsp_quiesce)
MOCK_VOID(hakios_mpss_quiesce)
MOCK_VOID(hakios_ddr_quiesce)
MOCK_VOID(hakios_haptic_quiesce)
MOCK_VOID(hakios_torch_quiesce)
MOCK_VOID(hakios_camera_quiesce)
MOCK_VOID(hakios_mic_quiesce)
MOCK_VOID(lithium_lsdb_finalize)
MOCK_VOID(hakios_var_save_on_return)
MOCK_VOID(hakios_adreno_stop)
MOCK_VOID(hakios_wifi_stop)
MOCK_VOID(hakios_adsp_stop)
MOCK_VOID(hakios_mpss_stop)
static unsigned cpu_number(void) { return 0; }
static void panic(const char *s,...) { (void)s; ++panic_calls; panic_flag=true; longjmp(jump_env,2); }
static void loader_jump(uint64_t reason,uint64_t code) {
 jump_reason=reason; jump_code=code; ev('J'); longjmp(jump_env,1);
}
static bool PE_parse_boot_argn(const char *key,void *out,size_t size) {
 EQ(size,sizeof(uint32_t));
 if (!strcmp(key,"return_panic") && boot_args && strstr(boot_args,"return_panic=1")) {
  *(uint32_t *)out=1; return true;
 }
 return false;
}
static int cpu_xcall(int cpu,void (*fn)(void *),void *p) { (void)cpu; fn(p); return -1; }
static void delay_for_interval(unsigned a,unsigned b) { (void)a; (void)b; CHECK(false); }
#define printf kernel_printf
#include "kernel_callers.inc"
#undef printf

enum {CTX_LOG_PA,CTX_LOG_LEN,CTX_REASON,CTX_ESR,CTX_ELR,CTX_FAR,CTX_LR,CTX_KSP};
#define CHECKPOINT_SELFTEST UINT64_C(0xffff)
static uint64_t loader_ctx[8],checkpoint,slide;
static void set_result(const char *s,uint64_t a,uint64_t b) { (void)s; (void)a; (void)b; ev('R'); }
static void write_logs(const char *s,uint64_t n) { (void)s; (void)n; ++write_calls; ev('W'); }
#include "loader_callers.inc"
'''

C_TESTS = r'''
static const char *on="ufs=0 bootcpu=1 cpus=1 rd=md0 return_fastboot=1 return_fastboot_delay=0";
static const uint8_t initial[RFBT_WIRE_SIZE]=RFBT_WIRE_INITIALIZER;

static void reset(const char *args) {
 memset(events,0,sizeof events); nevents=klen=llen=0; ktext[0]=ltext[0]=0;
 session=17; delivered=lost=done_session=0; session_loads=0; tear_session=false;
 mpidr=0x80000001; maxcpu=0; usb_total=1000;
 ticks=100000; frequency=1000; reg_reads=drain_calls=kernel_clean=barriers=0;
 locate_calls=prepare_calls=commit_calls=stalls=pets=cleans=flushes=0;
 retained_logs=panic_calls=quiesces=write_calls=0;
 jump_reason=jump_code=pre_firmware_ticks=ticket_seen=0;
 lithium_log_contention_dropped_bytes=0; panic_flag=loader_return_entered=false;
 debug_buf_base=debug_buf_ptr=NULL; boot_args=args;
 inject=NONE; clock_mode=fw_mode=locate_mode=0; reenter_prepare=false;
 ufs_page=ufs_buffer=NULL; ufs_parts=0;
 ufs_ready=ufs_data_used=ufs_write_busy=ufs_write_out=ufs_write_was_native=false;
 ufs_write_part=-1; ufs_var_save=ufs_var_busy_pid=0;
 ufs_requests=ufs_writes=ufs_write_errors=ufs_write_refused=0;
 memcpy(lithium_fastboot_wire,initial,sizeof initial);
 memset(&lithium_fastboot_config,0,sizeof lithium_fastboot_config);
 memset(&lithium_fastboot_record,0,sizeof lithium_fastboot_record);
 lithium_fastboot_pending=false; lithium_fastboot_usb_session=0;
 memset(&lithium_fastboot_requested,0,sizeof lithium_fastboot_requested);
 memset(&lithium_fastboot_once,0,sizeof lithium_fastboot_once); /* New simulated loader invocation. */
 lithium_fastboot_handoff=NULL; lithium_fastboot_cookie=0; lithium_fastboot_firmware=NULL;
 memset(&firmware,0,sizeof firmware);
 firmware.revision=LFB_PROTOCOL_REVISION; firmware.size=sizeof firmware;
 firmware.profile=LFB_PROFILE_RAM_ISOLATED; firmware.capabilities=LFB_CAP_NO_PHYSICAL_UFS;
 firmware.prepare=mock_prepare; firmware.commit=mock_commit;
 services.locate_protocol=mock_locate; services.stall=mock_stall;
 block_enabled=block_write_requested=hc_requested=false;
 memset(loader_ctx,0,sizeof loader_ctx); checkpoint=slide=0;
}
static void seed_range(const char *args,uint32_t bytes,unsigned copies,uint64_t lo,uint64_t hi,uint64_t cp) {
 lithium_fastboot_seed(args,bytes,copies,lo,hi,cp);
}
static void seed(void) {
 seed_range(boot_args,boot_args ? (uint32_t)strlen(boot_args)+1 : 0,1,
  (uint64_t)(uintptr_t)lithium_fastboot_wire,
  (uint64_t)(uintptr_t)lithium_fastboot_wire+RFBT_WIRE_SIZE,0);
}
static int run_timer(void) {
 int j=setjmp(jump_env);
 if (!j) { loader_return_timer(NULL,NULL); CHECK(false); }
 return j;
}
static void ready(void) {
 seed(); CHECK(lithium_fastboot_handoff==lithium_fastboot_wire);
 CHECK(lithium_fastboot_cookie!=0);
 EQ(run_timer(),1); EQ(jump_reason,RFBT_RETURN_REASON); EQ(jump_code,0u);
}
static void finish(uint64_t reason,uint64_t syndrome) {
 loader_ctx[CTX_REASON]=reason; loader_ctx[CTX_ESR]=syndrome;
 EQ(host_finish_run_kernel(),1);
}
static void no_firmware(void) { EQ(prepare_calls,0u); EQ(commit_calls,0u); }
static void retained_once(unsigned commits) {
 struct rfbt_record r;
 EQ(prepare_calls,1u); EQ(commit_calls,commits);
 EQ(lithium_fastboot_once.used,1u);
 EQ(lithium_fastboot_once.result,RFBT_E_HANDOFF_RETURNED);
 EQ(rfbt_decode(lithium_fastboot_wire,RFBT_WIRE_SIZE,&r),RFBT_OK);
 EQ(r.phase,RFBT_PHASE_FAILED); EQ(r.failure,RFBT_E_HANDOFF_RETURNED);
 uint8_t keep[RFBT_WIRE_SIZE]; memcpy(keep,lithium_fastboot_wire,sizeof keep);
 for (unsigned i=0;i<3;++i) finish(RFBT_RETURN_REASON,0);
 EQ(prepare_calls,1u); EQ(commit_calls,commits);
 EQ(memcmp(keep,lithium_fastboot_wire,sizeof keep),0);
 seed(); EQ(lithium_fastboot_handoff,NULL); EQ(prepare_calls,1u);
}
static void defaultoff(void) {
 const char *off[]={"ufs=0 bootcpu=1 cpus=1 rd=md0",
  "ufs=0 bootcpu=1 cpus=1 rd=md0 return_fastboot=0",
  "ufs=0 bootcpu=1 cpus=1 rd=md0 return_fastboot_delay=10"};
 for (unsigned i=0;i<sizeof off/sizeof *off;++i) {
  TEST("default off: existing timer behavior, zero new external actions"); reset(off[i]);
  seed(); EQ(run_timer(),1); EQ(jump_reason,0u); finish(jump_reason,0);
  EQ(locate_calls,0u); EQ(reg_reads,0u); EQ(kernel_clean,0u); EQ(barriers,0u);
  EQ(cleans,0u); EQ(flushes,0u); EQ(stalls,0u); no_firmware();
  CHECK(!lithium_fastboot_pending); EQ(lithium_fastboot_cookie,0u);
  EQ(memcmp(initial,lithium_fastboot_wire,sizeof initial),0);
  EQ(drain_calls,2u); EQ(quiesces,1u); EQ(write_calls,1u);
 }
}
static void happy_and_callback(void) {
 for (unsigned m=0;m<12;++m) {
  TEST("real timer -> wire -> real loader caller -> firmware returned, no retry"); reset(on);
  fw_mode=(int)m; reenter_prepare=m==11; ready();
  struct rfbt_record r;
  EQ(rfbt_decode(lithium_fastboot_wire,RFBT_WIRE_SIZE,&r),RFBT_OK);
  EQ(r.phase,RFBT_PHASE_READY); EQ(r.boot_cookie,lithium_fastboot_cookie);
  EQ(r.primary_target,drain_target[0]); EQ(r.kernel_tail,drain_target[0]);
  EQ(r.final_target,drain_target[1]); EQ(r.tail_tail,drain_target[1]);
  CHECK(r.final_target>r.primary_target); EQ(r.storage,RFBT_STORAGE_RAM_NONE);
  EQ(kernel_clean,2u); EQ(barriers,1u);
  CHECK(strstr(events,"2q")!=NULL); CHECK(strstr(events,"cbJ")!=NULL);
  finish(jump_reason,0); EQ(write_calls,1u); CHECK(strstr(events,"WFl")!=NULL);
  const char *w=strchr(events,'W'),*p=strchr(events,'P'); CHECK(w && p && w<p);
  retained_once(m==0 || m==10 || m==11 ? 1u : 0u);
 }
}
static void kernel_negative(void) {
 for (int i=PRIMARY_FAIL;i<=END_MPIDR;++i) {
  TEST("actual timer injected drain/session/loss/storage/CPU fault"); reset(on);
  seed(); inject=(enum injection)i; EQ(run_timer(),1); EQ(jump_reason,0u);
  finish(jump_reason,0); no_firmware(); EQ(kernel_clean,0u); EQ(barriers,0u);
 }
 for (unsigned i=0;i<10;++i) {
  TEST("kernel initial cookie, CPU, session, panic and public return gate"); reset(on);
  seed();
  switch(i) {
  case 0: session=0;break;
  case 1: lost=1;break;
  case 2: maxcpu=1;break;
  case 3: mpidr=0x100;break;
  case 4: mpidr=0x100000001ULL;break;
  case 5: panic_flag=true;break;
  case 6: memset(lithium_fastboot_wire+48,0,8);break;
  case 7: lithium_fastboot_wire[20]^=1;break;
  case 8: lithium_log_contention_dropped_bytes=1;break;
  case 9: ufs_write_refused=1;break;
  }
  EQ(run_timer(),1); EQ(jump_reason,0u); finish(jump_reason,0); no_firmware();
 }
 TEST("unseeded firmware-missing kernel does not arm"); reset(on);
 EQ(run_timer(),1); EQ(jump_reason,0u); CHECK(!lithium_fastboot_pending); no_firmware();
 TEST("panic-request timer skips new preparation and drains"); reset(
  "ufs=0 cpus=1 bootcpu=1 rd=md0 return_fastboot=1 return_panic=1");
 seed(); EQ(run_timer(),2); EQ(panic_calls,1u); EQ(drain_calls,0u);
 CHECK(!lithium_fastboot_pending); finish(0,0); no_firmware();
 TEST("public panic/common return refuses even a READY production wire"); reset(on); ready();
 if (!setjmp(jump_env)) loader_return();
 EQ(jump_reason,0u); finish(jump_reason,0); no_firmware();
}
static void storage_all_fields(void) {
 for (unsigned i=0;i<16;++i) {
  TEST("actual storage.inc every independently blocking state"); reset(on);
  CHECK(lithium_return_fastboot_ram_storage_clean());
  switch(i) {
  case 0:ufs_page=(void *)1;break; case 1:ufs_buffer=(void *)1;break;
  case 2:ufs_parts=1;break; case 3:ufs_ready=true;break;
  case 4:ufs_write_part=0;break; case 5:ufs_data_used=true;break;
  case 6:ufs_write_busy=true;break; case 7:ufs_write_out=true;break;
  case 8:ufs_write_was_native=true;break; case 9:ufs_var_save=1;break;
  case 10:ufs_var_busy_pid=1;break; case 11:ufs_requests=1;break;
  case 12:ufs_writes=1;break; case 13:ufs_write_errors=1;break;
  case 14:ufs_write_refused=1;break; case 15:ufs_write_part=-2;break;
  }
  CHECK(!lithium_return_fastboot_ram_storage_clean()); seed();
  EQ(run_timer(),1); EQ(jump_reason,0u); finish(jump_reason,0); no_firmware();
 }
}
static void profile_and_protocol(void) {
 const char *bad[]={
  "ufs=1 cpus=1 bootcpu=1 rd=md0", "cpus=1 bootcpu=1 rd=md0",
  "ufs=0 cpus=2 bootcpu=1 rd=md0", "ufs=0 cpus=1 bootcpu=0 rd=md0",
  "ufs=0 cpus=1 bootcpu=1 rd=md1", "ufs=0 cpus=1 bootcpu=1 rd=md0 rd=md0",
  "ufs=0 cpus=1 bootcpu=1 ufs=0", "ufs=0 cpus=1 cpus=1 bootcpu=1",
  "ufs=0 cpus=1 bootcpu=1 bootcpu=1", "ufs=0 cpus=1 bootcpu=1 ufs_write=0",
  "ufs=0 cpus=1 bootcpu=1 var_save=0", "ufs=0 cpus=1 bootcpu=1 root_apfs=x",
  "ufs=0 cpus=1 bootcpu=1 rootdev=x", "ufs=0 cpus=1 bootcpu=1 root_device=x",
  "ufs=0 cpus=1 bootcpu=1 apfs_volume=x", "ufs=0 cpus=1 bootcpu=1 cpumask=1",
  "ufs=0 cpus=1 bootcpu=1 return_cpu=0", "ufs=0 cpus=1 bootcpu=1 return_panic=0",
  "ufs=0 cpus=1 bootcpu=1 hfs_bench=0", "ufs=0 cpus=1 bootcpu=1 fsdiag=0",
  "ufs=0 cpus=1 bootcpu=1 hfsdiag=0", "ufs=0 cpus=1 bootcpu=1 lsdbfinal=0"};
 for (unsigned i=0;i<sizeof bad/sizeof *bad;++i) {
  snprintf(argsbuf,sizeof argsbuf,"%s return_fastboot=1",bad[i]);
  TEST("loader rejects exact profile token violation before protocol lookup"); reset(argsbuf);
  seed(); EQ(lithium_fastboot_handoff,NULL); EQ(locate_calls,0u); no_firmware();
 }
 for (unsigned i=0;i<18;++i) {
  TEST("loader protocol/profile/runtime and payload metadata admission"); reset(on);
  uint32_t bytes=(uint32_t)strlen(on)+1; unsigned copies=1; uint64_t cp=0;
  const char *arg=on;
  switch(i) {
  case 0:firmware.revision++;break; case 1:firmware.size--;break;
  case 2:firmware.profile=LFB_PROFILE_UFS;break; case 3:firmware.capabilities=0;break;
  case 4:firmware.capabilities|=2;break; case 5:firmware.prepare=NULL;break;
  case 6:firmware.commit=NULL;break; case 7:locate_mode=1;break; case 8:locate_mode=2;break;
  case 9:block_enabled=true;break; case 10:block_write_requested=true;break;
  case 11:hc_requested=true;break; case 12:copies=2;break; case 13:cp=1;break;
  case 14:bytes--;break; case 15:bytes=RFBT_ARGS_MAX+1;break;
  case 16:arg=NULL;break; case 17:copies=0;break;
  }
  uint64_t lo=(uint64_t)(uintptr_t)lithium_fastboot_wire;
  seed_range(arg,bytes,copies,lo,lo+RFBT_WIRE_SIZE,cp);
  EQ(lithium_fastboot_handoff,NULL); EQ(cleans,0u);
  EQ(memcmp(initial,lithium_fastboot_wire,sizeof initial),0);
  EQ(run_timer(),1); EQ(jump_reason,0u); finish(jump_reason,0); no_firmware();
 }
 const char *invalid[]={
  "return_fastboot=2","return_fastboot=1 return_fastboot=1",
  "return_fastboot=1 return_fastboot_delay=61",
  "return_fastboot=1 return_fastboot_delay=-1","return_fastboot=1junk",
  "return_fastboot=1 return_fastboot_extra=1"};
 for (unsigned i=0;i<sizeof invalid/sizeof *invalid;++i) {
  snprintf(argsbuf,sizeof argsbuf,"ufs=0 cpus=1 bootcpu=1 rd=md0 %s",invalid[i]);
  TEST("both production peers fail closed on invalid feature args"); reset(argsbuf);
  seed(); EQ(run_timer(),1); finish(jump_reason,0);
  EQ(jump_reason,0u); EQ(locate_calls,0u); EQ(reg_reads,0u); no_firmware();
 }
 TEST("loader may omit rd before kernel adds md0, kernel requires actual md0");
 reset("ufs=0 cpus=1 bootcpu=1 return_fastboot=1"); seed();
 CHECK(lithium_fastboot_handoff!=NULL); EQ(run_timer(),1); EQ(jump_reason,0u);
 finish(jump_reason,0); no_firmware();
 TEST("RAM loader log target absent"); reset(on); EQ(find_log_partition(),NULL);
}
static void scan_candidates(void) {
 long size=sysconf(_SC_PAGESIZE); CHECK(size>=4096);
 size_t page=(size_t)size;
 uint8_t *map=mmap(NULL,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 CHECK(map!=MAP_FAILED); EQ(mprotect(map+page,page,PROT_READ|PROT_WRITE),0);
 uint8_t *mem=map+page;
 for (unsigned i=0;i<12;++i) {
  TEST("actual aligned scan with guard pages, duplicates and stale candidates"); reset(on);
  memset(mem,0,page); uint64_t lo=(uint64_t)(uintptr_t)mem,hi=lo+page;
  unsigned offset=0;
  switch(i) {
  case 0:break;
  case 1:memcpy(mem,initial,128);break;
  case 2:memcpy(mem+page-128,initial,128);offset=(unsigned)page-128;break;
  case 3:memcpy(mem+1,initial,128);break;
  case 4:memcpy(mem,initial,128);memcpy(mem+128,initial,128);break;
  case 5:memcpy(mem,initial,128);mem[48]=1;break;
  case 6:memcpy(mem,initial,128);mem[120]=1;break;
  case 7:memcpy(mem+page-128,initial,128);hi--;break;
  case 8:memcpy(mem,initial,128);lo++;break;
  case 9:hi=lo+127;break;
  case 10:hi=lo;break;
  case 11:memcpy(mem+64,initial,128);lo++;offset=64;break;
  }
  seed_range(on,(uint32_t)strlen(on)+1,1,lo,hi,0);
  if (i==1 || i==2 || i==11) {
   EQ(lithium_fastboot_handoff,mem+offset); CHECK(lithium_fastboot_cookie!=0);
   EQ(rfbt_get64(mem+offset+48),lithium_fastboot_cookie); EQ(cleans,1u);
  } else { EQ(lithium_fastboot_handoff,NULL); EQ(cleans,0u); }
  no_firmware();
 }
 EQ(munmap(map,page*3),0);
}
static void wire_context_and_boot(void) {
 for (unsigned i=0;i<RFBT_WIRE_SIZE;++i) {
  TEST("every returned production wire byte corrupted"); reset(on); ready();
  lithium_fastboot_wire[i]^=1; uint8_t keep[128]; memcpy(keep,lithium_fastboot_wire,128);
  finish(jump_reason,0); no_firmware(); EQ(stalls,0u);
  EQ(memcmp(keep,lithium_fastboot_wire,128),0);
 }
 const uint64_t reasons[]={0,1,2,3,UINT64_MAX};
 for (unsigned i=0;i<sizeof reasons/sizeof *reasons;++i) {
  TEST("independent actual loader context reason rejects READY proof"); reset(on); ready();
  finish(reasons[i],0); no_firmware(); EQ(stalls,0u);
 }
 TEST("exception syndrome rejects special reason"); reset(on); ready();
 finish(RFBT_RETURN_REASON,0x96000010); no_firmware(); EQ(stalls,0u);
 TEST("selftest retains even READY proof"); reset(on); ready();
 checkpoint=CHECKPOINT_SELFTEST; finish(2,0); no_firmware();
 TEST("cross-boot READY proof rejected by out-of-band loader cookie"); reset(on); ready();
 uint8_t old[128]; memcpy(old,lithium_fastboot_wire,128); uint64_t cookie=lithium_fastboot_cookie;
 reset(on); ticks++; seed(); CHECK(lithium_fastboot_cookie!=cookie);
 memcpy(lithium_fastboot_wire,old,128); finish(RFBT_RETURN_REASON,0);
 no_firmware(); EQ(stalls,0u);
 TEST("kernel/loader delay disagreement"); reset(on); ready();
 lithium_fastboot_requested.delay_seconds=10; lithium_fastboot_requested.seen|=RFBT_SEEN_DELAY;
 finish(RFBT_RETURN_REASON,0); no_firmware();
}
static void countdown(void) {
 const unsigned delays[]={0,1,10,60};
 for (unsigned i=0;i<sizeof delays/sizeof *delays;++i) {
  snprintf(argsbuf,sizeof argsbuf,
   "ufs=0 cpus=1 bootcpu=1 rd=md0 return_fastboot=1 return_fastboot_delay=%u",delays[i]);
  TEST("bounded monotonic post-return countdown and watchdog"); reset(argsbuf); ready();
  uint64_t start=ticks; finish(jump_reason,0);
  EQ(prepare_calls,1u); EQ(commit_calls,1u); EQ(stalls,delays[i]*10u); EQ(pets,stalls);
  CHECK(pre_firmware_ticks-start>=(uint64_t)delays[i]*frequency);
  retained_once(1);
 }
 for (unsigned i=0;i<5;++i) {
  TEST("frequency invalid or clock frozen/backwards/wrapping retains"); reset(
   "ufs=0 cpus=1 bootcpu=1 rd=md0 return_fastboot=1 return_fastboot_delay=1"); ready();
  switch(i) {
  case 0:frequency=0;break; case 1:frequency=UINT64_C(0x100000000);break;
  case 2:clock_mode=1;break; case 3:clock_mode=2;break;
  case 4:ticks=UINT64_MAX-50;break;
  }
  finish(jump_reason,0); no_firmware(); CHECK(stalls<=40); EQ(pets,stalls);
 }
}
static void getter_and_bootargs_bounds(void) {
 for (unsigned i=0;i<8;++i) {
  TEST("actual USB getters reject stale/incomplete/torn sessions"); reset(on);
  delivered=900; done_session=session; lost=43;
  switch(i) {
  case 0:EQ(lithium_usb_log_delivered(),900u);break;
  case 1:done_session=0;EQ(lithium_usb_log_delivered(),0u);break;
  case 2:done_session--;EQ(lithium_usb_log_delivered(),0u);break;
  case 3:session=done_session=0;EQ(lithium_usb_log_delivered(),0u);break;
  case 4:tear_session=true;EQ(lithium_usb_log_delivered(),0u);break;
  case 5:ud_fb_new_session();EQ(session,18u);EQ(done_session,0u);
   EQ(lithium_usb_log_delivered(),0u);break;
  case 6:session=UINT64_MAX;ud_fb_new_session();EQ(session,0u);
   EQ(lithium_usb_log_delivered(),0u);break;
  case 7:delivered=UINT64_MAX;EQ(lithium_usb_log_delivered(),UINT64_MAX);break;
  }
  EQ(lithium_usb_log_lost_total(),43u);
 }
 const unsigned lengths[]={607,608,609,4095,4096};
 for (unsigned i=0;i<sizeof lengths/sizeof *lengths;++i) {
  TEST("actual XNU 608-byte command line completion boundary"); reset(on);
  size_t n=lengths[i]; memset(argsbuf,' ',n); memcpy(argsbuf,on,strlen(on)); argsbuf[n]=0;
  boot_args=argsbuf; seed(); EQ(lithium_fastboot_handoff!=NULL,n<RFBT_ARGS_MAX);
  EQ(run_timer(),1);
  EQ(jump_reason,n<608 ? RFBT_RETURN_REASON : 0u);
  if (n>=608) { finish(jump_reason,0); no_firmware(); CHECK(!lithium_fastboot_pending); }
 }
 TEST("null kernel args while loader has seeded proof"); reset(on); seed();
 boot_args=NULL; EQ(run_timer(),1); EQ(jump_reason,0u); no_firmware();
}
int main(void) {
 defaultoff(); happy_and_callback(); kernel_negative(); storage_all_fields();
 profile_and_protocol(); scan_candidates(); wire_context_and_boot(); countdown();
 getter_and_bootargs_bounds();
 printf("{\"status\":\"passed\",\"scenarios\":%u,\"checks\":%u,"
        "\"production_units\":7,\"physical_device_operations\":0}\n",scenarios,checks);
 return 0;
}
'''


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function(text: str, name: str) -> tuple[str, int]:
    pat = r"(?m)^(?:static )?[\w *]+\n" + re.escape(name) + r"\([^;]*?\)\n\{"
    match = re.search(pat, text)
    if not match:
        raise RuntimeError("missing exact production function " + name)
    brace = match.end() - 1
    tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
                         r"'(?:\\.|[^'\\])*'|[{}]", text[brace:])
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():brace + token.end()], text.count("\n", 0, match.start()) + 1
    raise RuntimeError("unclosed production function " + name)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--cc", default="clang")
    args = ap.parse_args()
    if os.name == "nt" and not args.native:
        drive, tail = os.path.splitdrive(str(Path(__file__).resolve()))
        script = "/mnt/" + drive[0].lower() + tail.replace("\\", "/")
        return subprocess.run(["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--",
                               "python3", script, "--native", "--cc", args.cc],
                              check=False).returncode
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = BASE / "runs" / stamp
    out.mkdir(parents=True, exist_ok=False)
    tmp = out / "tmp"
    tmp.mkdir()
    for key in ("TMPDIR", "TMP", "TEMP"):
        os.environ[key] = str(tmp)
    inputs = [*[SRC / name for name in ("contract.h", "firmware-protocol.h", "kernel.inc",
                                       "loader.inc", "profile.h", "storage.inc")],
              PREP, PATCH, Path(__file__).resolve(),
              ROOT / "ports/hakios-lithium/drivers/usb/hakios.lithium_usb.c"]
    inputs += [COMMON / kind / name for kind in ("common-before", "common-after") for name in NAMES]
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in inputs if p.is_file()}
    report = {"status": "running", "started_utc": datetime.now(timezone.utc).isoformat(),
              "scope": "actual production includes and exact patched caller bodies with host mocks",
              "input_sha256": hashes, "commands": [], "sanitizer_runs": [],
              "source_checks": [], "host_substitutions": [],
              "device_operations": 0, "production_writes": 0, "git_commands": 0,
              "unverified": [
                  "Physical ARM cache/barrier execution, firmware protocol and DXE wiring",
                  "Real USB session/drain counter publication and host persistence",
                  "Actual physical-provider absence, kernel/UEFI address mapping and scan range",
                  "Hardware IMEM/PMIC readback, actual reset and Fastboot enumeration",
                  "Full production build, packaging, touch operation and device behavior"]}
    transcript: list[str] = []

    def run(command: list[str], env: dict | None = None) -> str:
        cp = subprocess.run(command, cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        report["commands"].append({"argv": command, "exit_code": cp.returncode})
        transcript.append("$ " + " ".join(command) + "\n" + cp.stdout)
        if cp.returncode:
            raise RuntimeError(f"command failed ({cp.returncode}): {command}\n{cp.stdout}")
        return cp.stdout

    def check(label: str, condition: bool):
        report["source_checks"].append({"name": label, "passed": bool(condition)})
        if not condition:
            raise RuntimeError("source integration check failed: " + label)

    try:
        # Compile only the pure transformation functions: do not run prepare.main(),
        # import hakios, touch external workspaces or regenerate the shared patch.
        tree = ast.parse(PREP.read_text(encoding="utf-8"))
        pure = ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)
                               and n.name in ("once", "kernel_source", "loader_source", "storage_source")],
                          type_ignores=[])
        transforms: dict = {}
        exec(compile(pure, str(PREP), "exec"), transforms)
        chunks = []
        after = {}
        for name, transform in zip(NAMES, ("kernel_source", "loader_source", "storage_source")):
            before = (COMMON / "common-before" / name).read_text(encoding="utf-8")
            changed = transforms[transform](before)
            expected = (COMMON / "common-after" / name).read_text(encoding="utf-8")
            check("prepare transform equals actual common-after " + name, changed == expected)
            chunks.append("".join(difflib.unified_diff(
                before.splitlines(True), changed.splitlines(True),
                fromfile="a/" + name, tofile="b/" + name)))
            after[name] = expected
        patch_text = PATCH.read_text(encoding="utf-8")
        sections = re.split(r"(?m)(?=^--- a/)", patch_text)
        selected = "".join(section for section in sections
                           if any(section.startswith("--- a/" + name + "\n") for name in NAMES))
        check("patch0023 kernel/loader/storage sections exactly equal regenerated in-memory diff",
              "".join(chunks) == selected)
        report["patch_sections_outside_scope"] = [
            section.splitlines()[0][6:] for section in sections
            if section.startswith("--- a/") and
            not any(section.startswith("--- a/" + name + "\n") for name in NAMES)]

        kernel = (SRC / "kernel.inc").read_text(encoding="utf-8")
        replacements = [
            ('__asm__ volatile ("dc cvac, %0" : : "r"(p) : "memory");',
             "mock_kernel_clean(p);"),
            ('__asm__ volatile ("dsb sy" : : : "memory");', "mock_kernel_barrier();")]
        for old, new in replacements:
            check("exact host-only ARM replacement " + old, kernel.count(old) == 1)
            report["host_substitutions"].append({
                "file": "kernel.inc", "line": kernel[:kernel.index(old)].count("\n") + 1,
                "from": old, "to": new})
            kernel = kernel.replace(old, new)
        check("no remaining kernel inline assembly", "__asm__" not in kernel)
        report["host_substitutions"].append({
            "file": "kernel.inc", "from": '__builtin_arm_rsr64("MPIDR_EL1")',
            "to": 'mock_mpidr("MPIDR_EL1") via preprocessor macro'})
        (out / "kernel_host.inc").write_text(
            '#line 1 "ports/hakios-lithium/return-fastboot/kernel.inc"\n' + kernel,
            encoding="utf-8")
        stub = out / "stubs/pexpert/arm64"
        stub.mkdir(parents=True)
        boot_header = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/"
                           "xnu-src/xnu-8796.121.2/pexpert/pexpert/arm64/boot.h")
        check("actual XNU boot header defines BOOT_LINE_LENGTH 608",
              boot_header.is_file() and re.search(
                  r"(?m)^#define\s+BOOT_LINE_LENGTH\s+608\s*$",
                  boot_header.read_text(encoding="utf-8")) is not None)
        report["external_read_only_header"] = {"path": str(boot_header),
                                              "sha256": sha(boot_header), "BOOT_LINE_LENGTH": 608}
        (stub / "boot.h").write_text("#define BOOT_LINE_LENGTH 608\n", encoding="utf-8")

        usb_path = ROOT / "ports/hakios-lithium/drivers/usb/hakios.lithium_usb.c"
        usb = usb_path.read_text(encoding="utf-8")
        usb_bodies = []
        for name in ("lithium_usb_log_session", "lithium_usb_log_delivered",
                     "lithium_usb_log_lost_total", "ud_fb_new_session"):
            body, line = function(usb, name)
            usb_bodies.append(f'#line {line} "{usb_path.relative_to(ROOT).as_posix()}"\n{body}\n')
            report["source_checks"].append({
                "name": "exact production USB observation compiled " + name, "passed": True,
                "line": line, "body_sha256": hashlib.sha256(body.encode()).hexdigest()})
        (out / "usb_getters.inc").write_text("\n".join(usb_bodies), encoding="utf-8")

        ksource, lsource = after[NAMES[0]], after[NAMES[1]]
        bodies = []
        for name in ("loader_return_common", "loader_return", "loader_return_test", "loader_return_timer"):
            body, line = function(ksource, name)
            bodies.append(f'#line {line} "{NAMES[0]}"\n{body}\n')
            report["source_checks"].append({
                "name": "exact production caller compiled " + name, "passed": True, "line": line,
                "body_sha256": hashlib.sha256(body.encode()).hexdigest()})
        (out / "kernel_callers.inc").write_text("\n".join(bodies), encoding="utf-8")
        body, line = function(lsource, "find_log_partition")
        # Preserve the complete exact final run_kernel block, including write_logs,
        # context selection, exception/checkpoint handling and the new callback.
        start = lsource.index("\tconst char *klog = (const char *)loader_ctx[CTX_LOG_PA];")
        end = lsource.index("\n\treturn 1;\n}", start) + len("\n\treturn 1;")
        finish = lsource[start:end]
        check("write_logs precedes production callback", finish.index("write_logs(") <
              finish.index("lithium_fastboot_after_return(reason, loader_ctx[CTX_ESR])"))
        (out / "loader_callers.inc").write_text(
            f'#line {line} "{NAMES[1]}"\n{body}\n'
            "static int host_finish_run_kernel(void) {\n"
            f'#line {lsource[:start].count(chr(10))+1} "{NAMES[1]}"\n{finish}\n}}\n',
            encoding="utf-8")
        check("timer prepare precedes primary drain precedes tail drain",
              ksource.index("lithium_fastboot_timer_prepare();") <
              ksource.index("lithium_fastboot_primary(target, drained);") <
              ksource.index("lithium_fastboot_tail(tail_target, tail_drained);"))
        check("actual loader seed receives actual cmdline bytes/copies/range/checkpoint",
              "lithium_fastboot_seed(pi.cmdline, pi.cmdline_size, pi.cmdline_copies,\n"
              "\t    kern_lo, kern_hi, checkpoint);" in lsource)
        source = out / "integration_tests.c"
        source.write_text(C_PREFIX.lstrip() + C_TESTS, encoding="utf-8")
        report["compiler"] = run([args.cc, "--version"]).splitlines()[0]
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        for opt in ("-O1", "-O2"):
            binary = out / ("integration-tests-" + opt[1:])
            run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function", opt, "-g", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                 "-I", str(out / "stubs"), "-I", str(out), "-I", str(SRC),
                 str(source), "-o", str(binary)])
            result = json.loads(run([str(binary)], env=env).strip())
            result["optimization"] = opt
            result["binary_sha256"] = sha(binary)
            report["sanitizer_runs"].append(result)
            print(f"ASan/UBSan {opt}: {result['scenarios']} scenarios / {result['checks']} checks", flush=True)
        check("all production inputs unchanged throughout run",
              all(sha(ROOT / path) == digest for path, digest in hashes.items()))
        check("external read-only XNU header unchanged throughout run",
              sha(boot_header) == report["external_read_only_header"]["sha256"])
        report["generated_sha256"] = {str(p.relative_to(out)): sha(p)
                                      for p in sorted(out.rglob("*")) if p.is_file()}
        report["status"] = "passed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr)
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        report["run_directory"] = str(out.relative_to(ROOT))
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        (out / "results.json").write_text(rendered, encoding="utf-8")
        (out / "commands.txt").write_text("\n".join(transcript), encoding="utf-8")
        (BASE / "latest-results.json").write_text(rendered, encoding="utf-8")
    print("Evidence: " + str(out / "results.json"), flush=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
