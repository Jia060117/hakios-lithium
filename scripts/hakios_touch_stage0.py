"""Exact host-only provenance for R20-display plus touch Stage 0."""
import hashlib
import json
from pathlib import Path

R20_CONTRACT_SHA256 = 'c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c'
R20_SHA256 = '3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82'
R15_SHA256 = 'ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55'
# Only these five production inputs differ from the immutable source snapshot
# embedded in the ancestor of the accepted R20 image. No historical boot is run.
REVIEWED_DELTA = {
    'patches/0020-lithium-touch-batch-queue.patch': '8c06c74e6ebf996f34cf7746e51bc28fc6bd2865dbc2116d165428f136ff4c52',
    'patches/0021-lithium-r20-touch-geometry.patch': '2961652ccf1a65e8578ebb52697e62134b6848a1b561ba47207e199b6886ce4e',
    'peripherals/boot-options.h': '8328d5d65ec1eafe6be426e415cf1ff2b770a5bdc5cf2a52d392410e6efe1761',
    'peripherals/kernel.inc': '98de71f1422fd771f5d01d9cdaceb8321eff16e5e0eee0d27c311ff8c02d97bb',
    'peripherals/contract.json': '076128ef502c7665afc89f77886ab3fa6cfcae3ac1a7423c72e573ae2e2f4f8e',
}
PORT_DELTA = set(REVIEWED_DELTA) | {'patches/0019-lithium-peripheral-admission.patch'}

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))

def validate_delta(prior, current):
    delta = {name for name in set(prior) | set(current) if prior.get(name) != current.get(name)}
    require(delta == set(REVIEWED_DELTA), 'Only the five reviewed R20 touch Stage 0 inputs may differ')
    for name, digest in REVIEWED_DELTA.items():
        require(current.get(name) == digest, 'Unreviewed R20 touch input: ' + name)
    return sorted(delta)

def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    base = root / 'artifacts/hakios/lithium-ios16'
    contract_path = base / 'phase4-r20-display/contract.json'
    require(sha(contract_path) == R20_CONTRACT_SHA256, 'Frozen R20-display contract changed')
    contract = read(contract_path)
    require(sha(root / contract['image_path']) == R20_SHA256 and
            sha(base / 'baseline-desktop-r15.img') == R15_SHA256, 'Frozen R20/R15 image changed')
    require(contract['source_code_changed'] is False and contract['all_other_boot_arguments_preserved'] is True and
            contract['changed_boot_arguments'] == {'fbwidth': [750, 1080], 'fbheight': [1334, 2040]},
            'R20 must retain its accepted display-only image change')
    acceptance = read(root / 'reports/hakios-ios16-device-r20-display.json')
    require(acceptance.get('revision') == 'R20-display' and acceptance.get('image_sha256') == R20_SHA256 and
            acceptance.get('display_acceptance', {}).get('passed') is True, 'R20 display acceptance differs')
    # R20 reused the parent source byte-for-byte; consult that frozen manifest
    # solely as source ancestry, without calling any historical revision code.
    ancestor_path = base / 'phase4-r19/contract.json'
    require(sha(ancestor_path) == contract['evidence']['r19_contract_sha256'], 'R20 source ancestry changed')
    previous = read(ancestor_path)['inputs']
    require({k: v for k, v in previous.items() if k != 'port_files'} ==
            {k: v for k, v in current.items() if k != 'port_files'}, 'Non-port HakiOS baseline changed')
    delta = validate_delta(previous['port_files'], current['port_files'])
    port = root / 'ports/hakios-lithium'
    for name, digest in REVIEWED_DELTA.items():
        require(sha(port / name) == digest and sha(work / 'devices/lithium' / name) == digest,
                'Actual synchronized touch input differs: ' + name)
    admission = read(port / 'peripherals/contract.json')
    require(admission['default_off'] is True and admission['mmio_access'] is False and
            admission['touch_driver_implemented'] is False and admission['gpu_driver_implemented'] is False and
            admission['touch_stage0_static_report'] is True and admission['touch_stages_1_to_3_blocked'] is True,
            'Stage 0 must stay static, default-off and without a physical backend')
    return dict(passed=True, display_baseline='R20-display', exact_post_r20_source_delta=delta,
                physical_touch_backend=False, device_operations=False)
