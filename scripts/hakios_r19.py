"""R19: freeze current provenance while retaining the R18 display experiment."""
import hashlib
import json
from pathlib import Path

PORT_DELTA = {
    "patches/0019-lithium-peripheral-admission.patch",
    "peripherals/boot-options.h", "peripherals/contract.json", "peripherals/kernel.inc",
}
R18_CONTRACT_SHA256 = "2220293e49729357f66971adc5ff15dfb493f686c7a400676c1227b68ed53599"
PERIPHERAL_ARGS = {"touch", "touch_log", "touch_poll", "gpu_probe", "gpu_stage", "gpu_wd"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def validate_delta(prior, current):
    delta = {name for name in set(prior) | set(current) if prior.get(name) != current.get(name)}
    require(delta == PORT_DELTA, "R19 permits exactly the reviewed four admission additions")
    require(all(name not in prior and name in current for name in delta),
            "R19 additions must not replace or remove an R18 source")
    return sorted(delta)


def command_line(prior, baseline):
    require(isinstance(prior, str) and prior == baseline, "R19 must preserve the exact R15/R18 command line")
    words = prior.split()
    for token in ("bootcpu=1", "cpus=1", "ufs=2", "root_apfs=ufs_userdata",
                  "var_hfs=ufs_darwin_data", "ufs_write=1", "lithiumusb=2", "gl=0", "metal=0",
                  "fbwidth=750", "fbheight=1334", "loader_return_after=10800"):
        name = token.split("=", 1)[0]
        require(words.count(token) == 1 and sum(word.split("=", 1)[0] == name for word in words) == 1,
                "Missing/duplicated/conflicting baseline argument: " + token)
    require(not any(word.split("=", 1)[0] in PERIPHERAL_ARGS for word in words),
            "Do not enable or add peripheral arguments to the display experiment")
    return prior


def source_checks(root, work, current):
    """Verify the explicit source relationship, not the missing historical reports."""
    root, work = Path(root), Path(work)
    prior_path = root / "artifacts/hakios/lithium-ios16/phase4-r18/contract.json"
    require(sha(prior_path) == R18_CONTRACT_SHA256, "Frozen R18 contract changed")
    prior = read(prior_path)
    old_inputs = prior["inputs"]
    delta = validate_delta(old_inputs["port_files"], current["port_files"])
    require({k: v for k, v in old_inputs.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"}, "Non-port baseline inputs changed")
    r15 = read(root / "artifacts/hakios/lithium-ios16/phase4-r15/contract.json")
    args = command_line(prior["cmdline"], r15["cmdline"])
    port = root / "ports/hakios-lithium"
    display = port / "diagnostics/r18.json"
    require(sha(display) == prior["evidence_sha256"]["ports/hakios-lithium/diagnostics/r18.json"],
            "R18 display source contract changed")
    for name, digest in read(display)["after_sha256"].items():
        require(sha(work / name) == digest, "R18 display implementation changed: " + name)
    admission = read(port / "peripherals/contract.json")
    require(admission["default_off"] is True and admission["mmio_access"] is False and
            admission["touch_driver_implemented"] is False and admission["gpu_driver_implemented"] is False,
            "Admission must remain default-off and without hardware backends")
    for name, digest in admission["after_sha256"].items():
        require(sha(work / name) == digest, "Unexpected peripheral integration: " + name)
    require(sha(port / "peripherals/kernel.inc") == admission["header_sha256"], "Admission header changed")
    require(sha(port / "peripherals/boot-options.h") == admission["parser_sha256"] ==
            sha(root / "maintenance/hakios-lithium/peripherals/boot-options.h"), "Admission parser changed")
    tests_path = root / "reports/overnight-20261118/accepted-host-tests/passed.json"
    tests = read(tests_path)
    for name, digest in tests["source_sha256"].items():
        require(sha(root / name) == digest, "Previously tested peripheral source changed: " + name)
    return dict(passed=True, display_sources_unchanged=True, exact_post_r18_port_delta=delta,
                cmdline=args, peripheral_arguments_absent=True, admission_default_off=True,
                admission_test_manifest_sha256=sha(tests_path), tested_source_hashes=len(tests["source_sha256"]),
                hardware_verified=False, display_acceptance_verified=False, device_operations=False)


def snapshot_evidence(root, output, evidence):
    """Keep exact text evidence bytes in the new revision, retaining live guards."""
    root, output = Path(root).resolve(), Path(output).resolve()
    require(output != root and output.is_relative_to(root),
            "Evidence output must be a separate directory inside the repository")
    saved = {}
    for name, digest in sorted(evidence.items()):
        relative = Path(name)
        require(not relative.is_absolute() and ".." not in relative.parts and relative.parts and
                relative.parts[0] in {"scripts", "reports", "ports", "config"}, "Unsafe evidence path")
        original, target = (root / relative).resolve(), (output / "evidence" / relative).resolve()
        require(original.is_relative_to(root) and target.is_relative_to(output / "evidence"),
                "Evidence path escapes its source/snapshot directory")
        require(original != target, "Evidence source must not alias its snapshot")
        data = original.read_bytes()
        require(bytes([0]) not in data and hashlib.sha256(data).hexdigest() == digest,
                "Evidence changed or is not text: " + name)
        try:
            data.decode("utf-8")
        except UnicodeDecodeError as error:
            raise RuntimeError("Evidence must be UTF-8 text: " + name) from error
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            require(target.read_bytes() == data, "Preserve existing evidence snapshot: " + name)
        else:
            with target.open("xb") as stream:
                stream.write(data)
        require(sha(original) == sha(target) == digest, "Evidence copy/readback changed: " + name)
        saved[name] = dict(path=str(target.relative_to(root)), sha256=digest)
    return saved
