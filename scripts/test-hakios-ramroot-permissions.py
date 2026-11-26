"""Regress the real 0777 image and exercise the upstream writer/reader on ext4."""
from pathlib import Path
import copy
import json
import subprocess
import tempfile
import hakios
import hakios_ramroot as permissions


def rejected(call):
    try:
        call()
    except (AssertionError, RuntimeError, SystemExit):
        return True
    raise AssertionError('Unsafe metadata was accepted')


def main():
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST); hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    r29 = hakios.read(hakios.ROOT / 'reports/hakios-device-r29.json')
    ready = hakios.read(hakios.ROOT / r29['test_contract_path'])
    bad = hakios.ROOT / ready['image_path']; bad = bad.parent / 'ios14-dynamic-root.apfs'
    hakios.require(hakios.sha(bad) == ready['ramdisk_sha256'], 'R29 frozen root differs')
    checks = []
    with tempfile.TemporaryDirectory(prefix='ramroot-permissions-', dir=work) as tmp:
        tmp = Path(tmp)
        rejected(lambda: permissions.verify_image(bad, work, tmp / 'r29'))
        checks.append('real_R29_0777_image_rejected')
        root = tmp / 'root'; root.mkdir()
        for name in ('LaunchDaemons', 'bin', 'sbin', 'lib', 'etc'):
            (root / name).mkdir()
        for name in ('LaunchDaemons/test.plist', 'bin/svc_test', 'sbin/launchd', 'lib/test.dylib', 'etc/rc'):
            (root / name).write_bytes(b'fixture\n')
        for p in [root] + list(root.rglob('*')): p.chmod(0o777)
        permissions.normalize(root)
        image = tmp / 'root.apfs'
        subprocess.run(['python3', 'tools/apfs/mkapfs.py', '-o', str(image), 'Root=' + str(root)], cwd=work, check=True)
        good = permissions.verify_image(image, work, tmp / 'good', expected_jobs=1)
        checks.append('normalized_ext4_tree_roundtrips_actual_upstream_APFS_writer_reader')
        rows = [dict(path='/', mode=0o40755, uid=0, gid=0)]
        for p in sorted(root.rglob('*')):
            rows.append(dict(path='/' + p.relative_to(root).as_posix(), mode=p.stat().st_mode, uid=0, gid=0))
        for path, key, value in [('/LaunchDaemons/test.plist', 'uid', 501),
                ('/', 'mode', 0o40777), ('/LaunchDaemons', 'mode', 0o40777),
                ('/bin/svc_test', 'mode', 0o100644), ('/etc/rc', 'gid', 20)]:
            changed = copy.deepcopy(rows)
            next(r for r in changed if r['path'] == path)[key] = value
            rejected(lambda: permissions.verify_rows(changed, 1))
            checks.append('reject_' + path + '_' + key)
    hakios.write(hakios.ROOT / 'reports/hakios-ramroot-permission-tests.json', dict(passed=True, checks=checks,
        real_R29_regression_checked=True, actual_upstream_writer_reader_exercised=True,
        helper_sha256=hakios.sha(Path(permissions.__file__)), test_source_sha256=hakios.sha(Path(__file__)),
        qemu_used=False, device_commands_issued=False))
    print('Boot-root permissions: ' + str(len(checks)) + ' checks passed')


if __name__ == '__main__': main()
