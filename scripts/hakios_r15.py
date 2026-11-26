"""Reviewed Run A: preserve numbering, select phys1, extend observation and sync Data."""
from hakios_boot_observer import once

PORT_DELTA={'patches/0015-lithium-run-a-finalize.patch','diagnostics/r15.json','diagnostics/lsdb-finalize.inc'}

def return_source(text):
    return once(text,'\t\thakios_var_save_on_return();   /* the data volume kept, while the system is still whole */',
        '#if defined(LITHIUM_ARMV8_0)\n\t\textern void lithium_lsdb_finalize(void);\n\t\tlithium_lsdb_finalize();\n#endif\n'
        '\t\thakios_var_save_on_return();   /* the data volume kept, while the system is still whole */')

def xnu_source(name,text):
    if name=='bsd/kern/bsd_init.c':
        return once(text,'\tlithium_cpu_sanity();','\tlithium_cpu_sanity();\n\textern void lithium_run_a_cpu(void);\n\tlithium_run_a_cpu();')
    if name=='bsd/vfs/vfs_syscalls.c':
        return text+'\n#if defined(LITHIUM_ARMV8_0)\n#include <kern/clock.h>\n#include <machine/machine_routines.h>\n#include <machine/cpu_number.h>\n#include <pexpert/pexpert.h>\n#include "../../../../devices/lithium/diagnostics/lsdb-finalize.inc"\n#endif\n'
    raise RuntimeError('Unexpected Run A patch target: '+name)

def command_line(prior):
    for old in ('bootcpu=2','cpus=3','loader_return_after=600'):
        if prior.split().count(old)!=1:raise RuntimeError('Frozen R14 argument missing/duplicated: '+old)
    return prior.replace('bootcpu=2','bootcpu=1').replace('cpus=3','cpus=1').replace('loader_return_after=600','loader_return_after=10800')

def command_budget(cmdline,version):
    from hakios_service_wait_observer import command_budget as budget
    words=cmdline.split()
    for w in ('bootcpu=1','cpus=1','loader_return_after=10800'):
        if words.count(w)!=1:raise RuntimeError('Run A requires '+w)
    return budget(cmdline,version)

def matches_prior(work,name,digest):
    import hakios
    # Walk explicit before->after chains, never accept a generic dirty file.
    for revision in (13,15,16,17,18):
        path=work/f'devices/lithium/diagnostics/r{revision}.json'
        if path.exists():
            c=hakios.read(path)
            if c['before_sha256'].get(name)==digest:digest=c['after_sha256'][name]
    return hakios.sha(work/name)==digest
