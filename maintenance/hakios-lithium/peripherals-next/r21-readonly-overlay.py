#!/usr/bin/env python3
"""Emit the R21 read-only storage overlay into a fresh host directory."""

import argparse
import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DRIVER = Path("lithiumPkg/Drivers/LithiumUfsDxe")
SOURCE = ROOT / "ports/hakios-lithium/firmware" / DRIVER
CONTRACT = ROOT / "artifacts/hakios/lithium-ios16/phase4-r20-display/contract.json"
PERIPHERALS = ROOT / "ports/hakios-lithium/peripherals"
LOADER_HEADER = "../devices/lithium/peripherals/r21-fastboot-stage.h"
PINNED = {
    "LithiumUfsDxe.c": "114b66926adb28b8ffbcea0cc5d78fc64d9c5ef4283eddff7d86161c60de08e1",
    "WarmUfs.inc": "40cc28fb6eebec3afbbf604a5f5c6060620caeaf4bae682c8ebae1190d56a2f9",
    "loader.c": "addecda569872ba830c435cbd8507bf1957b642bf1934b45ef5451a7482f11d9",
    "contract.json": "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c",
    "kernel.inc": "b7ca2079f6f76cbeb288e54adf123f86ba57a5cdfeb8198ce716aa8df74e0ac9",
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_pinned(path):
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == PINNED[path.name], f"Unreviewed input: {path}")
    return raw.decode("utf-8").replace("\r\n", "\n")


def replace(source, old, new):
    require(source.count(old) == 1, f"Review changed anchor: {old[:75]!r}")
    return source.replace(old, new, 1)


def span(source, start, end, new):
    begin = source.index(start)
    finish = source.index(end, begin + len(start))
    return replace(source, source[begin:finish], new)


def build_dxe():
    dxe = read_pinned(SOURCE / "LithiumUfsDxe.c")
    dxe = replace(dxe, "#include <LithiumStorage.h>\n", "")
    dxe = replace(dxe, "#include <Uefi/UefiGpt.h>\n", "")
    dxe = replace(dxe, "mActive, mDead, mWriteEnabled;", "mActive, mDead;")
    dxe = span(dxe, "STATIC EFI_STATUS EFIAPI Flush(", "STATIC EFI_STATUS EFIAPI Read(", """\
STATIC EFI_STATUS EFIAPI Flush(EFI_BLOCK_IO_PROTOCOL *This) {
  return mActive && !mDead ? EFI_SUCCESS : EFI_DEVICE_ERROR; // no device command
}
STATIC EFI_STATUS EFIAPI Write(EFI_BLOCK_IO_PROTOCOL *This, UINT32 Media, EFI_LBA Lba, UINTN Size, VOID *Buffer) {
  if (Media != mMedia.MediaId) return EFI_MEDIA_CHANGED;
  return EFI_WRITE_PROTECTED;
}
""")
    dxe = span(dxe, "// Read the real GPT", "EFI_STATUS EFIAPI LithiumUfsEntry(", "")
    dxe = span(dxe, "  mWriteEnabled=CheckWritableGpt();", "  SaveState(6,", """\
  DEBUG((DEBUG_ERROR,"LITHIUM UFS READY LUN0 last=%lx block=%u Data_write=0 policy=r21-readonly\\n",mMedia.LastBlock,BLOCK));
""")
    tx = read_pinned(SOURCE / "WarmUfs.inc")
    tx = replace(tx, "#define UFS_BASE", '#include "r21-ufs-readonly.h"\n\n#define UFS_BASE')
    tx = span(tx, " * Issue one bounded SCSI command", " * Returns 0", " * Issue one bounded, read-only SCSI command in slot 0.\n")
    tx = span(tx, "\tBOOLEAN Write =", "\tzero(&utrl[0]", """\
\tif (!mActive || mDead || !ocs_out || !status_out ||
\t    !lithium_r21_ufs_admit(lun, cdb, cdb_len, buf, len, data, sizeof(data))) return 5;
""")
    tx = replace(tx, "len ? (Write ? 0x20 : 0x40) : 0x00", "len ? 0x40 : 0x00")
    tx = replace(tx, "len ? (Write ? 1u : 2u) : 0u", "len ? 2u : 0u")
    return {DRIVER / name: content for name, content in
            (("LithiumUfsDxe.c", dxe), ("WarmUfs.inc", tx),
             ("r21-ufs-readonly.h", (HERE / "r21-ufs-readonly.h").read_text(encoding="utf-8")))}


