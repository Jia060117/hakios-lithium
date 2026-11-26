#!/usr/bin/env python3
"""Rebind only the rerun package suite after the documented LLVM alias fix."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'evidence/fastboot-return-impl-20261120/main'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    pointer=EVIDENCE/'host-tests-final.json'
    old=json.loads(pointer.read_text())
    gate=Path(old['path'])
    assert old['passed'] is True and sha(gate)==old['sha256']
    prior=json.loads(gate.read_text())
    assert prior['passed'] is True and len(prior['tests'])==7
    allowed={str(ROOT/'scripts/package-hakios-return-fastboot.py'),
        str(ROOT/'scripts/test-hakios-return-fastboot-package.py')}
    changed={p for p,digest in prior['source_sha256'].items() if sha(p)!=digest}
    assert changed and changed<=allowed,changed
    native_path=ROOT/'evidence/fastboot-return-impl-20261120/package/unit-tests.json'
    native=json.loads(native_path.read_text())
    assert native['passed'] is True and native['tests_run']>=49
    assert native['failures']==native['errors']==native['skipped']==0
    for p,digest in native['source_sha256'].items():
        assert sha(p)==digest,p
    assert allowed<=set(native['source_sha256'])
    text=(native_path.parent/'unit-tests.txt').read_text()
    assert text.rstrip().endswith('OK') and 'symlink_driver_alias' in text
    now=datetime.now(timezone.utc)
    out=EVIDENCE/('host-tests-package-fix-'+now.strftime('%Y%m%dT%H%M%S%fZ'))
    out.mkdir()
    report=out/'package-native.json'
    report.write_bytes(native_path.read_bytes())
    tests=[]
    for test in prior['tests']:
        test=dict(test)
        assert test['exit_code']==0
        test['reused_exact_inputs']=test['name']!='package'
        if test['name']=='package':
            test['native_result']=str(report)
            test['rerun_after_llvm_alias_fix']=True
        tests.append(test)
    evidence={p:d for p,d in prior['evidence_sha256'].items()
        if Path(p).name!='package-native.json'}
    for p,d in evidence.items(): assert sha(p)==d,p
    evidence[str(report)]=sha(report)
    result=dict(schema=1,passed=True,source_sha256={p:sha(p) for p in prior['source_sha256']},
        evidence_sha256=evidence,tests=tests,tests_run=7,failures=0,errors=0,
        parent_gate=str(gate),parent_gate_sha256=sha(gate),
        changed_inputs=sorted(changed),package_tests_run=native['tests_run'],
        isolated_rerun_reason='Real package resolved ld.lld alias to generic lld; preserve executable alias',
        unchanged_suites_reused_only_after_every_other_source_hash_matched=True,
        finished_at=now.isoformat(),device_operations=False,qemu_used=False)
    result_path=out/'result.json'
    result_path.write_text(json.dumps(result,indent=2)+'\n')
    backup=EVIDENCE/'host-tests-final.before-package-fix.json'
    assert not backup.exists()
    backup.write_bytes(pointer.read_bytes())
    pointer.write_text(json.dumps(dict(passed=True,path=str(result_path),
        sha256=sha(result_path)),indent=2)+'\n')
    print('Package regression rerun accepted; six unrelated gates retained with unchanged source hashes')


if __name__=='__main__':
    main()
