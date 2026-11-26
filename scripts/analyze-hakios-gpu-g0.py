#!/usr/bin/env python3
"""Audit only the completed, saved G0 USB stream; never contact a device."""
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-g0-20261123/package/runs/gpu-g0-01"
OUT = ROOT / "evidence/gpu-g0-20261123/main/runtime-g0-01"
def read(path): return json.loads(path.read_text(encoding="utf-8"))
def write(path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
def sha(data): return hashlib.sha256(data).hexdigest()
def fields(line): return dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)",line))
def main():
    state = read(RUN/"result.json")
    receiver = read(RUN/"usb-status.json")
    if receiver["status"] == "capturing":
        raise RuntimeError("saved USB capture is still running; final audit deferred")
    raw = (RUN/"console.frames").read_bytes()
    console = (RUN/"console.log").read_bytes()
    text = console.decode("utf-8",errors="replace")
    spec=importlib.util.spec_from_file_location("g0_usb_audit",ROOT/"scripts/hakios_usb_audit.py")
    audit_module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit_module)
    usb=audit_module.audit(raw,console,RUN/"capture-host.py")
    write(OUT/"usb-audit.json",usb)
    expected = {
        "CX_GDSCR":0x008c4034,"GX_GDSCR":0x008c4024,
        "GPU_AHB_CBCR":0x008c403c,"GPU_AON_ISENSE_CBCR":0x008c4044,
        "GPU_GX_GFX3D_CBCR":0x008c4028,"GPU_GX_RBBMTIMER_CBCR":0x008c40b0,
        "GFX3D_CMD_RCGR":0x008c4000,"GCC_MMSS_BIMC_GFX_CBCR":0x00309010,
        "GCC_BIMC_GFX_CBCR":0x00346018}
    rows=[]; begin=[]; end=[]; maps=[]; unmaps=[]; temps=[]
    for number,line in enumerate(text.splitlines(),1):
        f=fields(line)
        if line.startswith("LITHIUM GPU stage=0 pass="):
            rows.append(dict(line=number,pass_index=int(f["pass"]),reg=f["reg"],
                pa=int(f["pa"],0),valid=int(f["valid"]),
                raw=int(f["raw"],0) if f["raw"]!="UNREAD" else None,
                stable=int(f["stable"])))
        elif line.startswith("LITHIUM GPU stage=0 begin"):
            begin.append(dict(line=number,fields=f))
        elif line.startswith("LITHIUM GPU stage=0 end"):
            end.append(dict(line=number,fields=f))
        elif line.startswith("LITHIUM GPU stage=0 map "):
            maps.append(dict(line=number,fields=f))
        elif line.startswith("LITHIUM GPU stage=0 unmap "):
            unmaps.append(dict(line=number,fields=f))
        elif line.startswith("LITHIUM CPU TEMP "):
            temps.append(dict(line=number,fields=f))
    pairs={}
    for row in rows:
        key=(row["pass_index"],row["reg"])
        if key in pairs: raise ValueError("duplicate G0 register observation")
        pairs[key]=row
    snapshot = len(rows)==18 and len(begin)==1 and len(end)==1
    for name,pa in expected.items():
        a=pairs.get((0,name)); b=pairs.get((1,name))
        snapshot = snapshot and bool(a and b and a["pa"]==b["pa"]==pa and
            a["valid"]==b["valid"]==a["stable"]==b["stable"]==1 and a["raw"]==b["raw"])
    summary = end[-1]["fields"] if end else {}
    snapshot = bool(snapshot and summary.get("status")=="snapshot_complete" and
        summary.get("errors")=="0x0" and summary.get("loads")=="18" and
        summary.get("gpu_writes")=="0" and summary.get("valid0")=="0x1ff" and
        summary.get("valid1")=="0x1ff" and summary.get("stable_mask")=="0x1ff")
    binding = bool(begin and begin[0]["fields"].get("logical")=="0" and
                   begin[0]["fields"].get("affinity")=="0x1")
    cleanup = len(maps)==len(unmaps)==3
    for m,u in zip(maps,unmaps):
        a=m["fields"]; b=u["fields"]
        cleanup = (cleanup and a["valid"]=="1" and a["allocation"]==b["allocation"] and
            a["bytes"]==b["bytes"]=="16384" and a["index"]==b["index"])
    decoded=[]
    for name,pa in expected.items():
        row=pairs.get((0,name))
        if not row or row["raw"] is None: continue
        value=row["raw"]
        d=dict(reg=name,pa=f"0x{pa:08x}",raw=f"0x{value:08x}")
        if "GDSCR" in name:
            d.update(pwr_on=(value>>31)&1,sw_override=(value>>2)&1,
                hw_control=(value>>1)&1,sw_collapse=value&1,
                meaning="GDSC reports collapsed, PWR_ON clear" if not value>>31 and value&1 else "see raw")
        elif "CBCR" in name:
            d.update(branch_enable=value&1,branch_off=(value>>31)&1,
                meaning="documented branch enable/off fields only; no actual MHz or ownership claim")
        decoded.append(d)
    drops=[int(x) for x in re.findall(r"writer_contention_dropped_bytes=(\d+)",text)]
    stop=[t for t in temps if t["fields"].get("phase")=="stop"]
    stop_valid={t["fields"].get("affinity") for t in stop if t["fields"].get("valid")=="1"}
    peaks={}
    for t in temps:
        f=t["fields"]
        if f.get("valid")=="1" and "native_mC" in f:
            affinity=f["affinity"]; value=int(f["native_mC"])
            peaks[affinity]=max(peaks.get(affinity,value),value)
    return_record=read(RUN/"return-verification.json")
    no_panic=not re.search(r"panic\(cpu |PANIC CAUSE|Debugger message:\s*panic",text,re.I)
    passed=bool(state["command_accepted"] and snapshot and binding and cleanup and
        usb["emitted_kernel_stream_through_timer_verified"] and drops and drops[-1]==0 and
        stop_valid=={"0x0","0x1","0x100","0x101"} and return_record.get("passed") is True and no_panic)
    result=dict(schema="hakios-gpu-g0-saved-runtime-v1",analyzed_at=datetime.now(timezone.utc).isoformat(),
        attempt="gpu-g0-01",passed=passed,status="G0_passed" if passed else "G0_observations_incomplete",
        accepted_at=state["accepted_at"],image_sha256=state["image_sha256"],
        boot_args=state["boot_args"],snapshot_passed=snapshot,cpu_binding_observed=binding,
        allocation_cleanup_observed=cleanup,begin=begin,end=end,registers=rows,decoded_registers=decoded,
        gpu_mmio_writes=0,gpu_aperture_read=False,chip_id=None,firmware_loaded=False,
        gpu_acceleration_verified=False,power_rail_voltage_verified=False,
        cpu_temperature_peak_native_mC=peaks,temperature_rows=len(temps),stop_temperature_rows=stop,
        source_contention_dropped_bytes=drops[-1] if drops else None,
        usb=usb,receiver_terminal=receiver,return_verification=return_record,
        no_panic_text_in_collected_stream=no_panic,physical_screen_observed=False,
        cpu_frequency_kind="614.4 MHz configuration model; no PMU measured MHz",
        device_operation_scope="5 preflight reads, one temporary boot, one post-return devices/product check; no retry",
        next="G1 source-backed GPU GFX/MX supply and bus prerequisites before power-on/chip-ID",
        input_sha256={name:sha((RUN/name).read_bytes()) for name in (
            "console.frames","console.log","result.json","usb-status.json","return-verification.json","capture-host.py")})
    write(OUT/"analysis.json",result)
    print(json.dumps({k:result[k] for k in ("passed","status","snapshot_passed","allocation_cleanup_observed",
        "cpu_temperature_peak_native_mC","source_contention_dropped_bytes")},indent=2))
if __name__=="__main__": main()
