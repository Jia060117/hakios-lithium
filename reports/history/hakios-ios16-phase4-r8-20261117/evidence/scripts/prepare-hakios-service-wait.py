#!/usr/bin/env python3
"""Prepare reviewed R8 incrementally; leave archived R7 contract untouched."""
import hashlib
from pathlib import Path
import hakios
import hakios_boot_observer as base
import hakios_service_wait_observer as observer


def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.workspace(spec)
    hakios.check_common(work, hakios.read(work/'ios-mix1-workspace.json'))
    port = hakios.ROOT/spec['port_path']
    contract_path = port/'diagnostics/service-wait-observer.json'
    patch = port/'patches/0009-lithium-service-wait-bgr24.patch'
    names = ['xnu/files/osfmk/arm64/user_exception_trace.c',
        'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp', 'xnuloader/loader.c']
    if patch.exists():
        contract = hakios.read(contract_path)
        for name in names:
            hakios.require(hakios.sha(work/name) == contract['after_sha256'][name], 'Existing R8 source differs: '+name)
    else:
        r7 = hakios.read(port/'diagnostics/thread-wait-observer.json')
        before = {name:(work/name).read_text() for name in names}
        for name in names[:2]:
            hakios.require(hashlib.sha256(before[name].encode()).hexdigest() == r7['after_sha256'][name], 'R7 base differs: '+name)
        after = dict(zip(names, [observer.trace_source(before[names[0]]),
            observer.framebuffer_source(before[names[1]]), observer.loader_source(before[names[2]])]))
        patch.write_text(''.join(base.patch_for(name, before[name], after[name]) for name in names), encoding='utf-8', newline='\n')
        hakios.write(contract_path, dict(revision=8, base_revision=7,
            before_sha256={n:hashlib.sha256(before[n].encode()).hexdigest() for n in names},
            after_sha256={n:hashlib.sha256(after[n].encode()).hexdigest() for n in names},
            process='SpringBoard', related_processes=observer.RELATED.split(','),
            first_kernel_seconds=180, interval_ms=360000, max_samples=8, max_threads_per_process=96,
            max_processes_per_sample=8, frames_per_thread=14, top_enabled=True,
            proc_name_api='proc_best_name', synchronous_console_drain=True,
            ipc_receiver_lookup='ipc_port_get_receiver_task_locked; sampled current name mapping, not historical RPC proof',
            bgr24_packing_pixels=4, pixel_algorithm_unchanged=True, kernel_memory_mib=2880, xnu_cpus=1,
            desktop_hardware_verified=False))
    hakios.prepare(spec, source)
    print('Prepared R8 complete HakiOS diagnostics and pixel-identical BGR24 packing; no device operation')


if __name__ == '__main__':
    main()
