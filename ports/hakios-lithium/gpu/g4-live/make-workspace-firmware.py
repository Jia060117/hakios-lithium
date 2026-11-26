#!/usr/bin/env python3
"""Generate ONLY the ignored workspace header from already-local stock blobs.

No bytes in source/evidence, no downloads, no firmware-bearing object output.
Use under WSL; the output lives outside the repository in the active workspace.
"""
import hashlib
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
WORKSPACE = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')
OUTPUT = WORKSPACE / 'local-inputs/gpu-cp/g4-pm4-pfp-local.inc'
STOCK = {
    'pm4': ('a530_pm4.fw', 19572, '6419f35956ec7307af83723fedfba752520bacd8389eda0d0120e185e4cb1d3f'),
    'pfp': ('a530_pfp.fw', 15876, 'ed2c860ae56c5061d630b40cbe0aae6b0c4c4d0422b91b838973fbee66a3b00e'),
}

def main():
    if not WORKSPACE.is_dir():
        raise SystemExit('Use the existing active WSL XNU workspace; no fallback destination.')
    # Resolve all paths first. Refuse symlinks that could redirect vendor data.
    if OUTPUT.resolve().is_relative_to(ROOT.resolve()):
        raise SystemExit('Refusing firmware bytes anywhere in repository source/evidence.')
    if not OUTPUT.resolve().is_relative_to(WORKSPACE.resolve() / 'local-inputs'):
        raise SystemExit('Workspace destination escaped local-inputs.')
    blobs = []
    for symbol, (name, size, digest) in STOCK.items():
        path = ROOT / 'artifacts/local-firmware/lithium-a530' / name
        subprocess.run(['git', 'check-ignore', '-q', '--', str(path)], cwd=ROOT, check=True)
        tracked = subprocess.check_output(['git', 'ls-files', '--', str(path)], cwd=ROOT)
        if tracked:
            raise SystemExit('Stock firmware unexpectedly tracked.')
        data = path.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit('Local stock firmware digest/size mismatch: ' + name)
        blobs.append((symbol, data))
    lines = ['/* LOCAL WORKSPACE ONLY: vendor bytes, NEVER copy into source/evidence. */',
             '#ifndef LITHIUM_G4_LOCAL_FW_INC', '#define LITHIUM_G4_LOCAL_FW_INC',
             '#include "g4-live.h"']
    for symbol, data in blobs:
        lines.append(f'static const uint8_t g4l_stock_{symbol}[] = {{')
        lines.extend('    ' + ','.join(f'0x{x:02x}' for x in data[i:i+16]) + ','
                     for i in range(0, len(data), 16))
        lines.append('};')
    lines.extend(['static const struct g4l_firmware g4l_stock_files[2] = {',
                  '    {g4l_stock_pm4,sizeof(g4l_stock_pm4)},',
                  '    {g4l_stock_pfp,sizeof(g4l_stock_pfp)}', '};', '#endif', ''])
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text('\n'.join(lines), encoding='ascii')
    print('Generated workspace-only header; raw files unchanged; no firmware-bearing object generated.')
    print(OUTPUT)

if __name__ == '__main__':
    main()
