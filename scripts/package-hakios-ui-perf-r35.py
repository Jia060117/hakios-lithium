#!/usr/bin/env python3
"""Package R35 UFS UI profiles with the existing R34 touch-live packager.

Supply the completed new kernel and unchanged root.touch-live.apfs explicitly.
Default: quiet. Lite persists UI preferences on UFS Data; quiet does not undo
those preferences. CPU3 is diagnostic. --dry-run reads frozen metadata/payload
only, never the supplied kernel/root, and creates no files.
"""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
FROZEN_DIR = "phase4-r34-touch-live-ufs-final"
FROZEN_IMAGE = "lithium-hakios-ios16-r34-touch-live-ufs.img"
FROZEN_SHA = "47740664f56fcf021096493444cc96934223f49ce0dfc099920bfb73cfc426ed"
CONTRACT_SHA = "a97073359194258501b83c31fd2f316a9814d42c2f83817fa4fdf807c7b3568f"
ROOT_SHA = "38c69bbe8caf11ceb4e90fc6614976ef37a6c2ef51057a119e9c53d0ebe1e71c"
PROFILES = ("quiet", "lite", "cpu3")


def load_live():
    spec = importlib.util.spec_from_file_location(
        "r35_touch_live", ROOT / "scripts/package-hakios-touch-live.py")
    live = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(live)
    return live


def frozen_contract(live):
    directory = live.BASE / FROZEN_DIR
    raw = (directory / "contract.json").read_bytes()
    live.require(live.digest(raw) == CONTRACT_SHA, "frozen R34 UFS contract differs")
    contract = json.loads(raw)
    live.require(contract["image"]["sha256"] == FROZEN_SHA and
                 contract["inputs"]["root"]["sha256"] == ROOT_SHA and
                 contract["return_fastboot"] is False and
                 contract["ufs_automatic_return_supported"] is False,
                 "R34 UFS support/root/return contract differs")
    # Read the actual frozen kernel.ver entry for the existing loader budgeter.
    payload = (directory / "payload.bin").read_bytes()
    live.require(live.metadata(payload) == contract["components"]["payload.bin"],
                 "frozen R34 payload differs")
    codec, _ = live.bootstrap()
    entries = codec.parse_payload(payload)
    live.require(live.text_entry(entries["cmdline"]) == contract["cmdline"] and
                 live.metadata(entries["ramdisk"]) == contract["payload_entries"]["ramdisk"],
                 "frozen R34 payload command line/root differs")
    version = live.text_entry(entries["kernel.ver"])
    live.require(live.command_budget(contract["cmdline"], version, "ufs") ==
                 contract["command_budget"], "frozen R34 command budget differs")
    return contract, version


def profile_plan(live, frozen, version, profile):
    words = frozen["cmdline"].split()
    live.require(words.count("bootcpu=1") == words.count("cpus=1") == 1 and
                 not any(w.split("=", 1)[0] in ("applog_ui_quiet", "ui_lite") or
                         w.startswith("return_fastboot") for w in words),
                 "R34 baseline CPU/feature arguments differ")
    if profile == "cpu3":
        words[words.index("cpus=1")] = "cpus=3"
    words.append("applog_ui_quiet=1")
    if profile == "lite":
        words.append("ui_lite=1")
    args = " ".join(words)
    return {
        "profile": profile, "boot_args": args,
        "command_budget": live.command_budget(args, version, "ufs"),
        "bootcpu": 1, "cpus": 3 if profile == "cpu3" else 1,
        "feature_args": {"applog_ui_quiet": 1,
                         "ui_lite": 1 if profile == "lite" else "absent (default off)"},
        "ui_lite_preferences_may_persist_on_ufs_data": profile == "lite",
        "quiet_does_not_reset_previously_persisted_ui_lite_preferences": True,
        "cpu3_diagnostic_only": profile == "cpu3",
        "return_fastboot": False, "ufs_automatic_return_supported": False,
        "normal_boot_may_write_userdata": True,
    }


