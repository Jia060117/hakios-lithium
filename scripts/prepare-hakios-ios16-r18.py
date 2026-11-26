"""Prepare the early loader display correction; preserve all frozen revisions."""
import hashlib
import json
from pathlib import Path
import hakios
import hakios_r18
from hakios_boot_observer import patch_for

def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.workspace(spec)
    hakios.check_common(work, hakios.read(work/"ios-mix1-workspace.json"))
    port = hakios.ROOT/spec["port_path"]
    backup = hakios.read(Path("/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/display-before-r18-20261118/backup.json"))
    hakios.require(hakios.sha(Path(backup["archive"].replace("E:\\", "/mnt/e/").replace("\\","/"))) == backup["sha256"], "Source backup differs")
    path = port/"diagnostics/r18.json"
    names = ("xnuloader/loader.c", "userland/src/inject_sb.c")
    if not path.exists():
        before = {name:(work/name).read_text() for name in names}
        after = {names[0]:hakios_r18.loader_source(before[names[0]]), names[1]:hakios_r18.springboard_source(before[names[1]])}
        patch = "patches/0018-lithium-early-display-profile.patch"
        (port/patch).write_text("".join(patch_for(n,before[n],after[n]) for n in names), encoding="utf-8", newline="\n")
        # Retain original R16 overlay separately; R17 is historical and unchanged.
        overlay = (port/"diagnostics/display-userspace.inc").read_text()
        overlay = overlay.replace("/* Measurements only, except for retaining UIKit\'s native 2x/1334 status bar. */",
            "/* R18: retained 750x1334@2 profile; actual runtime measurements only. */", 1)
        old = '    if (sample == 0) lithium_display_mg();'
        new = '    say("LITHIUM DISPLAY PROFILE sample=%u expected=375x667@2 actual=%gx%g@%g native=%gx%g@%g match=%u\\n",\n'
        new += '        sample, bounds.size.width, bounds.size.height, scale, native.size.width, native.size.height, native_scale,\n'
        new += '        b && s && n && ns && bounds.size.width == 375 && bounds.size.height == 667 && scale == 2 &&\n'
        new += '        native.size.width == 750 && native.size.height == 1334 && native_scale == 2);\n'
        new += old
        hakios.require(overlay.count(old) == 1, "Unexpected display measurement anchor")
        overlay = overlay.replace(old, new, 1)
        (port/"diagnostics/display-userspace-r18.inc").write_text(overlay, encoding="utf-8", newline="\n")
        digest = lambda s:hashlib.sha256(s.encode()).hexdigest()
        prior = hakios.read(hakios.ROOT/"artifacts/hakios/lithium-ios16/phase4-r15/contract.json")
        hakios.write(path, dict(revision=18, base_revision=17, runtime_reference_revision=16,
            before_sha256={n:digest(before[n]) for n in names}, after_sha256={n:digest(after[n]) for n in names},
            patch=patch, backup=backup, payload_boot_args=hakios_r18.command_line(prior["cmdline"]),
            payload_arguments_preserved_from_r15=True, render=[750,1334], scale=2, subtype=1334, points=[375,667],
            panel=[1080,2040], panel_stride=3240, panel_format="BGR24", surface_format="BGRA32",
            bootcpu=1, cpus=1, expected_affinity="0x1", hardware_verified=False, display_acceptance_verified=False,
            physical_stride_changed=False, compositor_changed=False, new_boot_args=False, userdata_reinstall_required=False,
            notes="Correct flattened DT producer before XNU; exact reason MG retained3 in R16 is not yet hardware-verified."))
    else:
        c = hakios.read(path)
        hakios.require(all(hakios.sha(work/n) in (c["before_sha256"][n], c["after_sha256"][n]) for n in names), "Unknown preexisting R18 source state")
    hakios.prepare(spec, source)
    print("R18 prepared: retained750x1334 profile serialized before XNU; no device access")

if __name__ == "__main__": main()
