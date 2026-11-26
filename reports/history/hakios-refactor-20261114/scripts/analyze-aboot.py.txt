"""Reproduce stock aboot's analysis-only ELF and annotated ARM disassembly.

The input is never changed. The derived ELF has synthetic section metadata;
it is for disassemblers only and must never be sent to a device.
"""
from pathlib import Path
import hashlib
import json
import struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = "91835d1f70edaad214d72dca22f13230edcfd62849c3f5b79409d1cc673e3081"
RANGES = {
    "address_policy": (0x92a37840, 0x92a378b4),
    "handoff": (0x92a38ca8, 0x92a38f04),
    "scm_aarch64": (0x92a1ebfc, 0x92a1ed40),
    "fastboot_decompression_and_copy": (0x92a3ad98, 0x92a3b268),
    "gzip_detection": (0x92a6d350, 0x92a6d3cc),
    "runtime_ram_base": (0x92a05554, 0x92a05594),
}


def main():
    source, = ROOT.glob("lithium_images_*/images/emmc_appsboot.mbn")
    original = source.read_bytes()
    assert hashlib.sha256(original).hexdigest() == EXPECTED
    assert original[:7] == b"\x7fELF\x01\x01\x01"
    entry, phoff = struct.unpack_from("<II", original, 24)
    phentsize, phnum = struct.unpack_from("<HH", original, 42)
    loads = [struct.unpack_from("<8I", original, phoff + i * phentsize)
             for i in range(phnum)]
    load, = [p for p in loads if p[0] == 1 and p[2] == entry]
    _, offset, base, physical, size, memsize, flags, alignment = load
    payload = original[offset:offset + size]
    assert len(payload) == size and base == physical == entry == 0x92a00000
    derived = bytearray(original)
    names = b"\0.text\0.shstrtab\0"
    names_offset = len(derived)
    derived.extend(names)
    derived.extend(b"\0" * (-len(derived) % 4))
    shoff = len(derived)
    derived.extend(bytes(40))
    derived.extend(struct.pack("<10I", 1, 1, 6, base, offset, size, 0, 0, 4, 0))
    derived.extend(struct.pack("<10I", 7, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0))
    struct.pack_into("<I", derived, 32, shoff)
    struct.pack_into("<HHH", derived, 46, 40, 3, 2)
    out = ROOT / "artifacts/research/aboot"
    out.mkdir(parents=True, exist_ok=True)
    (out / "aboot.analysis.elf").write_bytes(derived)
    (out / "aboot.pt-load.bin").write_bytes(payload)
    disassembler = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    disassembler.skipdata = True
    for name, (start, end) in RANGES.items():
        text = "\n".join(f"{i.address:08x}: {i.mnemonic:8} {i.op_str}"
                         for i in disassembler.disasm(payload[start-base:end-base], start))
        (out / (name + ".txt")).write_text(text + "\n", encoding="utf-8")
    report = dict(source=source.relative_to(ROOT).as_posix(), sha256=EXPECTED,
                  entry=hex(entry), load_start=hex(base), load_end=hex(base + memsize),
                  analysis_elf_is_flashable=False,
                  observations={
                      "arm64_magic": "0x92a38dd4..0x92a38de4 compares Image+0x38 with 0x644d5241 (ARMd)",
                      "arm64_handoff": "0x92a38ee4..0x92a38ef4 passes kernel and DT physical addresses to 0x92a1ebfc",
                      "scm": "0x92a1ebfc uses SCM service 1, command 15; kernel at argument+0x48, DT at argument+0; fallback call ID 0x0200010f",
                      "address_override": "0x92a37840 overrides header addresses: runtime RAM base + 0x80000 for ARM64, +0x2800000 DT, +0x2a00000 ramdisk",
                      "runtime_base": "0x92a05554 loads a runtime 64-bit global; base=0x40000000 inferred from observed initrd=0x42a00000, pending probe PC proof",
                      "gzip_fastboot": "0x92a3b190 decompresses gzip to a scratch buffer; 0x92a3b07c..0x92a3b088 copies decompressed bytes to the overridden kernel address",
                  },
                  limitations=["Firmware has no symbols; function roles inferred from data flow and messages.",
                               "Static analysis cannot establish the final EL/register/cache state.",
                               "No secure firmware changes or commands are issued by this script."])
    (ROOT / "reports/aboot-entry.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    assert source.read_bytes() == original
    print("Reproduced analysis ELF and six disassembly ranges; original aboot unchanged.")


if __name__ == "__main__":
    main()
