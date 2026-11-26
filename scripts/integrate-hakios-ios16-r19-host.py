"""Register R19 host provenance without changing the R18 display implementation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def edit(name, pairs):
    path = ROOT / name
    text = path.read_text(encoding="utf-8")
    for old, new in pairs:
        if text.count(old) != 1:
            raise RuntimeError(f"{name}: unexpected R19 anchor {old!r}")
        text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8", newline="\n")


def main():
    edit("scripts/check-hakios-ios16-build.py", [
        ("import hakios_r18", "import hakios_r18\nimport hakios_r19"),
        ("            expected.update(hakios_r18.PORT_DELTA)",
         "            expected.update(hakios_r18.PORT_DELTA)\n"
         "        if 'patches/0019-lithium-peripheral-admission.patch' in delta:\n"
         "            hakios_r19.source_checks(hakios.ROOT, work, hakios.inputs(spec))\n"
         "            expected.update(hakios_r19.PORT_DELTA)"),
        ("    symbols=subprocess.check_output(['llvm-nm',str(kernel)],text=True)",
         "    symbols=subprocess.check_output(['llvm-nm',str(kernel)],text=True)\n"
         "    if (work/'devices/lithium/peripherals/contract.json').exists():\n"
         "        hakios.require('_lithium_peripheral_worker' in symbols, 'R19 admission worker missing from actual kernel')\n"
         "        hakios.require(all(token in kernel.read_bytes() for token in (b'LITHIUM TOUCH status=blocked',\n"
         "            b'LITHIUM GPU status=blocked', b'platform_backend_unverified',\n"
         "            b'power_smmu_scm_backend_unverified')), 'Actual default-off admission diagnostics missing')"),
    ])
    edit("scripts/freeze-hakios-ios16-boot-userspace.py", [
        ("(3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18)",
         "(3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19)"),
    ])
    edit("scripts/package-hakios-ios16.py", [
        ("import hakios_r18", "import hakios_r18\nimport hakios_r19"),
        ("(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18)",
         "(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19)"),
        ("    if revision>=18:expected_changes.update(hakios_r18.PORT_DELTA)",
         "    if revision>=18:expected_changes.update(hakios_r18.PORT_DELTA)\n"
         "    if revision>=19:\n"
         "        hakios_r19.source_checks(hakios.ROOT, work, current)\n"
         "        expected_changes.update(hakios_r19.PORT_DELTA)"),
        ("(13,14,15,16,17,18)", "(13,14,15,16,17,18,19)"),
        ("    if revision==18:\n        checks=", "    if revision in (18,19):\n        checks="),
        ("    budgeter=hakios_r15",
         "    if revision==19:\n"
         "        checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-r19-checks.json')\n"
         "        hakios.require(checks['passed'] and checks['inputs']==current and checks['actual_frozen_command_parser_checked'], 'R19 actual baseline/default-off tests required')\n"
         "        for name,digest in checks['evidence_sha256'].items():\n"
         "            hakios.require(hakios.sha(Path(name))==digest, 'R19 evidence changed: '+name)\n"
         "        cmdline=hakios_r19.command_line(hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r18/contract.json')['cmdline'], hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')['cmdline'])\n"
         "    budgeter=hakios_r15"),
        ("    if revision==18:\n        for name in", "    if revision in (18,19):\n        for name in"),
        ("    contract=dict(schema=1",
         "    if revision==19:\n"
         "        for name in [*('ports/hakios-lithium/'+p for p in sorted(hakios_r19.PORT_DELTA)),\n"
         "            'scripts/hakios_r19.py', 'scripts/prepare-hakios-ios16-r19.py',\n"
         "            'scripts/test-hakios-ios16-r19.py', 'scripts/test-hakios-ios16-r19-contract.py',\n"
         "            'scripts/test-hakios-ios16-r19-snapshots.py', 'scripts/test-hakios-ios16-r19-package.py',\n"
         "            'scripts/run-hakios-ios16-r19-regressions.py', 'scripts/integrate-hakios-ios16-r19-host.py',\n"
         "            'scripts/boot-hakios-ios16.py', 'scripts/test-hakios-ios16-offline-preflight.py',\n"
         "            'scripts/run-wsl-limited.ps1', 'reports/hakios-ios16-r19-prepared-sources.json',\n"
         "            'reports/hakios-ios16-r19-checks.json', 'reports/overnight-20261118/accepted-host-tests/passed.json']:\n"
         "            evidence[name]=hakios.sha(hakios.ROOT/name)\n"
         "        evidence_snapshots=hakios_r19.snapshot_evidence(hakios.ROOT, out, evidence)\n"
         "    contract=dict(schema=1"),
        ("    if revision==18:\n        contract.update(display_profile_contract=",
         "    if revision in (18,19):\n        contract.update(display_profile_contract="),
        ("    hakios.write(out/'contract.json',contract)",
         "    if revision==19:\n"
         "        contract.update(provenance_parent_revision=18, admission_default_off=True,\n"
         "            hardware_backends_implemented=False, evidence_snapshots=evidence_snapshots,\n"
         "            evidence_snapshot_policy='Exact text bytes retained alongside unchanged live guards')\n"
         "    hakios.write(out/'contract.json',contract)"),
    ])
    p = ROOT / "scripts/run-wsl-limited.ps1"
    text = p.read_text(encoding="utf-8")
    tasks = ["prepare", "checks", "review", "regressions", "userspace",
             "build-check", "freeze", "package", "package-checks"]
    anchor = "'hakios-r18-package-checks', 'kernel'"
    if text.count(anchor) != 1:
        raise RuntimeError("R19 task allowlist anchor changed")
    text = text.replace(anchor, "'hakios-r18-package-checks', " +
                        ", ".join("'hakios-r19-" + x + "'" for x in tasks) + ", 'kernel'", 1)
    begin = text.index("if ($Task -like 'hakios-r18-*') {")
    end = text.index("if ($Task -like 'hakios-r12-*') {", begin)
    text = text[:end] + text[begin:end].replace("r18", "r19") + text[end:]
    text = text.replace("if ($Task -notlike 'hakios-r18-*'",
                        "if ($Task -notlike 'hakios-r19-*' -and $Task -notlike 'hakios-r18-*'", 1)
    text = text.replace("& wsl.exe @arguments",
                        "if ($Task -in @('hakios-r19-userspace', 'hakios-r19-build-check')) { $arguments += '--phase4' }\n"
                        "if ($Task -in @('hakios-r19-freeze', 'hakios-r19-package')) { $arguments += @('--revision', '19') }\n"
                        "& wsl.exe @arguments", 1)
    p.write_text(text, encoding="utf-8", newline="\n")
    # The old regex matched symbol annotations (for example selftest.cases).
    # Reuse the already-tested mnemonic/operand parser and run its controls.
    for name, label in (("scripts/check-hakios-ios16-build.py", "checked_instruction_filter"),
                        ("scripts/package-hakios-ios16.py", "checked_package_instruction_filter")):
        old_filter = next(line for line in (ROOT / name).read_text().splitlines()
                          if line.startswith("    forbidden=re.compile("))
        new_filter = ("    filter_path=hakios.ROOT/'scripts/check-hakios-lithium-peripheral-build.py'\n"
                      f"    filter_spec=importlib.util.spec_from_file_location('{label}',filter_path)\n"
                      "    isa=importlib.util.module_from_spec(filter_spec);filter_spec.loader.exec_module(isa)\n"
                      "    filter_controls=isa.check_filter_controls()")
        edit(name, [(old_filter, new_filter),
                    ("if forbidden.search(line.split('//')[0]):errors.append(line.strip())",
                     "if isa.instruction_rejected(line):errors.append(line.strip())")])
    edit("scripts/check-hakios-ios16-build.py", [
        ("    result=dict(passed=True,phase=phase,inputs=hakios.inputs(spec),instruction_scans=scans,",
         "    result=dict(passed=True,phase=phase,inputs=hakios.inputs(spec),instruction_scans=scans,\n"
         "        instruction_filter_controls=filter_controls,instruction_filter_sha256=hakios.sha(filter_path),"),
    ])
    edit("scripts/package-hakios-ios16.py", [
        ("            'scripts/boot-hakios-ios16.py', 'scripts/test-hakios-ios16-offline-preflight.py',",
         "            'scripts/boot-hakios-ios16.py', 'scripts/test-hakios-ios16-offline-preflight.py',\n"
         "            'scripts/check-hakios-lithium-peripheral-build.py',"),
        ("        new_instruction_checks=scans,sha256_literal=literal,stack_contract=stack,",
         "        new_instruction_checks=scans,sha256_literal=literal,stack_contract=stack,\n"
         "        instruction_filter_controls=filter_controls,instruction_filter_sha256=hakios.sha(filter_path),"),
    ])
    print("Registered R19 exact-source delta, live evidence plus snapshots, normal capped tasks")


if __name__ == "__main__":
    main()
