#!/usr/bin/env python3
"""Exercise the generated DXE callbacks and pre-doorbell transport gate."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("r21_overlay", HERE / "r21-readonly-overlay.py")
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument("--compiler", default="clang")
    args = parser.parse_args()
    sources = overlay.build_dxe()
    dxe = sources[overlay.DRIVER / "LithiumUfsDxe.c"]
    tx = sources[overlay.DRIVER / "WarmUfs.inc"]
    loader = overlay.build_loader(args.loader)
    kernel = overlay.build_input()
    assert ".ReadOnly=TRUE" in dxe and "mWriteEnabled" not in dxe
    assert "0x2a" not in tx and "0x35" not in tx
    assert "->write_blocks" not in loader and "->flush_blocks" not in loader
    assert "block_shared.native.hc_pa =" not in loader and "logdump" not in loader
    assert 'static int block_write_requested' not in loader
    assert '#include "../devices/lithium/peripherals/r21-fastboot-stage.h"' in loader
    assert 'static int lithium_r21_touch_stage = -1;' in loader
    assert 'lithium_r21_stage_command(cmd, &requested_stage)' in loader
    assert 'lithium_r21_stage_append(ba->commandLine, n, sizeof(ba->commandLine), selected_touch_stage)' in loader
    assert loader.index('if (*rest != 0)') < loader.index('ba->commandLine[n] = 0;')
    assert loader.index('ba->commandLine[n] = 0;') < loader.index('lithium_r21_stage_append(')
    assert 'touch-stage selector collides or does not fit' in loader
    contract = json.loads(overlay.read_pinned(overlay.CONTRACT))
    denied = {"var_hfs=ufs_darwin_data", "ufs_write=1", "hfs_bench=1"}
    expected_bootargs = " ".join(token for token in contract["cmdline"].split(" ") if token not in denied)
    assert overlay.bootargs() == expected_bootargs
    assert all(item in overlay.bootargs().split(" ") for item in
               ("bootcpu=1", "cpus=1", "ufs=2", "root_apfs=ufs_userdata",
                "lithiumusb=2", "gl=0", "metal=0", "fbwidth=1080", "fbheight=2040"))
    assert all(item not in overlay.bootargs().split(" ") for item in denied)
    assert "touch_stage=" not in overlay.bootargs()
    assert all(".." not in name.parts and not name.is_absolute() for name in kernel)

    # Preserve the generated callback bodies verbatim. In scsi, replace only
    # the code AFTER the guard with a count of submissions. Rejected CDBs
    # must return before this continuation, just as before descriptor setup.
    callbacks = dxe[dxe.index("STATIC EFI_STATUS EFIAPI Flush("):
                    dxe.index("STATIC EFI_STATUS EFIAPI Read(")]
    gate = tx[tx.index("static uint32_t scsi("):tx.index("\tzero(&utrl[0]")]
    harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "r21-ufs-readonly.h"
#define STATIC static
#define EFIAPI
#define EFI_SUCCESS 0u
#define EFI_MEDIA_CHANGED 1u
#define EFI_WRITE_PROTECTED 2u
#define EFI_DEVICE_ERROR 3u
typedef uint32_t EFI_STATUS;
typedef uint32_t UINT32;
typedef uint64_t UINTN;
typedef uint64_t EFI_LBA;
typedef void VOID;
typedef struct { unsigned ignored; } EFI_BLOCK_IO_PROTOCOL;
static unsigned mActive = 1, mDead;
static struct { UINT32 MediaId; } mMedia = {42};
static uint8_t data[65536];
static unsigned submissions;
""" + gate + r"""
    ++tag; *status_out = tag; ++submissions; return 0;
}
""" + callbacks + r"""
int main(void)
{
    uint32_t ocs = 0, status = 0;
    for (unsigned op = 0; op < 256; ++op) {
        unsigned char cdb[10] = {(unsigned char)op};
        const unsigned before = submissions;
        const unsigned admitted = op == 0 || op == 0x25 || op == 0x28;
        unsigned size = op == 0 ? 0 : op == 0x25 ? 8 : 4096;
        if (op != 0 && op != 0x25) cdb[8] = 1;
        assert(scsi(0, cdb, op == 0 ? 6 : 10, data, size, &ocs, &status) ==
            (admitted ? 0u : 5u));
        assert(submissions - before == admitted);
        const unsigned after = submissions;
        assert(scsi(1, cdb, 10, data, size, &ocs, &status) == 5);
        assert(submissions == after);
    }
    const unsigned before = submissions;
    assert(Write(NULL, 42, 0, 4096, data) == EFI_WRITE_PROTECTED);
    assert(Write(NULL, 42, UINT64_MAX, UINT64_MAX, NULL) == EFI_WRITE_PROTECTED);
    assert(Write(NULL, 42, 0, 0, NULL) == EFI_WRITE_PROTECTED);
    assert(Write(NULL, 99, 0, 4096, data) == EFI_MEDIA_CHANGED);
    assert(Flush(NULL) == EFI_SUCCESS && submissions == before);
    mActive = 0;
    assert(Flush(NULL) == EFI_DEVICE_ERROR);
    mActive = 1; mDead = 1;
    assert(Flush(NULL) == EFI_DEVICE_ERROR);
    assert(Write(NULL, 42, 0, 4096, data) == EFI_WRITE_PROTECTED);
    unsigned char tur[6] = {0};
    assert(scsi(0, tur, 6, NULL, 0, &ocs, &status) == 5);
    assert(submissions == before);
    puts("PASS generated DXE: callbacks and all opcode gates reject without submission");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="lithium-r21-ro-test-") as temp:
        root = Path(temp)
        (root / "r21-ufs-readonly.h").write_text(
            sources[overlay.DRIVER / "r21-ufs-readonly.h"], encoding="utf-8")
        source = root / "test.c"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([args.compiler, "-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-g", "-O1", "-fno-omit-frame-pointer",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(source), "-o", str(root / "test")], check=True, timeout=60)
        subprocess.run([str(root / "test")], check=True, timeout=30)
    print("PASS overlay: only new paths, loader writes/log persistence/native offer removed")


if __name__ == "__main__":
    main()
