#!/usr/bin/env python3
"""Create the MIX board overlay for the complete Mu UEFI / HakiOS loader.

Only frozen hardware libraries and board drivers are reused as sources.
The BDS and boot shim are new HakiOS-only components.
"""
import shutil
import hakios

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.frozen_check()
    original = hakios.ROOT / 'firmware/lithiumPkg'
    dest = hakios.ROOT / 'ports/hakios-lithium/firmware/lithiumPkg'
    records = {}
    for source in sorted(original.rglob('*')):
        if not source.is_file() or 'LithiumBdsDxe' in source.parts or source.name == 'MemoryMap.c':
            continue
        relative = source.relative_to(original)
        target = dest / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        content = source.read_text(encoding='utf-8')
        if source.name == 'Payload.h':
            content = content.replace('0x44d00000UL', '0x42a00000UL')
        if source.name == 'lithium.dsc':
            content = content.replace('All allocatable RAM stays in the stock Image window.',
                'Resident UEFI stays in the stock Image window; the XNU window has an allocation HOB.')
            import re
            content = re.sub(r'!ifdef LITHIUM_QEMU\n.*?!endif\n', '', content, flags=re.S) if 'CC_FLAGS' in content else content
            # Restore the real GIC PCDs after removing the conditional block.
            content = content.replace('  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29',
                '  gArmTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000\n'
                '  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000\n'
                '  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29')
        if source.name == 'PlatformSec.c':
            content = content.replace('#ifndef LITHIUM_QEMU\n', '').replace('#endif\n', '')
            content = content.replace('LITHIUM UEFI', 'HAKIOS UEFI')
        content = content.replace('Host QEMU can inspect them directly.', 'A photograph is required for runtime evidence.')
        target.write_text(content, encoding='utf-8', newline='\n')
        records[source.relative_to(hakios.ROOT).as_posix()] = hakios.sha(source)
    hakios.write(hakios.ROOT / 'reports/hakios-firmware-sources.json', dict(
        mu_commit='6b3d90b79e191e08171bfedd3980234a7e49d340', reused_hardware_sources=records,
        reused_legacy_kernel_runtime=False, new_components=['HakiOS BDS', 'HakiOS BootShim', 'HakiOS memory ownership map'],
        qemu_used=False, hardware_verified=False))

if __name__ == '__main__':
    main()
