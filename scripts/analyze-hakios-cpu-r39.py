#!/usr/bin/env python3
"""Analyze saved screening logs only. Does not open or command any device."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import statistics
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)",line))


def analyze(run):
    data=(run/"console.log").read_bytes() if (run/"console.log").exists() else b""
    lines=data.decode("utf-8",errors="replace").splitlines()
    state=json.loads((run/"result.json").read_text(encoding="utf-8"))
    tokens=state["boot_args"].split()
    parallel="cpu_rate_soak_parallel=1" in tokens
    perf_only="cpu_rate_soak_perf_only=1" in tokens
    extended="cpu_rate_soak_long=2" in tokens
    long=not extended and "cpu_rate_soak_long=1" in tokens
    load_seconds=1200 if extended else 600 if long else 90
    if extended:
        # Ambiguous arguments must not qualify or fall back to a shorter run.
        extended_args={"cpu_rate_soak_long":"2", "cpu_rate_soak":"1",
                       "cpu_rate_soak_parallel":"1", "cpu_rate_soak_perf_only":"1",
                       "loader_return_after":"1320"}
        extended_args_ok=all(
            [token for token in tokens if token.startswith(name+"=")]==[f"{name}={value}"]
            for name,value in extended_args.items())
    else:
        extended_args_ok=True
    prefix="LITHIUM CPU SOAK PARALLEL " if parallel else "LITHIUM CPU SOAK "
    spec=importlib.util.spec_from_file_location("saved_r39_usb",
                                             ROOT/"scripts/hakios_usb_audit.py")
    auditor=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(auditor)
    raw=(run/"console.frames").read_bytes() if (run/"console.frames").exists() else b""
    transport=auditor.audit(raw,data,run/"capture-host.py")
    samples=[]; operations=[]; cores=[]; benches=[]; stops=[]
    for number,line in enumerate(lines,1):
        if "LITHIUM CPU TEMP " in line:
            f=fields(line)
            if (f.get("valid")=="1" and
                f.get("native_mC","").lstrip("-").isdigit() and "raw" in f):
                raw=int(f["raw"],16); code=raw&4095
                if code>=2048: code-=4096
                if not raw&(1<<21) or code*100!=int(f["native_mC"]):
                    raise ValueError(f"Temperature raw mismatch at line {number}")
                samples.append(dict(line=number,phase=f.get("phase"),
                                    affinity=f.get("affinity"),native_mC=code*100))
        if line.startswith(prefix+"end "): operations.append(fields(line))
        if line.startswith(prefix+"core "): cores.append(fields(line))
        if line.startswith("LITHIUM CPU BENCH test="): benches.append(fields(line))
        if line.startswith("LITHIUM CPU TEMP phase=stop "): stops.append(number)
    if (run/"return-verification.json").exists():
        returned=json.loads((run/"return-verification.json").read_text(encoding="utf-8"))
    else:
        returned={}
    soak=operations[-1] if operations else {}
    soak_ok=(extended_args_ok and soak.get("result")=="1" and soak.get("reason")=="duration_complete" and
             all(soak.get(k)=="0" for k in ("errors","hotstop","thermal_errors")) and
             (soak.get("joined")=="1" and
              all(soak.get(k)=="0x7" for k in ("created_mask","ready_mask","done_mask"))
              if parallel else soak.get("original_binding_restored")=="1") and
             len(cores)==3 and {c.get("affinity") for c in cores}=={"0x1","0x100","0x101"} and
             all(c.get("valid")=="1" and
                 (c.get("active")=="0" and int(c["batches"])==0
                  if perf_only and c.get("affinity")=="0x1" else
                  int(c["batches"])>0 and
                  (c.get("active")=="1" if perf_only else True)) for c in cores) and
             (soak.get("active_mask")=="0x6" if perf_only else True) and
             int(soak.get("elapsed_ms","0"))>=load_seconds*1000)
    temperature_summary={}
    thermal_trends={}
    for affinity in ("0x0","0x1","0x100","0x101"):
        values=[s["native_mC"] for s in samples if s["affinity"]==affinity]
        if values: temperature_summary[affinity]=dict(
            min_mC=min(values),max_mC=max(values),samples=len(values))
        load_values=[s["native_mC"] for s in samples
                     if s["affinity"]==affinity and s["phase"]=="rate_soak_periodic"]
        if len(load_values)>=20:
            thermal_trends[affinity]=dict(
                periodic_samples=len(load_values),
                first10_median_mC=statistics.median(load_values[:10]),
                last10_median_mC=statistics.median(load_values[-10:]),
                last10_min_mC=min(load_values[-10:]),last10_max_mC=max(load_values[-10:]),
                calibration_verified=False,final_thermal_plateau_proven=False)
    panic=any(re.search(r"panic\(cpu|\bPANIC CAUSE\b",line) for line in lines)
    dropped=re.findall(rb"LITHIUM LOG writer_contention_dropped_bytes=(\d+)",data)
    source_drops=int(dropped[-1]) if dropped else None
    passed=bool(transport["emitted_kernel_stream_through_timer_verified"] and
                returned.get("passed") and soak_ok and len(stops)==4 and
                source_drops==0 and not panic)
    return dict(
        passed=passed,scope="perf_cluster_parallel_pwr_housekeeping_not_final_stability" if perf_only else
                            "three_core_parallel_screen_not_final_stability" if parallel else
                            "initial_rotating_per_core_screen_not_final_stability",
        attempt=state["attempt_id"],accepted_at=state["accepted_at"],
        image_sha256=state["image_sha256"],boot_args=state["boot_args"],
        normal_return_and_stream=transport["emitted_kernel_stream_through_timer_verified"],
        fastboot_endpoint_verified=returned.get("passed",False),
        correctness_soak_passed=bool(soak_ok),soak=soak,core_results=cores,
        temperature=temperature_summary,stop_temperature_rows=len(stops),
        soak_temperature_trend=thermal_trends,
        temperature_samples=len(samples),benchmarks=benches,panic_seen=panic,
        source_writer_contention_dropped_bytes=source_drops,
        transport=transport,device_operations=0,simultaneous_load_tested=parallel,
        stress_active_affinities=["0x100","0x101"] if perf_only else ["0x1","0x100","0x101"],
        pwr_housekeeping_only=perf_only,
        requested_load_seconds=load_seconds,ten_minute_load_completed=bool(long and soak_ok),
        twenty_minute_load_completed=bool(extended and soak_ok),
        extended_qualification_passed=bool(extended and passed),
        sustained_desktop_tested=False,measured_cpu_mhz=None)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt",required=True)
    args=parser.parse_args()
    if not re.fullmatch(r"r37-frequency-\d{2}",args.attempt):
        raise ValueError("Invalid phase-scoped run identifier")
    run=ROOT/"reports/local/r37-attempts"/args.attempt
    result=analyze(run)
    (run/"r39-analysis.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in result.items() if k not in (
        "transport","boot_args","core_results","benchmarks")},indent=2))
    return 0 if result["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
