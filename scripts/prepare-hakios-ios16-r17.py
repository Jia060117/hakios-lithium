"""Prepare the native D22AP display-profile correction from frozen R16."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import hakios
import hakios_r17
from hakios_boot_observer import patch_for


def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.workspace(spec)
    hakios.check_common(work, hakios.read(work/'ios-mix1-workspace.json'))
    port = hakios.ROOT/spec['port_path']
    contract_path = port/'diagnostics/r17.json'
    overlay_path = 'devices/lithium/diagnostics/display-userspace-r17.inc'
    source_path = 'userland/src/inject_sb.c'
    patch = port/'patches/0017-lithium-native-display-profile.patch'
    r16_port = hakios.read(port/'diagnostics/r16.json')
    r16 = hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r16/contract.json')
    r16_ready = hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-ready.json')
    hakios.require(r16_ready['revision'] == 16 and r16_ready['passed'] and
        r16_ready == hakios.read(hakios.ROOT/r16_ready['frozen_contract_path']),
        'R17 must derive from the exact frozen R16 package')
    hakios.require(r16_ready['image_sha256'] == r16['image_sha256'] and
        r16['cmdline'] == r16_ready['cmdline'],
        'Frozen R16 package/source chain differs')
    current = (work/'devices/lithium/diagnostics/display-userspace.inc').read_text(encoding='utf-8')
    expected = (port/'diagnostics/display-userspace.inc').read_text(encoding='utf-8')
    hakios.require(current == expected, 'R16 display overlay differs from reviewed source')
    hakios.require(hakios.sha(port/'diagnostics/display-userspace.inc') ==
        r16['inputs']['port_files']['diagnostics/display-userspace.inc'],
        'R16 display overlay is not the source recorded by its frozen package')
    overlay_after = hakios_r17.userspace_source(current)
    source_current = (work/source_path).read_text(encoding='utf-8')
    source_before = source_current
    r16_source_sha = r16_port['after_sha256']['userland/src/inject_sb.c']
    if hashlib.sha256(source_current.encode()).hexdigest() != r16_source_sha:
        prior_contract = hakios.read(contract_path) if contract_path.exists() else {}
        hakios.require(prior_contract.get('after_sha256',{}).get(source_path) ==
            hashlib.sha256(source_current.encode()).hexdigest() and patch.exists(),
            'Refusing to reconstruct an unknown SpringBoard source state')
        with tempfile.TemporaryDirectory(prefix='r17-source-reconstruct-') as temp:
            base = Path(temp)
            target = base/source_path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source_current, encoding='utf-8', newline='\n')
            subprocess.run(['git','apply','--reverse',str(patch)],cwd=base,check=True)
            source_before = target.read_text(encoding='utf-8')
    hakios.require(hashlib.sha256(source_before.encode()).hexdigest() ==
        r16_source_sha,
        'R16 SpringBoard source differs from its recorded output')
    source_after = hakios_r17.springboard_source(source_before)
    generated_patch = patch_for(source_path, source_before, source_after)
    contract = dict(revision=17, base_revision=16,
        before_sha256={source_path:hashlib.sha256(source_before.encode()).hexdigest(),
            'devices/lithium/diagnostics/display-userspace.inc':hashlib.sha256(current.encode()).hexdigest()},
        after_sha256={source_path:hashlib.sha256(source_after.encode()).hexdigest(),
            overlay_path:hashlib.sha256(overlay_after.encode()).hexdigest()},
        patch='patches/0017-lithium-native-display-profile.patch',
        source_chain=dict(r16_port_contract_sha256=hakios.sha(port/'diagnostics/r16.json'),
            r16_frozen_contract_sha256=hakios.sha(hakios.ROOT/r16_ready['frozen_contract_path']),
            r16_image_sha256=r16_ready['image_sha256'],
            r16_display_overlay_sha256=hakios.sha(port/'diagnostics/display-userspace.inc')) ,
        backup_branch='archive/ios16-r16-failed-before-r17-20261118',
        backup_archive='E:/IOS-MIX1-artifacts/ios16-d22ap/r16-before-r17-20261118/dirty-source.zip',
        backup_sha256='af64f09da6ac23e2d08b87a20da446a9211a3647bac4f6de580d689477426003',
        render=[1125,2436], native_scale=3, artwork_subtype=2436, logical_points=[375,812],
        panel=[1080,2040], panel_stride=3240, panel_format='BGR24',
        surface_format='BGRA32', expected_statusbar_profile=[3,2436],
        payload_from_r16=r16['cmdline'], payload_boot_args=hakios_r17.command_line(r16['cmdline']),
        only_boot_argument_changes=['fbwidth=750->1125','fbheight=1334->2436'],
        cpu_ufs_ram_hfs_policy_unchanged=True, userdata_reinstall_required=False,
        hardware_verified=False, dock_verified=False, labels_verified=False, bounds_verified=False)
    incomplete = True
    if contract_path.exists():
        existing = hakios.read(contract_path)
        incomplete = any(existing.get(key) != contract[key] for key in
            ('before_sha256','after_sha256','patch','source_chain','payload_from_r16','payload_boot_args'))
    overlay_path_host = port/'diagnostics/display-userspace-r17.inc'
    if overlay_path_host.exists():
        hakios.require(hakios.sha(overlay_path_host) in (contract.get('after_sha256',{}).get(overlay_path,''),
            hashlib.sha256(overlay_after.encode()).hexdigest()), 'Refusing to overwrite a modified R17 display include')
    if incomplete or not patch.exists() or patch.read_text(encoding='utf-8') != generated_patch or not overlay_path_host.exists():
        patch.write_text(generated_patch, encoding='utf-8', newline='\n')
        overlay_path_host.write_text(overlay_after, encoding='utf-8', newline='\n')
        hakios.write(contract_path, contract)
    hakios.prepare(spec, source)
    print('R17 prepared from R16: native 1125x2436@3 D22AP profile; no device access')


if __name__ == '__main__': main()
