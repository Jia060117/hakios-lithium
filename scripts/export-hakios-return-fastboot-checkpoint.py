#!/usr/bin/env python3
"""Export an exact reversible source/document delta; never modifies Git staging."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'evidence/fastboot-return-impl-20261120/main'
BEFORE=MAIN/'before'
STEM=ROOT/'reports/hakios-ios16-return-fastboot-r33-prepared'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    before=json.loads((MAIN/'snapshot.json').read_text())
    names=set(before['files'])
    names.update(p.relative_to(ROOT).as_posix() for p in
        (ROOT/'ports/hakios-lithium').rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    names.update(p.relative_to(ROOT).as_posix() for p in (ROOT/'scripts').glob('*return*fastboot*.py'))
    names.update({'scripts/hakios_return_fastboot.py',
        'reports/hakios-ios16-return-fastboot-r33-prepared.md',
        'reports/hakios-ios16-return-fastboot-r33-prepared.json',
        'reports/hakios-ios16-return-fastboot-build.json',
        'reports/hakios-ios16-return-fastboot-build-checks.json',
        'evidence/fastboot-return-impl-20261120/main/common-transform.json',
        'evidence/fastboot-return-impl-20261120/main/final-offline-readback.json',
        'evidence/fastboot-return-impl-20261120/main/linked-feature-review.json'})
    changed=[]
    for name in sorted(names):
        old=BEFORE/name;new=ROOT/name
        a=old.read_bytes() if old.is_file() else None
        b=new.read_bytes() if new.is_file() else None
        if a!=b:
            assert b is None or b'\0' not in b, 'No firmware/binary in source checkpoint: '+name
            changed.append((name,a,b))
    patch=STEM.with_suffix('.patch')
    assert not patch.exists()
    env=dict(os.environ,GIT_OPTIONAL_LOCKS='0')
    chunks=[]
    with tempfile.TemporaryDirectory(prefix='rfbt-source-diff-') as temporary:
        temp=Path(temporary)
        for name,a,b in changed:
            old=temp/'old';new=temp/'new'
            if a is not None:old.write_bytes(a)
            if b is not None:new.write_bytes(b)
            args=['git','diff','--no-index','--binary','--',
                'old' if a is not None else '/dev/null',
                'new' if b is not None else '/dev/null']
            p=subprocess.run(args,cwd=temp,env=env,capture_output=True)
            assert p.returncode==1,(name,p.stderr)
            # Rewrite only diff headers, never matching string literals in a
            # hunk (including this exporter's own source).
            lines=[]
            for line in p.stdout.splitlines(keepends=True):
                if line.startswith(b'diff --git '):
                    line=('diff --git a/'+name+' b/'+name+'\n').encode()
                elif line==b'--- a/old\n':
                    line=('--- a/'+name+'\n').encode()
                elif line==b'+++ b/new\n':
                    line=('+++ b/'+name+'\n').encode()
                lines.append(line)
            data=b''.join(lines)
            chunks.append(data)
    patch.write_bytes(b''.join(chunks))
    with tempfile.TemporaryDirectory(prefix='rfbt-source-rollback-') as temporary:
        temp=Path(temporary)
        for name,a,_ in changed:
            if a is not None:
                p=temp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(a)
        for args in (['git','apply','--check',str(patch)],['git','apply',str(patch)]):
            subprocess.run(args,cwd=temp,env=env,check=True,capture_output=True)
        for name,_,b in changed:
            p=temp/name
            assert (p.read_bytes() if p.exists() else None)==b,name
        for args in (['git','apply','--reverse','--check',str(patch)],
                     ['git','apply','--reverse',str(patch)]):
            subprocess.run(args,cwd=temp,env=env,check=True,capture_output=True)
        for name,a,_ in changed:
            p=temp/name
            assert (p.read_bytes() if p.exists() else None)==a,name
    result=dict(schema=1,patch_path=patch.relative_to(ROOT).as_posix(),
        patch_sha256=sha(patch.read_bytes()),files=len(changed),
        forward_exact=True,reverse_exact=True,git_staging_modified=False,
        firmware_binaries_in_patch=False,device_operations=0,
        source_files={n:dict(before_sha256=sha(a) if a is not None else None,
            after_sha256=sha(b) if b is not None else None) for n,a,b in changed},
        scope='Exact source/project-document rollback; local logs and frozen firmware remain archived')
    STEM.with_name(STEM.name+'-checkpoint.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_files'}))


if __name__=='__main__':
    main()
