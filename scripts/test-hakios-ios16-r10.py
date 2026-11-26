#!/usr/bin/env python3
"""Execute actual initCPU and deferred diagnostics under the console IRQ policy."""
from pathlib import Path
import importlib.util
import subprocess
import hakios
import hakios_r10


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    port = hakios.ROOT / spec['port_path']
    review = hakios.read(port / 'diagnostics/r10.json')
    name = 'xnu/files/iokit/Kernel/GenericARMPE.cpp'
    after = (work / name).read_text()
    hakios.require(hakios.sha(work / name) == review['after_sha256'][name], 'Actual R10 CPU source differs')
    # Reconstruct the exact R9 input without changing either workspace.
    before = subprocess.check_output(['git', 'show', 'archive/ios16-r9-20261117:scripts/hakios_r9.py'],
                                     cwd=hakios.ROOT, text=True)
    hakios.require(before == (hakios.ROOT / 'scripts/hakios_r9.py').read_text(), 'Frozen R9 transformer changed')
    baseline = hakios.read(port / 'diagnostics/r9.json')
    original = subprocess.check_output(['git', 'show', 'HEAD:' + name], cwd=work, text=True)
    import hakios_r9
    original = hakios_r9.pe_source(original)
    import hashlib
    hakios.require(hashlib.sha256(original.encode()).hexdigest() == baseline['after_sha256'][name], 'Reconstructed R9 differs')
    hakios.require(hakios_r10.pe_source(original) == after, 'R10 changes more than the reviewed delta')
    inc_path = port / 'diagnostics/cpu-online.inc'
    inc = inc_path.read_text()
    hakios.require(inc == (work / 'devices/lithium/diagnostics/cpu-online.inc').read_text(), 'Diagnostic include differs')
    module_spec = importlib.util.spec_from_file_location('r9_test_helpers', hakios.ROOT / 'scripts/test-hakios-ios16-r9.py')
    extract = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(extract)
    current_init = extract.function(after, 'GenericARMCPU::initCPU')
    old_init = extract.function(original, 'GenericARMCPU::initCPU')
    hakios.require('IOLog(' not in current_init and 'lithium_cpu_online_record(' in current_init,
                   'initCPU must record without console I/O')
    # The callback is an ordinary thread_call, not a timer/interrupt handler.
    hakios.require('thread_call_allocate([](thread_call_param_t, thread_call_param_t) {\n\t\tlithium_cpu_online_dump();' in after,
                   'Deferred output is not wired into the normal thread callback')
    prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define LITHIUM_ARMV8_0 1
static bool irq_enabled;
static unsigned prints, state_calls, signals, running, selected;
static uint64_t regs[3];
static bool ml_get_interrupts_enabled(void) { return irq_enabled; }
static void IOLog(const char *fmt, ...) {
    if (!irq_enabled) { fputs("EXPECTED console policy rejection\n",stderr); exit(73); }
    assert(strstr(fmt,"LITHIUM CPU online")); prints++;
    va_list ap;va_start(ap,fmt);
    unsigned logical=va_arg(ap,unsigned),phys=va_arg(ap,unsigned);
    assert(logical<4 && phys==(logical==0?0x100:logical==1?1:0x101));
    assert(va_arg(ap,unsigned long long)==phys);
    assert(va_arg(ap,unsigned long long)==0x510f2052ULL);
    assert(va_arg(ap,unsigned long long)==19200000ULL);
    assert(va_arg(ap,unsigned)==(logical==0)); va_end(ap);
}
static uint64_t host_register(const char *name) {
    assert(!irq_enabled);
    return !strcmp(name,"MPIDR_EL1")?regs[0]:!strcmp(name,"MIDR_EL1")?regs[1]:regs[2];
}
#define __builtin_arm_rsr64(name) host_register(name)
enum { kIOCPUStateRunning=4 };
static unsigned ml_get_cpu_count(void) { return 3; }
static void psci_smp_cpu_running(void) { assert(!irq_enabled); running++; }
static void gic_v3_irq_signal(unsigned phys) { assert(!irq_enabled && phys==selected);signals++; }
class GenericARMCPU {
public:
    unsigned logical,physID;
    unsigned getCPUNumber(void) { return logical; }
    void setCPUState(unsigned state) { assert(!irq_enabled && state==4);state_calls++; }
    void initCPU(bool boot);
};
'''
    tests = r'''
int main(void) {
    GenericARMCPU cpu;
    regs[1]=0x510f2052;regs[2]=19200000;
    for(unsigned logical=0;logical<3;logical++) {
        cpu.logical=logical;cpu.physID=logical==0?0x100:logical==1?1:0x101;
        selected=cpu.physID;regs[0]=cpu.physID;
        irq_enabled=false;cpu.initCPU(logical==0);
        assert(!irq_enabled && !prints);
    }
    assert(state_calls==3 && running==3 && signals==3);
    lithium_cpu_online_dump(); assert(!prints && !irq_enabled);
    irq_enabled=true;lithium_cpu_online_dump();assert(prints==3 && irq_enabled);
    lithium_cpu_online_dump();assert(prints==3);
    /* Reinitialization cannot rewrite published records; no IRQ or scheduler change. */
    irq_enabled=false;regs[0]=0xdead;cpu.initCPU(false);
    assert(lithium_cpu_online[2].mpidr==0x101 && !irq_enabled && prints==3);
    lithium_cpu_online_record(UINT_MAX,0,0,0,0,0);
    lithium_cpu_online_record(4,0,0,0,0,0);
    /* An incomplete publication must not be reported, and capture never waits. */
    lithium_cpu_online[3].state=1;
    lithium_cpu_online_record(3,9,9,9,9,9);
    irq_enabled=true;lithium_cpu_online_dump();assert(prints==3);
    puts("PASS actual initCPU/deferred dump: IRQ policy, 3 records, duplicate/partial/out-of-range, scheduler hooks preserved");
}
'''
    folder = work / 'build/r10-host-checks'
    folder.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, revision=10, inputs=hakios.inputs(spec), checks=[],
                  actual_production_functions=True, hardware_verified=False,
                  sanitizers=['address', 'undefined'])
    for label, body, expected in [('r10-cpu-irq', inc + current_init + tests, 0),
                                  ('r9-negative-control', old_init + '\nint main(){ GenericARMCPU cpu;cpu.logical=0;cpu.physID=0x100;cpu.initCPU(true); }\n', 73)]:
        source = folder / (label + '.cpp')
        binary = folder / label
        source.write_text(prefix + body)
        subprocess.run(['clang++', '-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        hakios.require(result.returncode == expected, label + ' failed: ' + result.stdout + result.stderr)
        if expected:
            hakios.require('EXPECTED console policy rejection' in result.stderr, 'Wrong negative-control failure')
        report['checks'].append(dict(name=label, passed=True, returncode=result.returncode,
            stdout=result.stdout, stderr=result.stderr, harness_sha256=hakios.sha(source)))
        print(label + ': PASS')
    console = work / 'xnu-src/xnu-8796.121.2/osfmk/console/serial_console.c'
    hakios.require('panic("Console I/O from interrupt-disabled context");' in console.read_text(), 'Console IRQ policy removed')
    evidence = [Path(__file__), Path(hakios_r10.__file__), inc_path, work/name,
                port/'diagnostics/r10.json', console]
    report.update(passed=True, console_policy_preserved=True, unchanged_r9_rejected=True,
                  evidence_sha256={str(p):hakios.sha(p) for p in evidence},
                  limitations=['Host stubs verify the actual call contract, not real SMP/GIC/cache/interrupt timing'])
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r10-checks.json', report)


if __name__ == '__main__':
    main()
