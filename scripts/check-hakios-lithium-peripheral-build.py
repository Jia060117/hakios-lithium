#!/usr/bin/env python3
"""Freeze host-only admission build evidence; never packages or queries a phone."""
import argparse
from datetime import datetime, timezone
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import hakios


FORBIDDEN_MNEMONIC = re.compile(r'(?:cas[a-z]*|swp[a-z]*|ld(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|st(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|ldapr[a-z]*|pac[a-z]*|aut(?:ia|ib|da|db)[a-z]*|xpac[a-z]*|retaa|retab)')
SAFE_HINTS = {'xpaclri','paciasp','pacibsp','autiasp','autibsp','pacia1716','pacib1716','autia1716','autib1716'}


def instruction_rejected(line):
    fields = line.split('//', 1)[0].strip().split(None, 1)
    if not fields:
        return False
    mnemonic = fields[0].lower()
    if mnemonic in SAFE_HINTS and len(fields) == 1:
        return False  # Architecturally safe hint encodings on ARMv8.0.
    if FORBIDDEN_MNEMONIC.fullmatch(mnemonic):
        return True
    return bool(mnemonic in {'msr', 'mrs'} and len(fields) == 2 and
                re.search(r'\b(?:PAN|UAO)\b', fields[1], re.IGNORECASE))


