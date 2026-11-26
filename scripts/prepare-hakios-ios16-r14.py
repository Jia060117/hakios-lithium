"""Preserve consumed R13 and fix only its CPU diagnostic logical-ID bound."""
from pathlib import Path
import subprocess
import zipfile
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec);hakios.resource_check()
    work=hakios.workspace(spec);hakios.check_common(work,hakios.read(work/'ios-mix1-workspace.json'))
    hakios.require(hakios.read(hakios.ROOT/'reports/hakios-ios16-device-r13.json')['panic_verified'],
        'Recorded R13 panic required before revision')
    port=hakios.ROOT/spec['port_path'];entry=port/'diagnostics/cpu-sanity.inc';record=port/'diagnostics/r14.json'
    if not record.exists():
        prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r13/contract.json')
        hakios.require(hakios.sha(entry)==prior['inputs']['port_files']['diagnostics/cpu-sanity.inc'],
            'CPU diagnostic differs from frozen consumed R13')
        branch='archive/ios16-r13-before-r14-20261118'
        if subprocess.run(['git','show-ref','--verify','--quiet','refs/heads/'+branch],cwd=hakios.ROOT).returncode:
            subprocess.run(['git','branch',branch],cwd=hakios.ROOT,check=True)
        backup=Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/r13-before-r14-20261118');backup.mkdir(parents=True,exist_ok=True)
        archive=backup/'dirty-source.zip'
        if not archive.exists():
            names=subprocess.check_output(['git','ls-files','-m','-o','--exclude-standard','-z'],cwd=hakios.ROOT).decode().split('\0')
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
                for name in dict.fromkeys(filter(None,names)):
                    p=hakios.ROOT/name
                    if p.is_file():z.write(p,name)
                z.writestr('common-r13.diff',hakios.common_diff(work))
                z.writestr('xnu-r13.diff',subprocess.check_output(['git','diff','--binary','HEAD'],cwd=work/'xnu-src/xnu-8796.121.2'))
        before=hakios.sha(entry);old=entry.read_text()
        needle='    for(unsigned i=0;i<4;i++) {\n        processor_t p=cpu_to_processor((int)i);'
        hakios.require(old.count(needle)==1,'Expected exact R13 diagnostic loop')
        new=old.replace('#include <kern/processor.h>','#include <kern/processor.h>\n#include <machine/machine_routines.h>',1)
        new=new.replace(needle,
            '    /* cpu_to_processor asserts before returning for an out-of-range ID.\n'
            '     * The firmware storage worker is not an XNU logical processor. */\n'
            '    const int max_cpu=ml_get_max_cpu_number();\n'
            '    for(unsigned i=0;max_cpu>=0 && i<=(unsigned)max_cpu;i++) {\n'
            '        processor_t p=cpu_to_processor((int)i);',1)
        (backup/'cpu-sanity-r13.inc').write_text(old)
        entry.write_text(new,encoding='utf-8',newline='\n')
        hakios.write(record,dict(revision=14,base_revision=13,before_cpu_sha256=before,
            after_cpu_sha256=hakios.sha(entry),backup_branch=branch,backup_path=str(archive),backup_sha256=hakios.sha(archive),
            runtime_change='Bound CPU diagnostic IDs by inclusive ml_get_max_cpu_number before cpu_to_processor',
            fixed_physical_cpu_limit_removed=True,diagnostic_workload_unchanged=True,
            cpu_count_policy_changed=False,clock_policy_changed=False,cache_policy_changed=False,
            hfs_policy_changed=False,flush_policy_changed=False,userdata_reinstall_required=False,
            hardware_verified=False))
    c=hakios.read(record);hakios.require(hakios.sha(entry)==c['after_cpu_sha256'],'Reviewed R14 CPU source differs')
    hakios.prepare(spec,source)
    print('Preserved R13 and prepared R14 logical CPU diagnostic bound; no phone command')

if __name__=='__main__':main()
