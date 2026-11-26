#!/usr/bin/env python3
"""Freeze G3 read-only RAM discovery from PAS03; never build or contact a device."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OWN = ROOT / "evidence/gpu-metal-20261126/g3-package"
REVISION = "01"
OUT = ROOT / "evidence/gpu-metal-20261126/g3-package/g3-read-01"
BASE = ROOT / "evidence/gpu-metal-20261123/package/g2-zap-03"
BASE_RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g2-zap-03/result.json"
ATTEMPT = "gpu-g3-read-01"
PHASE = "g3-smmu-read"
IMAGE_NAME = "lithium-hakios-ios16-gpu-g3-read-01.img"
BASE_IMAGE_SHA = "5256c30541f9a34ef5cbb92b49b34819bde20c9f28dc84533d94329a2ac062d6"
KERNEL_SHA = "690b1b8f0ecdd632d5271c697352c04a6fccd6e9937e7558ec9147e9151e5034"
READ01_SOURCE_SHA = "eb13ae3296e713e748a4dd3ec33edc39a6426eae7136bc0a4c46eed4e5f4057f"
READ02_INPUTS = OWN / "read02-inputs.json"
BASE_ARGS = (
    "bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 "
    "fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 "
    "usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 "
    "cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 "
    "cpu_inherited_supply_r39=1 gpu_stage=1 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 "
    "gpu_power_clock=1 gpu_zap=2"
)
ARGS = BASE_ARGS + " gpu_smmu_diag=1"
PINS = {
    "hakios-kernel": KERNEL_SHA,
    "loader.efi": "10e867e941e05f216ac26e422ac3fcaaad04213f7e9cd81f8178d906e2ec7855",
    "lithium.fd": "3246df666702de479cf5ec0750142c7a70d05008840dd5d86cb43d008411c6ec",
    "fastboot-dxe.efi": "83393e5302856c9d93cce136400e2a9f001196b59ca5286bcc13371908a6177d",
    "loader.fat": "1d9acfbf956ac6a1af997e1e354abddadffa25ae04dd0fb3cb2eac60f55acb10",
    "root.apfs": "133d35b30af8f59c667b64aafe618b29f95355b84e7fde7e723863045ff332b8",
    "flashboot.apfs": "b48dd157a3aa981fb40ee7ba2d24628dc268acb48756c89492454a03bbb8d625",
    "ram-support.json": "2a030198b47181257865c36771d3191440115d10260c965e5d24fd6048ab9872",
    "kernel-result.json": "88e3da2211a1005491a3af7ae051952662b77ffe757884753527020d43b7a16f",
}
spec = importlib.util.spec_from_file_location("_g3_frozen_codec", ROOT / "scripts/package-hakios-gpu-g0.py")
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)
C.LOADER_SHA, C.FD_SHA, C.FASTBOOT_SHA = (PINS[k] for k in ("loader.efi", "lithium.fd", "fastboot-dxe.efi"))


def configure(attempt="01"):
    """Select a new attempt without changing read01's original admission rules."""
    global REVISION, OUT, ATTEMPT, IMAGE_NAME
    C.require(attempt in ("01", "02"), "unknown G3 attempt")
    REVISION = attempt
    OUT = OWN / ("g3-read-" + attempt)
    ATTEMPT = "gpu-g3-read-" + attempt
    IMAGE_NAME = "lithium-hakios-ios16-gpu-g3-read-" + attempt + ".img"


def read02_inputs():
    C.require(READ02_INPUTS.is_file(), "read02 kernel23/source pins have not been delivered")
    pin = C.read_json(READ02_INPUTS)
    C.require(pin.get("status") == "frozen_kernel23_sources_reviewed" and
              pin.get("revision") == "23" and (pin.get("kernel") or {}).get("sha256") and
              (pin.get("kernel_report") or {}).get("sha256"),
              "read02 awaits the actual passed kernel23 report and reviewed ID7/resume source hashes")
    sources = pin.get("sources", {})
    old_report = C.read_json(OWN / "g3-read-01/kernel-result.json")
    old_native = old_report["inputs"]["port_files"]["gpu/g2-live/pas-native.h"]
    C.require(sources.get("gpu/g3-live/live.c") not in (None, READ01_SOURCE_SHA) and
              sources.get("gpu/g2-live/pas-native.h") not in (None, old_native) and
              pin["kernel"]["sha256"] != KERNEL_SHA and
              pin.get("rules") == dict(id7_major_required=False, scm_interrupted_resume=True),
              "read02 source-bound ID7/resume profile is incomplete")
    return pin


