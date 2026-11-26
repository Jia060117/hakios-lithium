#!/usr/bin/env python3
"""Run actual host suites and preserve a source-bound Fastboot-return test gate."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT/'evidence/fastboot-return-impl-20261120'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def read(p):
    return json.loads(Path(p).read_text())


def main():
    if sys.platform != 'linux':
        return subprocess.call(['wsl.exe','-d','Ubuntu','-u','jia','--','python3',
            '-B','/mnt/f/IOS-MIX1/scripts/'+Path(__file__).name])
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    out = EVIDENCE/'main'/('host-tests-'+stamp)
    out.mkdir(parents=True, exist_ok=False)
    suites = [
        ('contract','test-hakios-return-fastboot-contract.py',
            EVIDENCE/'contract-fixes/test-results.json'),
        ('reset','test-hakios-return-fastboot-reset.py',
            EVIDENCE/'reset-core/test-results.json'),
        ('integration','test-hakios-return-fastboot-integration.py',
            EVIDENCE/'integration-review/latest-results.json'),
        ('init','test-hakios-return-fastboot-init.py',
            EVIDENCE/'integration-review/init/latest-results.json'),
        ('firmware','test-hakios-return-fastboot-firmware.py',
            EVIDENCE/'firmware-review/main-final/test-results.json'),
        ('package','test-hakios-return-fastboot-package.py',
            EVIDENCE/'package/unit-tests.json')]
    port = ROOT/'ports/hakios-lithium'
    sources = {str(p):sha(p) for p in port.rglob('*')
        if p.is_file() and '__pycache__' not in p.parts}
    paths = [Path(__file__),ROOT/'scripts/package-hakios-return-fastboot.py',
        ROOT/'scripts/hakios_return_fastboot.py',ROOT/'scripts/build-hakios-return-fastboot.py',
        ROOT/'scripts/prepare-hakios-return-fastboot.py',
        ROOT/'scripts/check-hakios-ios16-build.py',ROOT/'scripts/build-hakios-support.py']
    paths += [ROOT/'scripts'/script for _,script,_ in suites]
    paths += [ROOT/'scripts/test-hakios-lithium-peripherals-next.py',
        ROOT/'scripts/test-hakios-lithium-peripherals.py']
    sources.update({str(p):sha(p) for p in paths})
    tests, evidence = [], {}
    result = dict(schema=1, passed=False, source_sha256=sources,tests=tests,
        tests_run=0, failures=0, errors=0,device_operations=False,qemu_used=False)
    try:
        for name, script, report in suites:
            argv = [sys.executable,'-B',str(ROOT/'scripts'/script)]
            if name=='firmware': argv += ['--run-label','main-final']
            with (out/(name+'.log')).open('w') as sink:
                p=subprocess.run(argv,cwd=ROOT,stdout=sink,stderr=subprocess.STDOUT)
            tests.append(dict(name=name,argv=argv,exit_code=p.returncode))
            if p.returncode: raise RuntimeError(name+' failed; '+str(out/(name+'.log')))
            native=read(report)
            if native.get('status') != 'passed' and native.get('passed') is not True:
                raise RuntimeError(name+' did not provide a passing native report')
            # The producer may move its output directory when replacing a
            # reviewed suite. A passing historical report is not evidence for
            # the source that this invocation just executed.
            bindings = native.get('input_sha256', native.get('source_sha256', {}))
            for path, digest in bindings.items():
                p=Path(path)
                if not p.is_absolute(): p=ROOT/p
                if sha(p)!=digest: raise RuntimeError(name+' native report is stale: '+path)
            if 'test_script_sha256' in native and native['test_script_sha256']!=sha(ROOT/'scripts'/script):
                raise RuntimeError(name+' native runner hash differs')
            if 'header_sha256' in native:
                header='contract.h' if name=='contract' else 'reset-core.h'
                if native['header_sha256']!=sha(port/'return-fastboot'/header):
                    raise RuntimeError(name+' native header hash differs')
            snapshot=out/(name+'-native.json')
            snapshot.write_bytes(report.read_bytes())
            evidence[str(snapshot)]=sha(snapshot)
            result['tests_run']+=1
            print('PASS actual host suite: '+name,flush=True)
        argv=[sys.executable,'-B',str(ROOT/'scripts/test-hakios-lithium-peripherals-next.py'),
            '--build-dir',str(out/'all-peripherals')]
        with (out/'peripherals.log').open('w') as sink:
            p=subprocess.run(argv,cwd=ROOT,stdout=sink,stderr=subprocess.STDOUT)
        tests.append(dict(name='all-peripherals',argv=argv,exit_code=p.returncode))
        if p.returncode: raise RuntimeError('Peripheral regression failed: '+str(out/'peripherals.log'))
        report=out/'all-peripherals/passed.json'
        if read(report).get('passed') is not True: raise RuntimeError('Peripheral gate incomplete')
        evidence[str(report)]=sha(report)
        result['tests_run']+=1
        for path,digest in sources.items():
            if sha(path)!=digest: raise RuntimeError('Input changed during host tests: '+path)
        result.update(passed=True,evidence_sha256=evidence)
    except Exception as exc:
        result.update(failures=1,error=str(exc))
        print(str(exc),file=sys.stderr,flush=True)
    result['finished_at']=datetime.now(timezone.utc).isoformat()
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    if result['passed']:
        pointer=EVIDENCE/'main/host-tests-final.json'
        pointer.write_text(json.dumps(dict(passed=True,path=str(out/'result.json'),
            sha256=sha(out/'result.json')),indent=2)+'\n')
    print('Host gate: '+str(out/'result.json'),flush=True)
    return 0 if result['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
