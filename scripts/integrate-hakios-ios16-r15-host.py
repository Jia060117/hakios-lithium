"""One-time mechanical registration of reviewed Run A in the host build gates."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def edit(name,pairs):
    p=ROOT/name;t=p.read_text(encoding='utf-8')
    for old,new in pairs:
        if t.count(old)!=1:raise RuntimeError(f'{name}: expected one {old!r}, got {t.count(old)}')
        t=t.replace(old,new,1)
    p.write_text(t,encoding='utf-8',newline='\n')

def main():
    edit('scripts/check-hakios-ios16-build.py',[
        ('import hakios_r13','import hakios_r13\nimport hakios_r15'),
        ("            expected.add('diagnostics/r14.json')","            expected.add('diagnostics/r14.json')\n        if 'diagnostics/r15.json' in delta:\n            expected.update(hakios_r15.PORT_DELTA)")])
    edit('scripts/freeze-hakios-ios16-boot-userspace.py',[
        ('(3,4,5,6,7,8,9,10,11,12,13,14)','(3,4,5,6,7,8,9,10,11,12,13,14,15)')])
    edit('scripts/test-hakios-ios16-boot-observer.py',[
        ('assert(lithium_sb_sample_allowed(119,3599999)&&!lithium_sb_sample_allowed(120,1)&&!lithium_sb_sample_allowed(0,3600000));',
         'assert(lithium_sb_sample_allowed(LITHIUM_SB_SAMPLES-1,LITHIUM_SB_LIMIT_MS-1)&&!lithium_sb_sample_allowed(LITHIUM_SB_SAMPLES,1)&&!lithium_sb_sample_allowed(0,LITHIUM_SB_LIMIT_MS));'),
        ('120 samples/3600s bounds','reviewed finite sample/time bounds')])
    edit('scripts/package-hakios-ios16.py',[
        ('import hakios_r13','import hakios_r13\nimport hakios_r15'),
        ('(1,2,3,4,5,6,7,8,9,10,11,12,13,14)','(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)'),
        ("    if revision==14:expected_changes.add('diagnostics/r14.json')","    if revision>=14:expected_changes.add('diagnostics/r14.json')\n    if revision>=15:expected_changes.update(hakios_r15.PORT_DELTA)"),
        ('    if revision in (13,14):\n        checks=', '    if revision in (13,14,15):\n        checks='),
        ('    if revision==14:\n        checks=', '    if revision>=14:\n        checks='),
        ("        if args.cpus==1:\n            primary=", "        if args.cpus==1 and revision<15:\n            primary="),
        ('    budgeter=hakios_r13 if revision in (13,14) else (hakios_r9 if revision>=9 else service)',
         '''    if revision==15:
        checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-r15-checks.json')
        hakios.require(checks['passed'] and checks['inputs']==current and checks['actual_production_functions'],
            'Reviewed Run A finalizer, mapping and sync failure checks required')
        for name,digest in checks['evidence_sha256'].items():
            hakios.require(hakios.sha(Path(name))==digest,'Run A evidence changed: '+name)
        prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r14/contract.json')
        cmdline=hakios_r15.command_line(prior['cmdline'])
    budgeter=hakios_r15 if revision==15 else (hakios_r13 if revision in (13,14) else (hakios_r9 if revision>=9 else service))'''),
        ('    if revision==14:\n        for name in', '    if revision>=14:\n        for name in'),
        ('    contract=dict(schema=1', '''    if revision==15:
        for name in [*('ports/hakios-lithium/'+p for p in sorted(hakios_r15.PORT_DELTA)),
            'scripts/hakios_r15.py','scripts/prepare-hakios-ios16-r15.py','scripts/test-hakios-ios16-r15.py',
            'scripts/run-hakios-ios16-r15-regressions.py','reports/hakios-ios16-r15-checks.json']:
            evidence[name]=hakios.sha(hakios.ROOT/name)
    contract=dict(schema=1'''),
        ("    if revision in (13,14) and args.cpus==1:hakios.write", '''    if revision==15:
        contract.update(run_a=True,xnu_cpus=1,bootcpu=1,expected_affinity='0x1',
            observation_seconds=12600,observation_policy=dict(initial_seconds=10800,after_first_frame_seconds=0,
                maximum_kernel_seconds=10800,finalization_after_timer=True,host_grace_seconds=1800,first_frame_rearms_once_only=False),
            database_finalize_contract='ports/hakios-lithium/diagnostics/r15.json',hardware_verified=False)
    if revision in (13,14) and args.cpus==1:hakios.write''')])
    p=ROOT/'scripts/run-wsl-limited.ps1';t=p.read_text()
    names=('prepare','checks','review','regressions','userspace','build-check','freeze','package','package-checks')
    t=t.replace("'kernel', 'native-package'",', '.join("'hakios-r15-"+n+"'" for n in names)+", 'kernel', 'native-package'",1)
    a=t.index("if ($Task -like 'hakios-r14-*') {");b=t.index("if ($Task -like 'hakios-r12-*') {",a)
    t=t[:b]+t[a:b].replace('r14','r15')+t[b:]
    t=t.replace("if ($Task -notlike 'hakios-r14-*'", "if ($Task -notlike 'hakios-r15-*' -and $Task -notlike 'hakios-r14-*'",1)
    needle="if ($Task -eq 'hakios-r14-package') { $arguments += @('--cpus', \"$XnuCpus\") }"
    if t.count(needle)!=1:raise RuntimeError('R14 task arguments not found')
    t=t.replace(needle,needle+"\nif ($Task -in @('hakios-r15-userspace', 'hakios-r15-build-check')) { $arguments += '--phase4' }\nif ($Task -in @('hakios-r15-freeze', 'hakios-r15-package')) { $arguments += @('--revision', '15') }",1)
    p.write_text(t,encoding='utf-8',newline='\n')
    print('Registered Run A revision and exact provenance gates')

if __name__=='__main__':main()