def rules_for_kernel(report):
    """Interpret the packaged kernel's exact source hash, never today's port files."""
    sha = report.get("sha256")
    sources = report.get("inputs", {}).get("port_files", {})
    if sha == KERNEL_SHA and sources.get("gpu/g3-live/live.c") == READ01_SOURCE_SHA:
        return dict(profile="kernel22-extra-id7-check-no-native-resume", kernel_sha256=sha,
                    smmu_source_sha256=READ01_SOURCE_SHA,
                    pas_native_source_sha256=sources.get("gpu/g2-live/pas-native.h"),
                    id7_major_required=True, scm_interrupted_resume=False)
    pin = read02_inputs()
    C.require(sha == pin["kernel"]["sha256"] and
              all(sources.get(name) == digest for name, digest in pin["sources"].items()),
              "kernel/source hashes do not match a reviewed G3 interpretation profile")
    return dict(profile="kernel23-id7-observe-native-resume", kernel_sha256=sha,
                smmu_source_sha256=sources["gpu/g3-live/live.c"],
                pas_native_source_sha256=sources["gpu/g2-live/pas-native.h"], **pin["rules"])


def validate_args(args):
    C.require(args == ARGS, "G3 must append only gpu_smmu_diag=1 to actual PAS03 args")
    keys = [word.split("=", 1)[0] for word in args.split()]
    C.require(len(keys) == len(set(keys)), "duplicate G3 boot argument")


def layers(image, components, args):
    C.BOOT_ARGS = args
    C.validate_args = lambda actual: C.require(actual == args, "payload arguments differ")
    try:
        return C.verify_layers(image, components)
    finally:
        C.BOOT_ARGS, C.validate_args = ARGS, validate_args


def read_components(directory, contract):
    result = {}
    for name, expected in contract["components"].items():
        path = directory / name
        C.require(path.resolve().parent == directory.resolve() and not path.is_symlink(), "component escaped")
        result[name] = path.read_bytes()
        C.require(C.meta(result[name]) == expected, "component differs: " + name)
    return result


def base_inputs():
    contract = C.read_json(BASE / "contract.json")
    components = read_components(BASE, contract)
    name = contract["image"]["path"]
    C.require(C.meta(components[name]) == dict(bytes=16449536, sha256=BASE_IMAGE_SHA), "PAS03 image differs")
    C.require(contract["cmdline"] == contract["boot_args"] == BASE_ARGS, "PAS03 args differ")
    state = C.read_json(BASE_RUN)
    C.require(state["command_accepted"] is True and state["image_sha256"] == BASE_IMAGE_SHA and
              state["boot_args"] == BASE_ARGS, "PAS03 accepted image/args differ")
    for name, digest in PINS.items():
        C.require(C.sha(components[name]) == digest, "frozen input differs: " + name)
    for marker in (b"gpu_smmu_diag", b"LITHIUM SMMU discover", b"LITHIUM SMMU raw"):
        C.require(marker in components["hakios-kernel"], "kernel22 missing G3 marker")
    layers(components[contract["image"]["path"]], components, BASE_ARGS)
    return contract, components