def package_one(live, frozen, plan, kernel, root, output, expected_kernel):
    # Only this freshly imported module is configured. Its package() performs
    # the existing actual boot.check_boot and all written-layer readbacks.
    def make_args(boot_profile, previous):
        live.require(boot_profile == "ufs" and previous == frozen["cmdline"],
                     "support command line differs from frozen R34 UFS")
        return plan["boot_args"]

    live.SUPPORT["ufs"] = (FROZEN_DIR, FROZEN_IMAGE, FROZEN_SHA)
    live.make_args = make_args
    with contextlib.redirect_stdout(io.StringIO()):
        contract = live.package(kernel, root, "ufs", output)
    live.require(contract["inputs"]["kernel"]["sha256"] == expected_kernel and
                 contract["inputs"]["root"]["sha256"] == ROOT_SHA,
                 "inputs changed while packaging; discard this output")
    old_name = contract["image"]["path"]
    image_name = "lithium-hakios-ios16-r35-ui-perf-" + plan["profile"] + ".img"
    image = output / image_name
    live.require(not image.exists(), "R35 image destination already exists")
    (output / old_name).rename(image)
    live.require(live.metadata(image.read_bytes()) ==
                 {key: contract["image"][key] for key in ("bytes", "sha256")},
                 "renamed image readback differs")
    contract["components"][image_name] = contract["components"].pop(old_name)
    contract["image"]["path"] = image_name
    contract["boot_topology"]["cpus"] = plan["cpus"]
    contract.update(
        schema="hakios-ui-perf-r35-package-v1",
        candidate="R35-ui-perf-" + plan["profile"],
        profile=plan["profile"], profile_plan=plan,
        default_profile="quiet", kernel_shared_across_selected_profiles=True,
        frozen_r34_contract_sha256=CONTRACT_SHA,
        root_unchanged_from_r34=True,
        touch_live_packager_sha256=contract["script_sha256"],
        script_sha256=live.digest(Path(__file__).read_bytes()))
    with (output / "contract.json").open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(contract, stream, indent=2)
        stream.write("\n")
    live.require(json.loads((output / "contract.json").read_text()) == contract,
                 "R35 contract readback differs")
    return contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True,
                        help="completed new arm64 Mach-O kernel; no build is invoked")
    parser.add_argument("--root", type=Path, required=True,
                        help="unchanged R34 root.touch-live.apfs (hash pinned)")
    parser.add_argument("--output-root", type=Path, required=True,
                        help="new private directory; selected profiles get child directories")
    parser.add_argument("--profile", choices=PROFILES + ("all",), default="quiet")
    parser.add_argument("--dry-run", action="store_true",
                        help="print profile/budget plan without reading new inputs or writing files")
    opts = parser.parse_args()
    live = load_live()
    try:
        kernel, root, output = (p.resolve() for p in
                                (opts.kernel, opts.root, opts.output_root))
        live.require(not output.exists(), "--output-root exists; choose a new directory")
        for parent in (output, *output.parents):
            revision = re.match(r"phase4-r(\d+)(?:-|$)", parent.name)
            live.require(not revision or int(revision[1]) > 34,
                         "output must not be inside a frozen R34 or older directory")
        frozen, version = frozen_contract(live)
        selected = PROFILES if opts.profile == "all" else (opts.profile,)
        plans = [profile_plan(live, frozen, version, p) for p in selected]
        result = {
            "schema": "hakios-ui-perf-r35-plan-v1", "dry_run": opts.dry_run,
            "package_host_ready": False, "default_profile": "quiet",
            "kernel": str(kernel), "root": str(root), "output_root": str(output),
            "required_root_sha256": ROOT_SHA, "frozen_support_sha256": FROZEN_SHA,
            "profiles": plans, "device_operations": False, "build_invoked": False,
        }
        if not opts.dry_run:
            kernel_data, root_data = kernel.read_bytes(), root.read_bytes()
            live.verify_kernel(kernel_data, "ufs")
            live.require(b"applog_ui_quiet" in kernel_data,
                         "new kernel lacks the default-off applog_ui_quiet argument")
            live.verify_root(root_data)
            live.require(live.digest(root_data) == ROOT_SHA,
                         "root must be byte-identical to frozen R34 root.touch-live.apfs")
            expected_kernel = live.digest(kernel_data)
            output.mkdir(parents=True)
            contracts = [package_one(live, frozen, plan, kernel, root,
                                     output / plan["profile"], expected_kernel)
                         for plan in plans]
            result.update(package_host_ready=True, kernel_sha256=expected_kernel,
                          images=[{"profile": c["profile"],
                                   "path": str(output / c["profile"] / c["image"]["path"]),
                                   "sha256": c["image"]["sha256"]} for c in contracts])
            with (output / "profiles.json").open("x", encoding="utf-8", newline="\n") as stream:
                json.dump(result, stream, indent=2)
                stream.write("\n")
        print(json.dumps(result, indent=2))
    except (OSError, RuntimeError, ValueError, KeyError, live.struct.error) as error:
        parser.exit(1, "ERROR: " + str(error) + "\n")


if __name__ == "__main__":
    main()
