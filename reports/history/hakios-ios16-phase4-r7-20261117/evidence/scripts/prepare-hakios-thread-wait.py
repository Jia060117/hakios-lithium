#!/usr/bin/env python3
"""Generate the exact R7 board diagnostic delta; preserve upstream and R6 inputs."""
import hashlib
from pathlib import Path
import hakios
import hakios_boot_observer
import hakios_thread_wait_observer as observer


def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.workspace(spec)
    marker = hakios.read(work/'ios-mix1-workspace.json')
    hakios.check_common(work, marker)
    trace_name = 'xnu/files/osfmk/arm64/user_exception_trace.c'
    fb_name = 'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    trace = (work/trace_name).read_text()
    fb = (work/fb_name).read_text()
    patch = hakios.ROOT/spec['port_path']/'patches/0008-lithium-thread-wait-profile.patch'
    config = hakios.ROOT/spec['port_path']/'diagnostics/thread-wait-observer.json'
    if patch.exists():
        contract = hakios.read(config)
        hakios.require(trace == observer.trace_source((source/trace_name).read_text()), 'Existing R7 trace differs')
        hakios.require(hashlib.sha256(fb.encode()).hexdigest() == contract['after_sha256'][fb_name], 'Existing R7 framebuffer differs')
    else:
        hakios.require(trace == observer.r6_trace_source((source/trace_name).read_text()),
            'R6 trace must match pinned upstream plus its reviewed independent fatal-trace gate')
        base6 = hakios.read(hakios.ROOT/spec['port_path']/'diagnostics/boot-observer.json')
        base_fb = hakios_boot_observer.framebuffer_base(fb)
        hakios.require(hashlib.sha256(base_fb.encode()).hexdigest() == base6['before_sha256'][fb_name], 'R6 framebuffer boundary differs')
        after_trace = observer.trace_source(trace)
        after_fb = observer.framebuffer_source(fb)
        patch.write_text(hakios_boot_observer.patch_for(trace_name, trace, after_trace) +
            hakios_boot_observer.patch_for(fb_name, fb, after_fb), encoding='utf-8', newline='\n')
        hakios.write(config, dict(revision=7, base_revision=6,
            before_sha256={trace_name: hashlib.sha256(trace.encode()).hexdigest(), fb_name: hashlib.sha256(fb.encode()).hexdigest()},
            after_sha256={trace_name: hashlib.sha256(after_trace.encode()).hexdigest(), fb_name: hashlib.sha256(after_fb.encode()).hexdigest()},
            process='SpringBoard', related_processes=['backboardd','lsd','containermanagerd','runningboardd'],
            first_kernel_seconds=180, interval_ms=360000, max_samples=8, max_threads_per_process=96,
            max_processes_per_sample=8, frames_per_thread=14, top_enabled=True,
            framebuffer_profile_swaps=8, framebuffer_max_bands=8, framebuffer_band_rows=512,
            raw_saved_registers=True, unfair_lock_owner_lookup='read-only normal IPC/thread-read conversion; sampled value may change',
            scheduling_time_separated=True, kernel_memory_mib=2880, xnu_cpus=1,
            lock_owner_or_desktop_hardware_verified=False))
    hakios.prepare(spec, source)
    print('Prepared finite R7 HakiOS thread/dependency and framebuffer CPU diagnostics; no phone operation')


if __name__ == '__main__':
    main()
