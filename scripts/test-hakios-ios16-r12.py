#!/usr/bin/env python3
"""Exercise actual raw benchmark, restoration failures, stats and all write gates."""
from pathlib import Path
import subprocess
import hakios
import hakios_r12
import hakios_r13

TEST=r'''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "LithiumStorage.h"
#include "raw-storage-core.h"
static uint8_t disk[LR_N][LR_B],original[LR_N][LR_B],saved[LR_N][LR_B],buf[LR_B],expected[LR_B];
static uint64_t clock_ns, calls, writes;
static uint64_t fail_at,corrupt_at,slow_at;
static int deny;
static uint64_t now(void *ctx) { (void)ctx;return clock_ns; }
static int io(void *ctx,unsigned op,uint64_t lba,uint8_t *data,uint64_t *service) {
    (void)ctx;assert(lithium_scratch_range(lba,1));
    assert(lithium_write_range(LITHIUM_STORAGE_FIRST+lba,1));
    unsigned i=(unsigned)(lba-LITHIUM_SCRATCH_FIRST);assert(i<LR_N);
    calls++;clock_ns+=100000+(calls==slow_at?121000000000ULL:0);*service=70000;
    if (deny && op==2) return 5;
    if (calls==fail_at) { if(op==2)disk[i][0]^=255;return 5; }
    if(op==0) { memcpy(data,disk[i],LR_B);if(calls==corrupt_at)data[0]^=255; }
    else if(op==2) { memcpy(disk[i],data,LR_B);writes++; }
    else assert(op==3);
    return 0;
}
static void reset(void) {
    for(unsigned i=0;i<LR_N;i++)for(unsigned j=0;j<LR_B;j++)disk[i][j]=(uint8_t)(i+j*7);
    memcpy(original,disk,sizeof(disk));clock_ns=calls=writes=0;fail_at=corrupt_at=slow_at=deny=0;
}
static struct lr_result run(void) {
    struct lr_result r;struct lr_ops o={0,now,io};
    lr_run(&o,&r,LITHIUM_SCRATCH_FIRST,saved,buf,expected);return r;
}
int main(void) {
    struct lr_stats s={0};lr_add(&s,30,0);lr_add(&s,10,0);lr_add(&s,20,0);lr_add(&s,99,5);
    assert(s.n==3&&s.min==10&&s.max==30&&s.sum/s.n==20&&s.errors==1);
    assert(!lithium_scratch_range(LITHIUM_SCRATCH_FIRST-1,1));
    assert(lithium_scratch_range(LITHIUM_SCRATCH_FIRST,100));
    assert(!lithium_scratch_range(LITHIUM_SCRATCH_FIRST,101));
    assert(!lithium_scratch_range(LITHIUM_SCRATCH_FIRST+99,2));
    assert(!lithium_scratch_range(LITHIUM_SCRATCH_FIRST,0));
    assert(!lithium_scratch_range(UINT64_MAX,2));
    assert(!lithium_scratch_range(LITHIUM_SCRATCH_FIRST,UINT64_MAX));
    assert(lithium_write_range(LITHIUM_WRITE_FIRST,1));
    assert(lithium_write_range(LITHIUM_WRITE_END-1,1));
    assert(!lithium_write_range(LITHIUM_WRITE_END,1));
    assert(!lithium_write_range(LITHIUM_WRITE_END-1,258));
    assert(!lithium_write_range(UINT64_MAX,1));
    reset();struct lr_result r=run();assert(r.complete&&r.verified==100&&!memcmp(disk,original,sizeof(disk)));
    assert(r.wall[LR_WRITE].n==100&&r.wall[LR_WRITE].sum/100==100000);
    assert(r.service[LR_WRITE].sum/100==70000&&r.wall[LR_DURABLE].sum/100==200000);
    assert(r.wall[LR_READ].n==100&&writes==200);
    /* Every possible failed request, including partial writes and restoration.
     * Cleanup success may be claimed only when restoration flush/readback succeeded. */
    for(int n=1;n<=602;n++) {
        reset();fail_at=n;r=run();assert(!r.complete);
        if(n<=101)assert(!writes&&!memcmp(disk,original,sizeof(disk)));
        else if(n<=401)assert(r.restored==r.touched&&r.verified==r.touched&&!memcmp(disk,original,sizeof(disk)));
        else assert(r.restored<100||r.verified<100||r.restore_flush_error);
    }
    reset();corrupt_at=302;r=run();assert(!r.complete&&r.mismatches==1&&r.verified==100&&!memcmp(disk,original,sizeof(disk)));
    reset();slow_at=102;r=run();assert(!r.complete&&r.timed_out&&r.verified==1&&!memcmp(disk,original,sizeof(disk)));
    reset();deny=1;r=run();assert(!r.complete&&r.restored==0&&r.touched==1);
    puts("PASS actual raw engine: stats, bounds, nominal restoration, 602 injected failures, corruption, timeout, denied writes");
}
'''

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
    port=hakios.ROOT/spec['port_path'];contract=hakios.read(port/'diagnostics/r12.json')
    files=[Path(__file__),Path(hakios_r12.__file__),port/'diagnostics/raw-storage-core.h',
        port/'diagnostics/raw-storage.inc',port/'firmware/lithiumPkg/Include/LithiumStorage.h',port/'diagnostics/r12.json',
        port/'patches/0013-lithium-raw-storage.patch']
    for name,digest in contract['after_sha256'].items():
        hakios.require(hakios_r13.matches_prior(work,name,digest),'R12 exact deployed source or reviewed R13: '+name);files.append(work/name)
    kernel=(work/'drivers/storage/hakios.ufs.c').read_text();loader=(work/'xnuloader/loader.c').read_text()
    hakios.require(kernel.count('lithium_raw_storage_if_requested();')==4,
        'All three init failures and success must stay in the diagnostic, before launchd')
    hakios.require('!(block_rawbench && lithium_scratch_range(lba,count))' in loader and
        '(c[14] == 0 || c[14] == \' \')' in loader,'Loader opt-in guard missing')
    hakios.require('offset / 4096 >= UFS_LITHIUM_END' in kernel,'Ordinary kernel write boundary changed')
    actual=work/'xnu-src/xnu-8796.121.2/bsd/dev/hakios.ufs.c'
    hakios.require(hakios.sha(actual)==hakios.sha(work/'drivers/storage/hakios.ufs.c'),'Actual XNU raw diagnostic not synchronized')
    files.append(actual)
    for name in ['raw-storage-core.h','raw-storage.inc']:
        deployed=work/'devices/lithium/diagnostics'/name
        hakios.require(hakios.sha(deployed)==hakios.sha(port/'diagnostics'/name),'Include not synchronized');files.append(deployed)
    # Extract actual loader acceptance expression and exercise normal/opt-in cases.
    begin=loader.index('if (block_write_part < 0 || part != (uint32_t)block_write_part || lba < BLOCK_WRITE_FIRST')
    end=loader.index(') {',begin)+1
    guard=loader[begin+3:end]
    gate='''\n#define HAKIOS_LITHIUM 1\n#define BLOCK_WRITE_FIRST LITHIUM_DATA_FIRST\n#define BLOCK_LITHIUM_END LITHIUM_IMAGE_BLOCKS
static int gate(int block_rawbench, uint64_t lba,uint64_t count) { int block_write_part=0;uint32_t part=0;return !'''+guard+'''; }
static void gate_tests(void) {
assert(!gate(0,LITHIUM_SCRATCH_FIRST,1));assert(gate(1,LITHIUM_SCRATCH_FIRST,100));
assert(!gate(1,LITHIUM_SCRATCH_FIRST,101));assert(!gate(1,LITHIUM_IMAGE_BLOCKS,1));
assert(!gate(1,LITHIUM_DATA_FIRST-1,1));assert(gate(0,LITHIUM_DATA_FIRST,1));
assert(!gate(1,LITHIUM_IMAGE_BLOCKS-1,258));assert(!gate(1,UINT64_MAX,1)); }
'''
    code=TEST.replace('int main(void) {',gate+'\nint main(void) { gate_tests();')
    out=work/'build/r12-host-tests';out.mkdir(parents=True,exist_ok=True)
    src=out/'raw.c';src.write_text(code)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(src),
        '-I'+str(port/'diagnostics'),'-I'+str(port/'firmware/lithiumPkg/Include'),'-o',str(out/'raw')],check=True)
    result=subprocess.run([str(out/'raw')],capture_output=True,text=True,check=True);print(result.stdout,end='')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r12-checks.json',dict(passed=True,revision=12,
        inputs=hakios.inputs(spec),actual_production_functions=True,hardware_verified=False,
        failure_injection_cases=602,source_inputs_unchanged=True,evidence_sha256={str(p):hakios.sha(p) for p in files},
        stdout=result.stdout,raw_results_available=False))

if __name__=='__main__':main()