def check_filter_controls():
    # Match the instruction, never symbol names such as whitelist_selftest.cases.
    good = ['adrp x9, 0xfffffff00718e000 <_hc_whitelist_selftest.cases+0x388>',
            'ret', 'ldr x0, [x1]', 'paciasp', 'xpaclri', '<_cases>:',
            'mrs x0, cntfrq_el0', 'add x0, x1, x2 // PAN is only a comment']
    bad = ['casal x0, x1, [x2]', 'swpal w0, w1, [x2]', 'ldaddal w0, w1, [x2]',
           'stclr w0, [x1]', 'ldapr x0, [x1]', 'pacia x0, x1', 'autib x0, x1',
           'xpaci x0', 'retaa', 'retab', 'mrs x0, PAN', 'msr uao, #1']
    hakios.require(all(not instruction_rejected(x) for x in good) and
                   all(instruction_rejected(x) for x in bad), 'Instruction-filter control failed')
    return dict(accepted=len(good), rejected=len(bad))


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='A new ignored artifact directory; existing directories are refused')
    args = parser.parse_args()
    filter_controls = check_filter_controls()
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    inputs = hakios.inputs(spec)
    kernel = work / 'hakios-kernel'
    built = hakios.read(hakios.ROOT / 'reports/hakios-kernel-build.json')
    hakios.require(built['inputs'] == inputs and built['kernel_sha256'] == hakios.sha(kernel),
                   'Current kernel/build provenance differs')
    prepare = module('peripheral_prepare', hakios.ROOT / 'scripts/prepare-hakios-lithium-overnight.py')
    name = 'drivers/input/hakios.input.c'
    hakios.require((work / name).read_text() == prepare.input_source((source / name).read_text()),
                   'Input source is not the reviewed default-off delta')
    port = hakios.ROOT / spec['port_path']
    contract = hakios.read(port / 'peripherals/contract.json')
    for relative, digest in contract['after_sha256'].items():
        hakios.require(hakios.sha(work / relative) == digest, 'Actual input source hash changed')
    for filename, key in [('boot-options.h', 'parser_sha256'), ('kernel.inc', 'header_sha256')]:
        for base in (port, work / 'devices/lithium'):
            hakios.require(hakios.sha(base / 'peripherals' / filename) == contract[key],
                           'Actual admission body/parser hash changed')
    hakios.require(contract['default_off'] and not contract['mmio_access'] and
                   not contract['touch_driver_implemented'] and not contract['gpu_driver_implemented'],
                   'This checker is limited to hardware-free admission')
    audit = module('peripheral_baseline', hakios.ROOT / 'scripts/audit-hakios-lithium-peripheral-baseline.py')
    package = module('ios16_payload_reader', hakios.ROOT / 'scripts/package-hakios-ios16.py')
    r15, cmdline = audit.payload_from_image(15, audit.R15_SHA, package.payload_entries)
    r18, current_cmdline = audit.payload_from_image(18, audit.R18_SHA, package.payload_entries)
    hakios.require(cmdline == current_cmdline, 'Frozen baseline command lines differ')
    alias = audit.BASE / 'baseline-desktop-r15.img'
    hakios.require(hakios.sha(alias) == audit.R15_SHA, 'Fixed desktop baseline differs')
    for item in (r15, r18):
        hakios.require(item['bootcpu'] == item['xnu_cpus'] == 1 and
                       item['expected_affinity'] == '0x1', 'Frozen CPU selection changed')
    tests_path = hakios.ROOT / 'reports/overnight-20261118/accepted-host-tests/passed.json'
    tests = hakios.read(tests_path)
    suites = sorted(p.stem for p in (hakios.ROOT / 'maintenance/hakios-lithium/peripherals').glob('*-test.c'))
    hakios.require(sorted(t['suite'] for t in tests['tests']) == suites and
                   all(t['exit_code'] == 0 for t in tests['tests']), 'Not every current suite passed')
    for relative, digest in tests['source_sha256'].items():
        hakios.require(hakios.sha(hakios.ROOT / relative) == digest, 'Test source evidence is stale: ' + relative)
    review_path = hakios.ROOT / 'reports/hakios-ios16-source-review.json'
    review = hakios.read(review_path)
    hakios.require(review['source_review_passed'] and review['inputs'] == inputs, 'Source review is stale')
    build_log = audit.BASE / 'kernel-build.log'
    log_text = build_log.read_text()
    hakios.require('make-kernel: built: hakios-kernel' in log_text and 'error:' not in log_text,
                   'Actual build log has a compiler/link failure or no success marker')
    output = args.output.resolve()
    artifact_root = (audit.BASE / 'overnight-20261118').resolve()
    hakios.require(output.is_relative_to(artifact_root) and not output.exists(),
                   'Output must be a new directory under ignored overnight artifacts')
    hakios.require(subprocess.run(['git', 'check-ignore', '-q', str(output)],
                   cwd=hakios.ROOT).returncode == 0, 'Artifact output is not Git-ignored')
    output.mkdir(parents=True)
    # Only the byte-validated SHA256 constant table is excluded from text scanning.
    generic = module('generic_literal_reader', hakios.ROOT / 'scripts/package-hakios-lithium.py')
    literal = generic.verified_sha256_literal(kernel, work)
    disassembly = output / 'kernel-disassembly.txt'
    # objdump performs many small writes: avoid per-line DrvFS overhead.
    with tempfile.TemporaryDirectory(prefix='lithium-instruction-scan-') as temporary:
        local = Path(temporary) / 'kernel-disassembly.txt'
        with local.open('w') as stream:
            subprocess.run(['llvm-objdump', '-d', '--no-symbolic-operands', '--no-leading-addr',
                            '--no-show-raw-insn', str(kernel)], stdout=stream, check=True, timeout=180)
        shutil.copyfile(local, disassembly)
    errors = []
    tables = skipped = skip = hint_count = 0
    for line in disassembly.read_text().splitlines():
        if line.strip() == '<K256>:':
            hakios.require(not skip, 'Nested literal exclusion')
            tables += 1
            skip = 64
            continue
        if skip and line.strip():
            hakios.require(not line.endswith(':'), 'SHA256 literal exclusion crossed a symbol')
            skip -= 1
            skipped += 1
            continue
        if line.split('//', 1)[0].strip() in SAFE_HINTS:
            hint_count += 1
            continue
        if instruction_rejected(line):
            errors.append(line.strip())
    hakios.require(tables == 1 and skipped == 64 and skip == 0, 'Literal exclusion extent differs')
    hakios.require(not errors, 'Unsupported Kryo instruction: ' + str(errors[:8]))
    symbols = subprocess.check_output(['llvm-nm', str(kernel)], text=True, timeout=30)
    hakios.require('_lithium_peripheral_worker' in symbols, 'Admission worker is not linked')
    content = kernel.read_bytes()
    for token in (b'LITHIUM TOUCH status=blocked', b'LITHIUM GPU status=blocked',
                  b'platform_backend_unverified', b'power_smmu_scm_backend_unverified'):
        hakios.require(token in content, 'Linked admission diagnostic is missing')
    for src, dest in ((kernel, 'hakios-kernel'), (build_log, 'kernel-build.log'),
                      (tests_path, 'host-tests.json'), (review_path, 'source-review.json'),
                      (hakios.ROOT / 'reports/hakios-kernel-build.json', 'kernel-build.json')):
        shutil.copyfile(src, output / dest)
        hakios.require(hakios.sha(src) == hakios.sha(output / dest), 'Snapshot copy differs')
    prior_names = ['console','dependency-sync','storage-host','package-host','boot-observer',
                   'observation','return','service-wait','thread-wait','static','host']
    prior_names += ['r' + str(n) for n in range(9, 19)]
    regression_reports = {}
    for name in prior_names:
        path = hakios.ROOT / ('reports/hakios-ios16-' + name + '-checks.json')
        report = hakios.read(path)
        hakios.require(report.get('passed') is True, 'Regression report not passed: ' + str(path))
        regression_reports[path.relative_to(hakios.ROOT).as_posix()] = hakios.sha(path)
        shutil.copyfile(path, output / path.name)
    drift = [name for name, digest in r18['evidence_sha256'].items()
             if not (hakios.ROOT / name).is_file() or hakios.sha(hakios.ROOT / name) != digest]
    evidence = dict(
        generated_at_utc=datetime.now(timezone.utc).isoformat(), passed=True,
        scope='Host-only admission kernel and protocol/policy models; no hardware drivers or boot package',
        baseline_image_sha256=audit.R15_SHA, preserved_r18_image_sha256=audit.R18_SHA,
        preserved_cmdline=cmdline.rstrip(b'\0').decode('ascii'),
        expected_affinity='0x1', new_kernel_sha256=hakios.sha(kernel), kernel_bytes=kernel.stat().st_size,
        inputs=inputs, host_suites=len(suites), test_manifest_sha256=hakios.sha(tests_path),
        regression_report_sha256=regression_reports, source_review_checks=len(review['checks']),
        source_review_sha256=hakios.sha(review_path),
        instruction_scan=dict(forbidden_matches=errors, safe_armv8_hint_encodings=hint_count,
                              filter_control_cases=filter_controls,
                              sha256_literal=literal, disassembly_sha256=hakios.sha(disassembly)),
        build_caveat='Upstream make export/symbol-set check exceptions accepted by the existing make-kernel wrapper; no compiler/link error',
        frozen_r18_mutable_report_drift=drift, frozen_contracts_rewritten=False,
        artifacts=hakios.file_map(output), check_script_sha256=hakios.sha(Path(__file__)),
        boot_image_produced=False, boot_ready=False, hardware_verified=False, phone_operations=False, qemu_used=False,
        touch_hardware_backend=False, gpu_hardware_backend=False)
    hakios.write(output / 'build-evidence.json', evidence)
    report_path = hakios.ROOT / 'reports/overnight-20261118/build-evidence.json'
    hakios.require(not report_path.exists(), 'Final evidence already exists; preserve it rather than overwrite')
    hakios.write(report_path, evidence)
    print('PASS preserved R15/R18 payloads and exact baseline command line')
    print('PASS current complete HakiOS admission kernel:', evidence['new_kernel_sha256'])
    print('PASS', len(suites), 'ASan/UBSan suites;', len(review['checks']), 'source checks; ARMv8 instruction filter')
    print('NOT BOOT READY: touch/GPU backends remain unverified; no phone operation or boot image')
    print(report_path)


if __name__ == '__main__':
    main()
