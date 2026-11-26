#!/usr/bin/env python3
"""Package R38 RAM observations or the default-off early perf experiment."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--power", action="store_true")
    parser.add_argument("--power-fix", action="store_true")
    parser.add_argument("--spmi", action="store_true")
    parser.add_argument("--spmi-done", action="store_true")
    parser.add_argument("--perf-uefi", action="store_true")
    parser.add_argument("--perf-readback", action="store_true")
    parser.add_argument("--perf-active", action="store_true")
    parser.add_argument("--perf-final", action="store_true")
    parser.add_argument("--perf-clkctl", action="store_true")
    options = parser.parse_args()
    if sum((options.power, options.power_fix, options.spmi, options.spmi_done,
            options.perf_uefi, options.perf_readback, options.perf_active,
            options.perf_final, options.perf_clkctl)) > 1:
        parser.error("select one power revision")
    spec = importlib.util.spec_from_file_location(
        "r38_existing_ram_packager", ROOT / "scripts/package-hakios-cpu-r37-ram.py")
    packager = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packager)
    perf = (options.perf_uefi or options.perf_readback or options.perf_active or
            options.perf_final or options.perf_clkctl)
    spmi = options.spmi or options.spmi_done or perf
    power = options.power or options.power_fix or spmi
    revision = "clock-power05" if options.spmi_done or perf else ("clock-power04" if options.spmi else ("clock-power03" if options.power_fix else ("clock-power01" if power else "clock-read01")))
    attempt = "14" if options.perf_clkctl else ("13" if options.perf_final else ("12" if options.perf_active else ("11" if options.perf_readback else ("10" if options.perf_uefi else ("09" if options.spmi_done else ("08" if options.spmi else ("07" if options.power_fix else ("05" if power else "04"))))))))
    packager.BUILD = ROOT / ("evidence/cpu-r38-20261120/main/kernel-result-" + revision + ".json")
    # Keep the proven profile, loader and PID1. One new diagnostic variable;
    # frequency remains selected so the existing runner admits the package.
    packager.BASE_ARGS += " cpu_clock_diag=1"
    if power:
        # This run observes supplies and executes the existing CPU benchmark.
        # Do not retry inherited PMU borrowing after the readback failure.
        packager.BASE_ARGS += " cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1"
    if spmi:
        packager.BASE_ARGS += " cpu_spmi_diag=1"
    if perf:
        packager.SUPPORT = ROOT / ("evidence/cpu-r38-20261121/perf-uefi-support05/ram-support.json"
                                   if options.perf_clkctl else
                                   "evidence/cpu-r38-20261121/perf-uefi-support04/ram-support.json"
                                   if options.perf_final else
                                   "evidence/cpu-r38-20261121/perf-uefi-support03/ram-support.json"
                                   if options.perf_active else
                                   "evidence/cpu-r38-20261121/perf-uefi-support02/ram-support.json"
                                   if options.perf_readback else
                                   "evidence/cpu-r38-20261120/perf-uefi-support01/ram-support.json")
        packager.BASE_ARGS += " cpu_perf_fixed=1"
    packager.main("frequency", attempt, not power)
    directory = ROOT / ("artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-" + attempt)
    manifest_path = directory / "contract.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest.update(implementation_revision="R38-perf-fixed05" if options.perf_clkctl else ("R38-perf-fixed04" if options.perf_final else ("R38-perf-fixed03" if options.perf_active else ("R38-perf-fixed02" if options.perf_readback else ("R38-perf-fixed01" if perf else "R38-" + revision)))),
                    cpu_clock_diag=True,
                    clock_register_writes=None if perf else 0,
                    max_clock_register_writes=11 if perf else 0,
                    regulator_writes=0,
                    perf_fixed_requested=perf,
                    perf_fixed_target_hz=307200000 if perf else None,
                    lock_readback_receipt=options.perf_readback or options.perf_active or options.perf_final or options.perf_clkctl,
                    primary_fsm_active_status_masked=options.perf_active or options.perf_final or options.perf_clkctl,
                    final_snapshot_receipt=options.perf_final or options.perf_clkctl,
                    post_mux_clkctl_exact_observed_state=options.perf_clkctl,
                    clkctl_high_bit_names_verified=False,
                    clock_voltage_fix_applied=False,
                    cpu_power_diag=power,
                    mmio_unmap_page_geometry_fixed=options.power_fix or spmi,
                    cpu_spmi_diag=spmi,
                    spmi_completion_before_expiry=options.spmi_done or perf,
                    spmi_supply_writes=0,
                    spmi_read_command_writes=None if perf else (12 if spmi else 0),
                    kernel_spmi_snapshot_max_read_commands=12 if spmi else 0,
                    pmu_setup_disabled=power)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if perf:
        print("PASS R38 early-perf candidate; separate RAM loader/Mu, frozen kernel reused; hardware unverified")
    else:
        print("PASS R38 clock-read package; no firmware or PMU implementation changes")


if __name__ == "__main__":
    main()