def _verify_read01(package=None):
    selected = OUT if package is None else Path(package)
    C.require(selected.resolve() == OUT.resolve(), "G3 package differs")
    contract = C.read_json(OUT / "contract.json")
    validate_args(contract["cmdline"])
    C.require(contract["boot_args"] == ARGS and contract["schema"] == "lithium-g3-smmu-read-ram-v1" and
              contract["candidate"] == "GPU-G3-read-01" and contract["profile"] == "g3-smmu-read-ram" and
              contract["gpu_smmu_diag"] is True and contract["gpu_zap_mode"] == 2 and
              contract["smmu_writes"] == 0 and contract["smmu_stage1_armed"] is False and
              contract["gpu_submission_armed"] is False and contract["cp_commands"] == 0 and
              contract["loader_return_after"] == 180 and contract["return_fastboot_delay"] == 10 and
              contract["return_verification_window_seconds"] == [240, 900], "G3 scope differs")
    components = read_components(OUT, contract)
    _, previous = base_inputs()
    changed = {"Image", "payload.bin", "boot-args.txt"}
    base_image = next(n for n in previous if n.endswith(".img"))
    C.require(set(components) == (set(previous) - {base_image}) | {IMAGE_NAME}, "G3 component set differs")
    for name in previous.keys() - changed - {base_image}:
        C.require(components[name] == previous[name], "reused component changed: " + name)
    image = components[IMAGE_NAME]
    C.require(contract["image"] == dict(path=IMAGE_NAME, **C.meta(image)), "image manifest differs")
    actual = layers(image, components, ARGS)
    C.require(all(contract[k] == v for k, v in actual.items()), "G3 layer readback differs")
    base_entries = C.CODEC.parse_payload(previous["payload.bin"])
    new_entries = C.CODEC.parse_payload(components["payload.bin"])
    C.require(new_entries == {**base_entries, "cmdline": ARGS.encode("ascii") + b"\0"}, "payload changed beyond args")
    return dict(passed=True, image=str(OUT / IMAGE_NAME), image_sha256=C.sha(image), image_bytes=len(image),
        cmdline=ARGS, component_count=len(components), final_fv=actual["final_fv"],
        command_budget=actual["command_budget"], base_image_sha256=BASE_IMAGE_SHA,
        kernel_sha256=KERNEL_SHA, only_boot_arg_added="gpu_smmu_diag=1",
        device_operations=0, kernel_builds=0, runtime_verified=False)


