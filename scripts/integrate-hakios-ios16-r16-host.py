"""Register R16 while retaining prior provenance and rejection checks."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def edit(name, pairs):
    path=ROOT/name; text=path.read_text(encoding='utf-8')
    for old,new in pairs:
        if text.count(old)!=1: raise RuntimeError(f'{name}: expected one anchor {old!r}, got {text.count(old)}')
        text=text.replace(old,new,1)
    path.write_text(text,encoding='utf-8',newline='\n')


def main():
    edit('scripts/check-hakios-ios16-build.py', [
        ('import hakios_r15', 'import hakios_r15\nimport hakios_r16'),
        ('            expected.update(hakios_r15.PORT_DELTA)',
         "            expected.update(hakios_r15.PORT_DELTA)\n        if 'diagnostics/r16.json' in delta:\n            expected.update(hakios_r16.PORT_DELTA)"),
        ("    check=work/('build/phase'+str(phase)+'-validation');", "    if (work/'devices/lithium/diagnostics/r16.json').exists():\n        hakios.require(b'LITHIUM DISPLAY GEOMETRY' in kernel.read_bytes() and b'LITHIUM DISPLAY DT phase=' in kernel.read_bytes(), 'R16 display profile diagnostics absent from linked kernel')\n        sb=(work/'userland/obj/libhakios.sb.dylib').read_bytes()\n        hakios.require(b'LITHIUM DISPLAY UISCREEN' in sb and b'ScreenDimensions' in sb and b'LITHIUM DISPLAY STATUSBAR' in sb, 'R16 runtime display diagnostics absent from actual SpringBoard injection')\n    check=work/('build/phase'+str(phase)+'-validation');")])
    edit('scripts/freeze-hakios-ios16-boot-userspace.py', [
        ('(3,4,5,6,7,8,9,10,11,12,13,14,15)', '(3,4,5,6,7,8,9,10,11,12,13,14,15,16)')])
    edit('scripts/package-hakios-ios16.py', [
        ('import hakios_r15', 'import hakios_r15\nimport hakios_r16'),
        ('(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)', '(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16)'),
        ('    if revision>=15:expected_changes.update(hakios_r15.PORT_DELTA)',
         '    if revision>=15:expected_changes.update(hakios_r15.PORT_DELTA)\n    if revision>=16:expected_changes.update(hakios_r16.PORT_DELTA)'),
        ('    if revision in (13,14,15):', '    if revision in (13,14,15,16):'),
        ("    if revision==15:\n        checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-r15-checks.json')", "    if revision>=15:\n        checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-r15-checks.json')"),
        ('    budgeter=hakios_r15 if revision==15 else', '''    if revision==16:
        checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-r16-checks.json')
        hakios.require(checks['passed'] and checks['inputs']==current and checks['actual_production_functions'],
            'R16 actual profile/stride/diagnostic checks required')
        for name,digest in checks['evidence_sha256'].items():
            hakios.require(hakios.sha(Path(name))==digest,'R16 evidence changed: '+name)
        cmdline=hakios_r16.command_line(hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')['cmdline'])
    budgeter=hakios_r15 if revision>=15 else'''),
        ('    if revision==15:\n        for name in', '    if revision>=15:\n        for name in'),
        ('    contract=dict(schema=1', '''    if revision==16:
        for name in [*('ports/hakios-lithium/'+p for p in sorted(hakios_r16.PORT_DELTA)),
            'scripts/hakios_r16.py','scripts/prepare-hakios-ios16-r16.py','scripts/test-hakios-ios16-r16.py',
            'scripts/run-hakios-ios16-r16-regressions.py','reports/hakios-ios16-r16-checks.json']:
            evidence[name]=hakios.sha(hakios.ROOT/name)
    contract=dict(schema=1'''),
        ('    if revision==15:\n        contract.update', '    if revision>=15:\n        contract.update'),
        ("    hakios.write(out/'contract.json',contract)", "    if revision==16:\n        contract.update(display_profile_contract='ports/hakios-lithium/diagnostics/r16.json',\n            payload_arguments_preserved_from_r15=True,display_acceptance_verified=False)\n    hakios.write(out/'contract.json',contract)")])


def register_tasks():
    path=ROOT/'scripts/run-wsl-limited.ps1';text=path.read_text(encoding='utf-8')
    names=('prepare','checks','review','regressions','userspace','build-check','freeze','package','package-checks')
    needle="'kernel', 'native-package', 'root-transfer')]"
    if text.count(needle)!=1: raise RuntimeError('Unexpected task enumeration')
    text=text.replace(needle,', '.join("'hakios-r16-"+n+"'" for n in names)+', '+needle,1)
    start=text.index("if ($Task -like 'hakios-r15-*') {");end=text.index("if ($Task -like 'hakios-r12-*') {",start)
    text=text[:end]+text[start:end].replace('r15','r16')+text[end:]
    text=text.replace("if ($Task -notlike 'hakios-r15-*'", "if ($Task -notlike 'hakios-r16-*' -and $Task -notlike 'hakios-r15-*'",1)
    anchor="if ($Task -in @('hakios-r15-freeze', 'hakios-r15-package')) { $arguments += @('--revision', '15') }"
    if text.count(anchor)!=1: raise RuntimeError('Unexpected R15 task arguments')
    text=text.replace(anchor,anchor+"\nif ($Task -in @('hakios-r16-userspace', 'hakios-r16-build-check')) { $arguments += '--phase4' }\nif ($Task -in @('hakios-r16-freeze', 'hakios-r16-package')) { $arguments += @('--revision', '16') }",1)
    path.write_text(text,encoding='utf-8',newline='\n')
    print('Registered exact R16 delta, retained earlier build and regression gates')


if __name__=='__main__':
    main()
    register_tasks()