def build_loader(source):
    loader = read_pinned(source)
    loader = replace(loader, '#include "identity.h"',
        '#include "identity.h"\n#include "' + LOADER_HEADER + '"\nstatic int lithium_r21_touch_stage = -1;')
    marker = "\tprint(\"cmdline: %d of %d characters\\n\", (uint64_t)n, (uint64_t)(BOOT_LINE_LENGTH - 1));"
    loader = replace(loader, marker,
        "\tba->commandLine[n] = 0;\n"
        "\tint selected_touch_stage = lithium_r21_stage_consume(&lithium_r21_touch_stage);\n"
        "\tint touch_stage_appended = lithium_r21_stage_append(ba->commandLine, n, sizeof(ba->commandLine), selected_touch_stage);\n"
        "\tif (!touch_stage_appended) {\n"
        "\t\tprint(\"touch-stage selector collides or does not fit\\n\"); return 0;\n\t}\n"
        "\tif (selected_touch_stage >= 0) print(\"LITHIUM TOUCH selector=%d volatile=1 mmio=0\\n\", (uint64_t)selected_touch_stage);\n"
        "\tn = (uint32_t)str_len(ba->commandLine);\n" + marker)
    loader = replace(loader, '#define LOG_PARTITION       u"logdump"\n', '')
    loader = span(loader, "/* Log persistence:", "/*\n * The \"staged\" buffer", "/* Volatile log staging */\n\n")
    loader = replace(loader, "last run. It is what gets written to the log partition and what the host\n * fetches over USB (fastboot get_staged).", "last run. The host fetches it over USB (fastboot get_staged).")
    loader = span(loader, "/*\n * Where the log is read back", "static const char *staged_data", "")
    loader = span(loader, "static int hc_log_skips;", "static void hc_print_state", "")
    loader = span(loader, "\tefi_block_io *bio = find_log_partition();", "\n}\n\nstatic void\nflush_log", """\
\tcompose_logs(klog, klog_len);
\tlithium_return_screen(klog, klog_len);""")
    loader = replace(loader, "\thc_log_skips = 0;\n", "")
    busy_anchor = "\tint busy = run_active;\n\n\t{\n\t\tif (busy && (starts_with(cmd, \"oem ramdisk\")"
    busy_replacement = """\tint busy = run_active;
\tint requested_stage;
\tint stage_command = lithium_r21_stage_command(cmd, &requested_stage);

\t{
\t\tif (stage_command > 0) {
\t\t\tif (busy) fb_reply(\"FAIL\", \"a kernel is running (oem stop ends it)\");
\t\t\telse { lithium_r21_touch_stage = requested_stage; fb_reply(\"OKAY\", \"\"); }
\t\t} else if (stage_command < 0) {
\t\t\tfb_reply(\"FAIL\", \"bad touch stage\");
\t\t} else if (busy && (starts_with(cmd, \"oem ramdisk\")"""
    loader = replace(loader, busy_anchor, busy_replacement)
    loader = span(loader, '\t\tblock_write_requested |= starts_with(c, "ufs_write=1")', "\t}\n\tif (battery_requested)", "")
    loader = span(loader, "\t\t} else if (op == BLOCK_OP_WRITE ||", "\t\t} else if (op == BLOCK_OP_READ)", """\
\t\t} else if (op == BLOCK_OP_WRITE || op == BLOCK_OP_WRITE_FAST || op == BLOCK_OP_FLUSH) {
\t\t\tif (block_writes_refused++ < 8) print("LITHIUM UFS refused write/flush policy=r21-readonly\\n");
\t\t\ts = 0x800000000000000FULL; /* EFI_ACCESS_DENIED */
\t\t\tbytes = 0;
""")
    loader = span(loader, "\t\tif (block_write_requested && block_is_darwin", "\t\tblock_bios[", "")
    loader = span(loader, "\tblock_writes = block_writes_refused", "\tblock_shared.magic =", "\tblock_writes_refused = 0;\n")
    loader = span(loader, "\t/* a run that does not ask for writes", "\tblock_probe_requested = 0;", "")
    loader = span(loader, "\tif (ufs_requested >= 2) {\n\t\thc_init();", "\tif (ufs_requested >= 1)", "")
    loader = span(loader, "\tif (block_write_requested) {", "\t/* ... and its other cores", "")
    loader = span(loader, "static int block_write_requested;", "\n", "")
    loader = span(loader, "static int block_write_part =", "\n", "")
    loader = span(loader, "static uint64_t block_writes,", "\n", "static uint64_t block_writes_refused;")
    loader = replace(loader, "static int block_rawbench;\n", "")
    loader = span(loader, "/*\n * Is this partition the one a write may reach?", "static void\nblock_start", "")
    loader = span(loader, "/*\n * Before a run (ufs_native=)", "/*\n * The firmware's configuration tables", "")
    loader = replace(loader, "static uint8_t hc_area[HC_AREA_SIZE] __attribute__((aligned(0x2000)));\n", "")
    loader = span(loader, "static int hc_requested;", "\n", "")
    loader = span(loader, "\t/* the kernel's own transfer list: this loader's area", "\thc_refused_reads = 0;", "")
    require("->write_blocks" not in loader and "->flush_blocks" not in loader and
            "block_shared.native.hc_pa =" not in loader, "Loader still has a write path")
    return loader


