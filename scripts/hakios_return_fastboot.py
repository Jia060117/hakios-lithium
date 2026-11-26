"""Exact R32 ancestry plus the reviewed additive Fastboot-return transformation."""
import hashlib
import json
from pathlib import Path
import hakios_touch_r32
import importlib.util


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def read(p):
    return json.loads(Path(p).read_text())


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    frozen = root / 'artifacts/hakios/lithium-ios16/phase4-r32-touch-qup-transfer/contract.json'
    prior = read(frozen)
    require(sha(root / prior['image_path']) == prior['image_sha256'] ==
        'b260776a3fff5513134b2c594e364eb58fb95a495c9a7625e6866354345fe767',
        'Frozen R32 image changed')
    before = prior['inputs']
    require({k: v for k, v in current.items() if k != 'port_files'} ==
        {k: v for k, v in before.items() if k != 'port_files'}, 'Complete HakiOS baseline changed')
    contract = read(root / 'ports/hakios-lithium/return-fastboot/source-contract.json')
    old, new = before['port_files'], current['port_files']
    delta = {k for k in set(old) | set(new) if old.get(k) != new.get(k)}
    self_name = 'return-fastboot/source-contract.json'
    require(delta == set(contract['post_r32_port_sha256']) | {self_name},
        'Unexpected Fastboot-return source delta')
    require(new[self_name] == sha(root / 'ports/hakios-lithium' / self_name) ==
        sha(work / 'devices/lithium' / self_name), 'Return source contract differs')
    for name, digest in contract['post_r32_port_sha256'].items():
        require(new.get(name) == digest and
            sha(root / 'ports/hakios-lithium' / name) == digest and
            sha(work / 'devices/lithium' / name) == digest,
            'Reviewed Fastboot-return input changed: ' + name)
    # Retain the complete R32 ancestry/actual touch admission checks using the
    # exact prior inventory, not a relaxed acceptance of new unrelated files.
    hakios_touch_r32.source_checks(root, work, before)
    transform = read(root / 'evidence/fastboot-return-impl-20261120/main/common-transform.json')
    require(sha(root / transform['patch']) == transform['patch_sha256'],
        'Additive return patch changed')
    for name, hashes in transform['sources'].items():
        require(sha(work / name) == hashes['after'], 'Transformed common input differs: ' + name)
        saved = root / 'evidence/fastboot-return-impl-20261120/main/common-before' / name
        require(sha(saved) == hashes['before'], 'Unmodified R32 common source snapshot differs')
    return dict(passed=True, revision='R33-return-fastboot', exact_post_r32_source_delta=sorted(delta),
        r32_touch_modules_unchanged=True, default_off=True, hardware_verified=False,
        ufs_auto_return_supported=False, ram_auto_return_implemented=True)


def matches_prior(work, name, digest):
    """Reconstruct known history to the exact pre0023 bytes, never accept drift."""
    work = Path(work)
    root = Path(__file__).resolve().parents[1]
    transform = read(root / 'evidence/fastboot-return-impl-20261120/main/common-transform.json')
    if name not in transform['sources']:
        return False
    entry = transform['sources'][name]
    if sha(work / name) != entry['after']:
        return False
    for revision in (13, 15, 16, 17, 18):
        path = work / f'devices/lithium/diagnostics/r{revision}.json'
        if path.exists():
            c = read(path)
            if c['before_sha256'].get(name) == digest:
                digest = c['after_sha256'][name]
    return digest == entry['before']


def init_source(text):
    path = Path(__file__).with_name('prepare-hakios-return-fastboot.py')
    spec = importlib.util.spec_from_file_location('return_fastboot_transform', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.init_source(text)
