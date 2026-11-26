"""Exact R20 ancestry and opt-in Stage1 build provenance; no device commands."""
import hashlib
import json
from pathlib import Path

R20_CONTRACT_SHA256 = 'c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c'
R20_SHA256 = '3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82'
R15_SHA256 = 'ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55'
REVIEWED_DELTA = {
    'patches/0020-lithium-touch-batch-queue.patch': '8c06c74e6ebf996f34cf7746e51bc28fc6bd2865dbc2116d165428f136ff4c52',
    'patches/0021-lithium-r20-touch-geometry.patch': '2961652ccf1a65e8578ebb52697e62134b6848a1b561ba47207e199b6886ce4e',
    'peripherals/boot-options.h': '8328d5d65ec1eafe6be426e415cf1ff2b770a5bdc5cf2a52d392410e6efe1761',
    'peripherals/contract.json': '1712dcb215a30ed92b530e32182825f1d88ddfcde2ae80eb4204c4f56722a85e',
    'peripherals/kernel.inc': '96a7394fc97e9a7fbab2d0054907a726ac58c3ad9bfd851227b981b268dbccc0',
    # Retained, unused candidates from the interrupted earlier work. Their
    # presence is recorded, not mistaken for an installed exception hook.
    'peripherals/touch-mmio-access.inc': 'd8a1733d957700df0cf0145659db0615d9d7e19c3d4af7105719a413afc8969f',
    'peripherals/touch-mmio-policy.h': '324bd13c3c0548cf0d7b9b1e9705bce4993857df34ba2a21db816f6fd5cefb69',
    'peripherals/touch-stage1.inc': '990dfbcfef08c3555a01017c8c21947158ace6a61dce16aeb356246cf06ed240',
}
PORT_DELTA = set(REVIEWED_DELTA) | {'patches/0019-lithium-peripheral-admission.patch'}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    base = root / 'artifacts/hakios/lithium-ios16'
    path = base / 'phase4-r20-display/contract.json'
    require(sha(path) == R20_CONTRACT_SHA256, 'Frozen R20 contract changed')
    r20 = read(path)
    require(sha(root / r20['image_path']) == R20_SHA256 and
            sha(base / 'baseline-desktop-r15.img') == R15_SHA256, 'Frozen R20/R15 changed')
    accepted = read(root / 'reports/hakios-ios16-device-r20-display.json')
    require(accepted.get('display_acceptance', {}).get('passed') is True and
            accepted.get('image_sha256') == R20_SHA256, 'R20 acceptance changed')
    ancestry = base / 'phase4-r19/contract.json'
    require(sha(ancestry) == r20['evidence']['r19_contract_sha256'], 'R20 source ancestry changed')
    prior = read(ancestry)['inputs']
    require({k: v for k, v in prior.items() if k != 'port_files'} ==
            {k: v for k, v in current.items() if k != 'port_files'}, 'Non-port HakiOS input changed')
    before, now = prior['port_files'], current['port_files']
    delta = {name for name in set(before) | set(now) if before.get(name) != now.get(name)}
    require(delta == set(REVIEWED_DELTA), 'Unreviewed Stage1 source additions/removals')
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root / 'ports/hakios-lithium' / name) == digest and
                sha(work / 'devices/lithium' / name) == digest, 'Stage1 input changed: ' + name)
    p = root / 'ports/hakios-lithium/peripherals'
    contract = read(p / 'contract.json')
    require(contract['default_off'] and contract['touch_stage1_probe'] and contract['mmio_access'] and
            contract['mmio_default_off'] and contract['touch_stages_2_to_3_blocked'] and
            not contract['touch_driver_implemented'] and not contract['gpu_driver_implemented'] and
            not contract['hardware_verified'], 'Stage1 must remain opt-in and observation-only')
    for field, name in [('header_sha256', 'kernel.inc'), ('parser_sha256', 'boot-options.h'),
                        ('stage1_sha256', 'touch-stage1.inc')]:
        require(contract[field] == sha(p / name), 'Stage1 contract hash differs: ' + name)
    body = (p / 'kernel.inc').read_text()
    require('#include "touch-stage1.inc"' in body and
            'lithium_peripheral_requested.touch_stage == 1' in body and
            'touch-mmio-access.inc' not in body, 'Stage1 dispatch or unused candidate boundary changed')
    return dict(passed=True, revision='R25-touch-stage1', display_baseline='R20-display',
                exact_post_r20_source_delta=sorted(delta), hardware_verified=False,
                physical_touch_backend=False, stage1_observation_only=True, device_operations=False)
