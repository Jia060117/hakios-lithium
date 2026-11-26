#!/usr/bin/env python3
"""Generate build-only stock ZAP inputs outside the repository."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "artifacts/local-firmware/lithium-a530"
WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
FILES = (
    ("a530_zap.mdt", 6684, "0ac7434112cef8fda24539bcf5b72e55e10ac709dc2358547a83bf9a06856e73"),
    ("a530_zap.b00", 148, "1f384b70d68863407d66f2ba328b9b7729ab149b43bbb5d5f7da9e21f9221de2"),
    ("a530_zap.b01", 6536, "d33640fa05e9111007c2f71e35de5c41658c807cd837501af1b5d202a5771a14"),
    ("a530_zap.b02", 4900, "bef98d709227d3a45d046439c1dc6a7af5ce607df6ba960f99417fb569f5700f"),
)


def prepare():
    assert WORK.is_dir() and not WORK.resolve().is_relative_to(ROOT.resolve())
    destination = WORK / "local-inputs/gpu-zap/zap-local-inputs.inc"
    rows, declarations = [], []
    for index, (name, size, expected) in enumerate(FILES):
        path = FIRMWARE / name
        subprocess.run(["git", "check-ignore", "--quiet", "--", str(path.relative_to(ROOT))],
                       cwd=ROOT, check=True)
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        assert len(data) == size and digest == expected, "stock ZAP input differs: " + name
        symbol = ("lithium_g2_zap_mdt", "lithium_g2_zap_b00", "lithium_g2_zap_b01", "lithium_g2_zap_b02")[index]
        declarations.append("const uint8_t " + symbol + "[]={\n" +
            "\n".join(",".join(f"0x{value:02x}" for value in data[offset:offset + 24]) + ","
                      for offset in range(0, len(data), 24)) + "\n};\n")
        rows.append(dict(path=str(path.relative_to(ROOT)), bytes=size, sha256=digest, symbol=symbol))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(declarations), encoding="ascii")
    return dict(generated_outside_repository=True, included_in_source_patch=False,
                output=str(destination), sha256=hashlib.sha256(destination.read_bytes()).hexdigest(), inputs=rows)


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    print(json.dumps(prepare(), indent=2))
