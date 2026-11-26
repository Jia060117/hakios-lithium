#!/usr/bin/env python3
"""Freeze one stock fixed-rate RAM screening profile; never contact a phone."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"artifacts/hakios/lithium-ios16"


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    value=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hz",required=True,type=int,
                        choices=(307200000,384000000,460800000,537600000,614400000,
                                 691200000,748800000,825600000,902400000))
    parser.add_argument("--attempt",required=True)
    parser.add_argument("--parallel",action="store_true")
    parser.add_argument("--perf-only",action="store_true")
    duration=parser.add_mutually_exclusive_group()
    duration.add_argument("--long",action="store_true",help="explicit 600s load / 720s isolated RAM session")
    duration.add_argument("--extended",action="store_true",help="explicit 1200s load / 1320s isolated RAM session")
    parser.add_argument("--log-retry",action="store_true")
    parser.add_argument("--kernel-revision",choices=("rate01","rate03","rate04","rate05","rate06","rate07","rate08","rate09","rate10"),default="rate01")
    parser.add_argument("--support-revision",choices=("01","02","03","04"),default="01")
    args=parser.parse_args()
    if len(args.attempt)!=2 or not args.attempt.isdigit():
        raise ValueError("attempt must be a new two-digit identifier")
    if args.perf_only and not args.parallel:
        raise ValueError("perf-only requires parallel workers")
    if args.long and (not args.perf_only or args.kernel_revision!="rate09"):
        raise ValueError("long profile requires explicit perf-only and long-capable rate09 kernel")
    if args.extended and (not args.perf_only or args.kernel_revision!="rate10" or
                          args.support_revision!="04"):
        raise ValueError("extended profile requires perf-only, rate10 kernel and support04")
    load_seconds, timer_seconds = (1200,1320) if args.extended else (600,720) if args.long else (90,180)
    pkg=module("r39_ram_packaging","scripts/package-hakios-cpu-r37-ram.py")
    pkg.BUILD=ROOT/("evidence/cpu-r39-20261121/main/kernel-result-r39-"+
                    args.kernel_revision+".json")
    pkg.SUPPORT=ROOT/("evidence/cpu-r39-20261121/support"+args.support_revision+
                     "/ram-support.json")
    reference=BASE/"phase4-r37-ram-frequency-16"
    old=json.loads((reference/"contract.json").read_text(encoding="utf-8"))
    pkg.require(old["image"]["sha256"]==
                "cb0c0a22e55dbdfda2f2850757d9de2d6fedd9f623928ecde5d60af13dcbd22e" and
                pkg.sha((reference/old["image"]["path"]).read_bytes())==old["image"]["sha256"],
                "frozen cleanup reference differs")
    words=old["cmdline"].split()
    words.remove("cpu_freq_diag=1")
    pkg.BASE_ARGS=" ".join(words+[
        f"cpu_perf_hz={args.hz}","cpu_inherited_supply_r39=1","cpu_rate_soak=1"])
    if args.parallel: pkg.BASE_ARGS+=" cpu_rate_soak_parallel=1"
    if args.perf_only: pkg.BASE_ARGS+=" cpu_rate_soak_perf_only=1"
    if args.long or args.extended:
        pkg.BASE_ARGS=pkg.BASE_ARGS.replace("loader_return_after=180",f"loader_return_after={timer_seconds}")
        pkg.BASE_ARGS+=f" cpu_rate_soak_long={2 if args.extended else 1}"
    if args.log_retry: pkg.BASE_ARGS+=" lithium_log_retry=1"
    build=json.loads(pkg.BUILD.read_text(encoding="utf-8"))
    pkg.require(build["linked_return_calls"][0][1]=="_lithium_cpu_thermal_stop",
                "linked cleanup call missing")
    reference_fat=None
    first_attempt={"01":"17","02":"26","03":"27","04":"28"}[args.support_revision]
    if args.attempt!=first_attempt:
        first=BASE/("phase4-r37-ram-frequency-"+first_attempt)
        first_contract=json.loads((first/"contract.json").read_text(encoding="utf-8"))
        reference_fat=first/"loader.fat"
        pkg.require(
            first_contract["components"]["loader.efi"]["sha256"]==
            json.loads(pkg.SUPPORT.read_text(encoding="utf-8"))["loader_sha256"] and
            pkg.sha(reference_fat.read_bytes())==
            first_contract["components"]["loader.fat"]["sha256"],
            "comparison loader/FAT differs")
    pkg.main("frequency",args.attempt,False,reference_fat=reference_fat)
    out=BASE/f"phase4-r37-ram-frequency-{args.attempt}"
    path=out/"contract.json"
    result=json.loads(path.read_text(encoding="utf-8"))
    result.update(
        implementation_revision="R39-stock-rate-screen01",
        perf_fixed_requested=True,perf_fixed_target_hz=args.hz,
        stock_corner={307200000:1,384000000:2,460800000:3,537600000:4,614400000:5,
                      691200000:6,748800000:7,825600000:8,902400000:9}[args.hz],
        inherited_S11_digital_uv=905000,
        CPU_policy_model_uv={307200000:585000,384000000:585000,460800000:585000,
                             537600000:585000,614400000:600000,691200000:610000,
                             748800000:620000,825600000:635000,902400000:645000}[args.hz],
        regulator_writes=0,TSENS_writes=0,max_clock_register_writes=11,
        inherited_voltage_dwell_check=True,dwell_min_us=302,
        thermal_observation=True,thermal_stop_linked_verified=True,
        stress_requested=True,stress_seconds=load_seconds,
        loader_return_after=timer_seconds,
        cpu_rate_soak_long=args.long or args.extended,stress_cutoff_native_mC=75000,
        stress_scope="two_perf_workers_pwr_housekeeping" if args.perf_only else
                     "three_concurrent_bound_workers" if args.parallel else
                     "rotating_per_core_initial_screen",
        stress_active_mask=6 if args.perf_only else 7,
        kernel_revision=args.kernel_revision,source_log_retry=args.log_retry,
        support_revision=args.support_revision,
        cpu_rate_soak_parallel=args.parallel,
        frequency_hardware_verified=False,stress_hardware_verified=False,
        measured_cpu_mhz=None)
    path.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")


if __name__=="__main__":
    main()
