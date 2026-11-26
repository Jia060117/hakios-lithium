"""Permissions for the small integrated boot root, checked through real APFS inodes."""
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import hakios


def expected_mode(path, directory=False):
    if directory:
        return stat.S_IFDIR | 0o755
    parts = PurePosixPath(path).parts
    executable = len(parts) > 2 and (parts[1] in ('bin', 'sbin') or
        (parts[1] == 'lib' and path.endswith('.dylib')))
    return stat.S_IFREG | (0o755 if executable else 0o644)


def normalize(root):
    root = Path(root)
    entries = [root] + sorted(root.rglob('*'))
    for p in entries:
        mode = p.lstat().st_mode
        hakios.require(stat.S_ISDIR(mode) or stat.S_ISREG(mode), 'Unexpected boot-root node: ' + str(p))
    for p in entries:
        name = '/' + p.relative_to(root).as_posix() if p != root else '/'
        p.chmod(stat.S_IMODE(expected_mode(name, p.is_dir())))
    return len(entries)


def verify_rows(rows, expected_jobs):
    paths = {r['path']: r for r in rows}
    hakios.require(len(paths) == len(rows) and '/' in paths, 'Duplicate or absent APFS inode paths')
    for path, row in paths.items():
        directory = stat.S_ISDIR(row['mode'])
        hakios.require(row['uid'] == row['gid'] == 0 and
            row['mode'] == expected_mode(path, directory), 'Unsafe APFS metadata: ' + str(row))
        if path != '/':
            parent = paths.get(str(PurePosixPath(path).parent))
            hakios.require(parent and stat.S_ISDIR(parent['mode']), 'Missing directory ancestor: ' + path)
    jobs = [p for p in paths if p.startswith('/LaunchDaemons/')]
    hakios.require(len(jobs) == expected_jobs and all(p.endswith('.plist') and
        stat.S_ISREG(paths[p]['mode']) for p in jobs), 'APFS launchd job count/type differs')
    for path in ['/LaunchDaemons', '/bin', '/sbin', '/lib', '/etc']:
        hakios.require(path in paths and stat.S_ISDIR(paths[path]['mode']), 'Required directory absent: ' + path)
    return len(paths)


def verify_image(image, work, output, expected_jobs=335):
    image, work, output = Path(image), Path(work), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    tool = work / 'apfs-work/apfs-tool'
    raw = subprocess.check_output([str(tool), 'census', str(image)], text=True)
    root = subprocess.check_output([str(tool), 'stat', str(image), '/'], text=True)
    (output / 'root-census.tsv').write_text(raw, encoding='utf-8')
    (output / 'root-stat.txt').write_text(root, encoding='utf-8')
    rows = []
    for line in raw.splitlines():
        path, kind, mode, uid, gid, size, fnv = line.split('\t')
        rows.append(dict(path=path, mode=int(mode, 8), uid=int(uid), gid=int(gid)))
    match = re.search(r'^mode\s+([0-7]+)\s+uid (\d+)\s+gid (\d+)$', root, re.M)
    hakios.require(match is not None, 'Missing actual root inode metadata')
    rows.append(dict(path='/', mode=int(match[1], 8), uid=int(match[2]), gid=int(match[3])))
    count = verify_rows(rows, expected_jobs)
    return dict(passed=True, actual_apfs_inodes_checked=count, jobs_checked=expected_jobs,
        directories_mode='0755', jobs_mode='0644', executable_mode='0755', uid=0, gid=0,
        image_sha256=hakios.sha(image), helper_sha256=hakios.sha(Path(__file__)),
        census_sha256=hakios.sha(output / 'root-census.tsv'), root_stat_sha256=hakios.sha(output / 'root-stat.txt'))