def bootargs():
    parent = json.loads(read_pinned(CONTRACT))["cmdline"].split(" ")
    denied = {"var_hfs=ufs_darwin_data", "ufs_write=1", "hfs_bench=1"}
    require(denied.issubset(parent), "R20 writable options changed")
    args = [item for item in parent if item not in denied]
    require(all(item.partition("=")[0] not in {"ufs_write", "ufs_native", "ufs_rawbench",
            "var_store", "var_hfs", "hfs_bench", "touch_stage"} for item in args), "Unexpected option")
    return " ".join(args)


def build_input():
    kernel = read_pinned(PERIPHERALS / "kernel.inc")
    kernel = replace(kernel, "static struct lithium_peripheral_options", '#include "touch-r21-kernel.inc"\n\nstatic struct lithium_peripheral_options')
    kernel = replace(kernel, "    if (length == 4096 ||", "    lithium_r21_touch_start(args, length);\n    if (length == 4096 ||")
    files = {"kernel.inc": kernel}
    files.update({name: (HERE / name).read_text(encoding="utf-8") for name in
                  ("touch-r21-kernel.inc", "touch-r21-options.h", "touch-static.h", "r21-fastboot-stage.h")})
    return {Path("devices/lithium/peripherals") / name: data for name, data in files.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loader", type=Path, required=True, help="reviewed patched HakiOS loader.c")
    parser.add_argument("--output", type=Path, required=True, help="new directory for candidate sources")
    args = parser.parse_args()
    files = build_dxe()
    files.update(build_input())
    files[Path("xnuloader/loader.c")] = build_loader(args.loader)
    files[Path("bootargs.txt")] = bootargs() + "\n"
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for rel, data in files.items():
        dest = out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(data, encoding="utf-8", newline="\n")
        print(hashlib.sha256(dest.read_bytes()).hexdigest(), rel.as_posix())


if __name__ == "__main__":
    main()