def _assemble_read01():
    C.require(not OUT.exists(), "preserve existing or partial G3 package; use --verify-package")
    old, previous = base_inputs()
    raw_old, dtbs, _, header = C.CODEC.decode_android(previous[old["image"]["path"]])
    entries = C.CODEC.parse_payload(previous["payload.bin"])
    payload = C.CODEC.encode_payload([(name, ARGS.encode("ascii") + b"\0" if name == "cmdline" else value)
                                      for name, value in entries.items()])
    shim, _ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT], len(previous["payload.bin"]), len(payload))
    raw = shim + previous["lithium.fd"] + payload + C.NOP * ((-len(payload) % 16) // 4)
    image = C.LIVE.make_android(raw, dtbs, previous["loader.fat"], header)
    components = {name: value for name, value in previous.items() if name != old["image"]["path"]}
    components.update({"Image": raw, "payload.bin": payload, "boot-args.txt": (ARGS + "\n").encode(), IMAGE_NAME: image})
    checked = layers(image, components, ARGS)
    contract = dict(old)
    contract.update(schema="lithium-g3-smmu-read-ram-v1", candidate="GPU-G3-read-01", profile="g3-smmu-read-ram",
        cmdline=ARGS, boot_args=ARGS, gpu_smmu_diag=True, gpu_zap=True, gpu_zap_mode=2,
        gpu_power_window_armed=True, stage1_armed=False, smmu_stage1_armed=False, smmu_read_only=True,
        smmu_base="0xb40000", smmu_bytes=65536, smmu_cb_read_mask=1, smmu_read_cap=1024,
        smmu_budget_us=20000, smmu_writes=0, smmu_routing_writes=0, smmu_tlb_invalidations=0,
        smmu_irq_acks=0, smmu_table_allocation=False, gpu_submission_armed=False, cp_commands=0,
        expected_pas_calls=4, stock_direct_pas=True, return_verification_window_seconds=[240, 900],
        hardware_verified=False, runtime_verified=False, automatic_return_hardware_verified=False,
        provenance=dict(base_package=str(BASE), base_image_sha256=BASE_IMAGE_SHA, accepted_run=str(BASE_RUN),
                        only_boot_arg_added="gpu_smmu_diag=1", all_other_payload_entries_identical=True,
                        firmware_support_fat_unchanged=True, pas03_runtime_success_not_borrowed=True),
        inputs={**old["inputs"], "loader_sha256": PINS["loader.efi"], "ram_fd_sha256": PINS["lithium.fd"],
                "fastboot_dxe_sha256": PINS["fastboot-dxe.efi"], "kernel_sha256": KERNEL_SHA},
        observation_scope="SMMU reads only; existing G1 power/clock writes and stock-direct PAS13 operations retained",
        combined_summary_smmu_label="Frozen G1 end prints smmu=unread unconditionally; use LITHIUM SMMU records",
        image=dict(path=IMAGE_NAME, **C.meta(image)), components={n: C.meta(v) for n, v in components.items()}, **checked)
    OUT.mkdir(parents=True)
    for name, value in components.items():
        (OUT / name).write_bytes(value)
    (OUT / "contract.json").write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")
    result = verify_package()
    (OUT / "package-result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def _read01_inputs():
    """Read the immutable read01 package without recursively selecting read02."""
    configure("01")
    try:
        _verify_read01()
        contract = C.read_json(OUT / "contract.json")
        components = read_components(OUT, contract)
        return contract, components
    finally:
        configure("02")


def _verify_read02(package=None):
    pin = read02_inputs()
    selected = OUT if package is None else Path(package)
    C.require(selected.resolve() == OUT.resolve(), "read02 package differs")
    contract = C.read_json(OUT / "contract.json")
    validate_args(contract["cmdline"])
    C.require(contract["candidate"] == "GPU-G3-read-02" and
              contract["schema"] == "lithium-g3-smmu-read-ram-v1" and
              contract["profile"] == "g3-smmu-read-ram" and contract["boot_args"] == ARGS and
              contract["gpu_zap_mode"] == 2 and contract["gpu_smmu_diag"] is True and
              contract["smmu_read_only"] is True and contract["smmu_writes"] == 0 and
              contract["smmu_stage1_armed"] is False and contract["gpu_submission_armed"] is False and
              contract["cp_commands"] == 0 and contract["loader_return_after"] == 180 and
              contract["return_fastboot_delay"] == 10 and
              contract["return_verification_window_seconds"] == [240, 900], "read02 scope differs")
    components = read_components(OUT, contract)
    old, previous = _read01_inputs()
    replaced = {"hakios-kernel", "kernel-result.json", "payload.bin", "Image", old["image"]["path"]}
    C.require(set(components) == (set(previous) - {old["image"]["path"]}) | {IMAGE_NAME}, "read02 component set differs")
    for name in previous.keys() - replaced:
        C.require(components[name] == previous[name], "read02 changed an inherited component: " + name)
    C.require(C.meta(components["hakios-kernel"]) == pin["kernel"] and
              C.meta(components["kernel-result.json"]) == pin["kernel_report"], "read02 frozen kernel/report differ")
    report = json.loads(components["kernel-result.json"])
    rules = rules_for_kernel(report)
    C.require(report.get("passed") is True and report["sha256"] == pin["kernel"]["sha256"] and
              report["bytes"] == pin["kernel"]["bytes"] and
              report.get("kernel") == "/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-23" and
              rules["profile"] == "kernel23-id7-observe-native-resume" and contract["source_rules"] == rules,
              "read02 build or source interpretation differs")
    image = components[IMAGE_NAME]
    C.require(contract["image"] == dict(path=IMAGE_NAME, **C.meta(image)), "read02 image manifest differs")
    actual = layers(image, components, ARGS)
    C.require(all(contract[k] == v for k, v in actual.items()), "read02 layer readback differs")
    old_entries = C.CODEC.parse_payload(previous["payload.bin"])
    C.require(C.CODEC.parse_payload(components["payload.bin"]) ==
              {**old_entries, "kernel": components["hakios-kernel"]}, "read02 payload changed beyond kernel")
    return dict(passed=True, image=str(OUT / IMAGE_NAME), image_sha256=C.sha(image), image_bytes=len(image),
                cmdline=ARGS, component_count=len(components), final_fv=actual["final_fv"],
                command_budget=actual["command_budget"], kernel_sha256=pin["kernel"]["sha256"],
                comparison_image_sha256=old["image"]["sha256"], only_payload_entry_changed="kernel",
                source_rules=rules, device_operations=0, kernel_builds=0, runtime_verified=False,
                hardware_verified=False)


def _assemble_read02():
    C.require(not OUT.exists(), "preserve existing or partial read02 package")
    pin = read02_inputs()
    report_path = ROOT / "evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-23.json"
    report_bytes = report_path.read_bytes()
    C.require(C.meta(report_bytes) == pin["kernel_report"], "actual kernel23 report differs from reviewed pins")
    report = json.loads(report_bytes)
    C.require(report.get("passed") is True and report.get("kernel") ==
              "/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-23",
              "kernel23 frozen build result required")
    # Assembly runs under WSL, like the existing kernel packagers; verification
    # and the Windows runner need only the package-local copied inputs.
    kernel = Path(report["kernel"]).read_bytes()
    C.require(C.meta(kernel) == pin["kernel"], "actual kernel23 bytes differ from reviewed pins")
    C.LIVE.verify_kernel(kernel, "ram")
    rules = rules_for_kernel(report)
    old, previous = _read01_inputs()
    old_raw, dtbs, _, header = C.CODEC.decode_android(previous[old["image"]["path"]])
    entries = C.CODEC.parse_payload(previous["payload.bin"])
    payload = C.CODEC.encode_payload([(name, kernel if name == "kernel" else value) for name, value in entries.items()])
    shim, _ = C.LIVE.replace_copy_length(old_raw[:C.FD_AT], len(previous["payload.bin"]), len(payload))
    raw = shim + previous["lithium.fd"] + payload + C.NOP * ((-len(payload) % 16) // 4)
    image = C.LIVE.make_android(raw, dtbs, previous["loader.fat"], header)
    components = {name: value for name, value in previous.items() if name != old["image"]["path"]}
    components.update({"hakios-kernel": kernel, "kernel-result.json": report_bytes,
                       "payload.bin": payload, "Image": raw, IMAGE_NAME: image})
    checked = layers(image, components, ARGS)
    contract = dict(old)
    contract.update(candidate="GPU-G3-read-02", hardware_verified=False, runtime_verified=False,
        automatic_return_hardware_verified=False, gpu_hardware_verified=False,
        inputs={**old["inputs"], "kernel": report["kernel"], "kernel_report": str(report_path),
                "kernel_sha256": pin["kernel"]["sha256"]}, source_rules=rules,
        provenance=dict(comparison_package=str(OWN / "g3-read-01"), comparison_image_sha256=old["image"]["sha256"],
                        only_payload_entry_changed="kernel", boot_args_identical=True,
                        fat_loader_support_ram_volumes_identical=True, prior_run_success_not_borrowed=True),
        image=dict(path=IMAGE_NAME, **C.meta(image)), components={n: C.meta(v) for n, v in components.items()}, **checked)
    OUT.mkdir()
    for name, value in components.items():
        (OUT / name).write_bytes(value)
    (OUT / "contract.json").write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")
    result = _verify_read02()
    (OUT / "package-result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def verify_package(package=None):
    return _verify_read01(package) if REVISION == "01" else _verify_read02(package)


def assemble():
    return _assemble_read01() if REVISION == "01" else _assemble_read02()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02"), default="01")
    parser.add_argument("--verify-package", action="store_true", help="read-only layer and frozen-input verification")
    options = parser.parse_args()
    configure(options.attempt)
    print(json.dumps(verify_package() if options.verify_package else assemble(), indent=2))
