#!/usr/bin/env python3
"""Add the strict R37 SMP return observation; host-only, reversible patch."""
from pathlib import Path
import difflib
import json
import hashlib
import shutil

ROOT = Path(__file__).resolve().parents[1]
WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
EVIDENCE = ROOT / "evidence/cpu-r37-20261120/return-fix"
PORT = ROOT / "ports/hakios-lithium"

STRICT = """
/* Only an exact firmware OFF result proves a started secondary has stopped.
 * The older stop routine's negative-state shortcut is not such proof. */
bool
psci_smp_stopped_strict(void)
{
    int max_cpu = ml_get_max_cpu_number();
    uint32_t started = psci_smp_started;
    if (cpu_number() != master_cpu || max_cpu < 0 || max_cpu >= 32)
        return false;
    if (max_cpu < 31 && (started >> (max_cpu + 1)) != 0)
        return false;
    for (int cpu = 0; cpu <= max_cpu; cpu++) {
        if (cpu == master_cpu || !(started & (1u << cpu))) continue;
        cpu_data_t *cdp = cpu_datap(cpu);
        if (cdp == NULL ||
            psci_call(PSCI_AFFINITY_INFO, cdp->cpu_phys_id, 0, 0) != PSCI_AFFINITY_OFF)
            return false;
    }
    return true;
}

"""


def main():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    sources = {
        "osfmk/arm64/psci_smp.h": ("extern void             psci_smp_stop_here(void);",
            "extern bool             psci_smp_stopped_strict(void);\nextern void             psci_smp_stop_here(void);"),
        "osfmk/arm64/psci_smp.c": ("/*\n * Where a processor learns that the system is ending:",
            STRICT + "/*\n * Where a processor learns that the system is ending:"),
        "osfmk/arm64/loader_return.c": ("\tpsci_smp_stop_others();\n#endif",
            "\tpsci_smp_stop_others();\n"
            "#if defined(LITHIUM_ARMV8_0)\n"
            "\tif (!lithium_fastboot_handoff_allowed()) {\n"
            '\t\tloader_log_puts("\\nLITHIUM FASTBOOT status=blocked reason=secondary_not_off\\n");\n'
            "\t\tfor (;;) __builtin_arm_wfi();\n\t}\n#endif\n#endif"),
    }
    patch = []
    hashes = {}
    for name, (before, after) in sources.items():
        template = WORK / "xnu/files" / name
        actual = WORK / "xnu-src/xnu-8796.121.2" / name
        old = template.read_text()
        assert old.count(before) == 1 and after not in old, name
        assert actual.read_text() == old, f"actual source differs: {name}"
        backup = EVIDENCE / ("before-" + Path(name).name)
        assert not backup.exists(), f"backup exists: {backup}"
        backup.write_bytes(template.read_bytes())
        new = old.replace(before, after)
        patch.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
            fromfile="a/xnu/files/" + name, tofile="b/xnu/files/" + name))
        template.write_text(new)
        actual.write_text(new)
        hashes[name] = hashlib.sha256(new.encode()).hexdigest()
    (PORT / "patches/0027-lithium-strict-smp-return.patch").write_text("".join(patch))
    for name in ("kernel.inc",):
        shutil.copyfile(PORT / "return-fastboot" / name,
                        WORK / "devices/lithium/return-fastboot" / name)
    # Reproducible prepared tree and its input map. This is not a device proof.
    for name in ("0023-lithium-return-fastboot.patch", "0027-lithium-strict-smp-return.patch"):
        shutil.copyfile(PORT / "patches" / name, WORK / "devices/lithium/patches" / name)
    (EVIDENCE / "prepared.json").write_text(json.dumps(
        {"passed": True, "source_sha256": hashes, "device_operations": 0}, indent=2) + "\n")
    print(json.dumps(hashes, indent=2))


if __name__ == "__main__":
    main()
