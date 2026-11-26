"""Prepare the minimal display profile correction, without accessing the phone."""
import hashlib
from pathlib import Path
import hakios
import hakios_r16
from hakios_boot_observer import patch_for


def main():
    spec = hakios.read(hakios.MANIFEST); source = hakios.policy(spec); hakios.resource_check()
    work = hakios.workspace(spec)
    hakios.check_common(work, hakios.read(work/'ios-mix1-workspace.json'))
    port = hakios.ROOT/spec['port_path']; contract = port/'diagnostics/r16.json'
    if not contract.exists():
        backup = hakios.read(Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/r15-before-r16-20261118/backup.json'))
        hakios.require(backup['kernel_fingerprint'] == hakios.kernel_source_fingerprint(work), 'R15 source differs from backup')
        names = ['xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp', 'userland/src/inject_sb.c']
        before = {n:(work/n).read_text(encoding='utf-8') for n in names}
        after = {names[0]:hakios_r16.framebuffer_source(before[names[0]]),
                 names[1]:hakios_r16.springboard_source(before[names[1]])}
        (port/'patches/0016-lithium-display-profile.patch').write_text(
            ''.join(patch_for(n, before[n], after[n]) for n in names), encoding='utf-8', newline='\n')
        digest = lambda text:hashlib.sha256(text.encode()).hexdigest()
        args = hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')['cmdline']
        hakios.write(contract, dict(revision=16, base_revision=15,
            before_sha256={n:digest(t) for n,t in before.items()}, after_sha256={n:digest(t) for n,t in after.items()},
            backup=backup, payload_boot_args=hakios_r16.command_line(args), boot_args_unchanged=True,
            bootcpu=1, cpus=1, render=[750,1334], panel=[1080,2040], panel_stride=3240,
            panel_format='BGR24', surface_format='BGRA32', automatic_scale=2, automatic_subtype=1334,
            intended_points=[375,667], explicit_overrides_preserved=True, compositor_changed=False,
            dtype_before=dict(chosen_scale=3,artwork_scale=3,subtype=2436),
            userspace_samples=3, userspace_window_limit=6, mg_queries=6,
            hardware_verified=False, dock_verified=False, labels_verified=False, content_bounds_verified=False,
            userdata_reinstall_required=False, clock_policy_changed=False, flush_policy_changed=False))
    hakios.prepare(spec, source)
    print('R16 prepared: exact R15 boot arguments, coherent implicit 750x1334@2 profile; no phone access')


if __name__ == '__main__': main()
