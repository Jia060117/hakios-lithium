#!/usr/bin/env python3
"""Analyze saved G1 precheck02 bytes only. No device operations."""
from datetime import datetime,timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
RUN=ROOT/"evidence/gpu-g1-20261123/package/runs/gpu-g1-precheck-02"
OUT=ROOT/"evidence/gpu-g1-20261123/main/runtime-precheck02"
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module
def read(path): return json.loads(path.read_text(encoding="utf-8"))
def write(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
def sha(data): return hashlib.sha256(data).hexdigest()
def fields(line): return dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)",line))
def segment(line,prefix):
    at=line.find(prefix)
    if at<0: return None
    text=line[at:]
    return text.split("LITHIUM ",2)[0] if not text.startswith("LITHIUM ") else (
        text[:text.find("LITHIUM ",8)] if text.find("LITHIUM ",8)>=0 else text)

def main():
    state=read(RUN/"result.json"); receiver=read(RUN/"usb-status.json")
    if receiver["status"] in ("capturing","waiting"):
        raise RuntimeError("receiver still active; final audit deferred")
    raw=(RUN/"console.frames").read_bytes(); console=(RUN/"console.log").read_bytes()
    text=console.decode("utf-8",errors="replace")
    usb=load("g1_usb_audit",ROOT/"scripts/hakios_usb_audit.py").audit(raw,console,RUN/"capture-host.py")
    write(OUT/"usb-audit.json",usb)
    row_ids=[39,41,63,64,66,68]
    field_names=["TYPE","SUBTYPE","RANGE","VSET","MODE","ENABLE","STEP"]
    offsets=[4,5,0x40,0x41,0x45,0x46,0x61]
    rails={"GFX_PMI8994_S2":3,"MX_PM8994_S2":1}
    fuse_rows=[]; observations=[]; routes=[]; begins=[]; ends=[]; temps=[]
    map_trace=[]; cpu_context=[]; g0_end=[]; interleaved=[]
    for n,line in enumerate(text.splitlines(),1):
        if line.count("LITHIUM ")>1: interleaved.append(n)
        for prefix,destination in (("LITHIUM GPU fuse pass=",fuse_rows),
            ("LITHIUM GPU supply pass=",observations),("LITHIUM GPU supply route",routes),
            ("LITHIUM GPU supply begin",begins),("LITHIUM GPU supply end",ends),
            ("LITHIUM CPU TEMP ",temps),("LITHIUM GPU stage=0 end",g0_end),
            ("LITHIUM CPU sanity logical=",cpu_context)):
            s=segment(line,prefix)
            if s: destination.append(dict(line=n,text=s,fields=fields(s)))
        for prefix in ("LITHIUM GPU supply map ","LITHIUM GPU supply unmap "):
            s=segment(line,prefix)
            if s: map_trace.append(dict(line=n,action="unmap" if " unmap " in s else "map",fields=fields(s)))
    fuse_pairs={}; rail_pairs={}
    for r in fuse_rows:
        f=r["fields"]; key=(int(f["pass"]),int(f["row"]))
        if key in fuse_pairs: raise ValueError("duplicate fuse record")
        fuse_pairs[key]=r
    for r in observations:
        f=r["fields"]; key=(f["rail"],int(f["pass"]),f["reg"])
        if key in rail_pairs: raise ValueError("duplicate supply record")
        rail_pairs[key]=r
    fuses_passed=len(fuse_pairs)==12
    for row in row_ids:
        a=fuse_pairs.get((0,row),{}).get("fields",{}); b=fuse_pairs.get((1,row),{}).get("fields",{})
        fuses_passed=bool(fuses_passed and a and b and int(a["pa"],0)==int(b["pa"],0)==0x74000+row*8
            and a["bits"]==b["bits"]=="64" and a["valid"]==b["valid"]=="1" and
            a["stable"]==b["stable"]=="1" and a["raw"]==b["raw"])
    decoder=load("g1_setpoint_decode",ROOT/"evidence/gpu-g1-20261123/spmi/decode_digital_setpoint.py")
    rail_passed=len(rail_pairs)==28; captures=[]; decoded=[]
    for name,sid in rails.items():
        capture=dict(rail=name,sid=sid,base=0x1700,fields={},valid_fields=[],stable_fields=[])
        for field,offset in zip(field_names,offsets):
            a=rail_pairs.get((name,0,field),{}).get("fields",{}); b=rail_pairs.get((name,1,field),{}).get("fields",{})
            good=bool(a and b and a["valid"]==b["valid"]=="1" and a["error"]==b["error"]=="0" and
                a["completed"]==b["completed"]==a["attempted"]==b["attempted"]=="1" and
                int(a["addr"],0)==int(b["addr"],0)==0x1700+offset and int(a["sid"])==int(b["sid"])==sid
                and int(a["status"],0)==int(b["status"],0)==1 and a["raw"]==b["raw"])
            rail_passed=rail_passed and good
            if good:
                capture["fields"][field]=int(a["raw"],0)
                capture["valid_fields"].append(field); capture["stable_fields"].append(field)
        captures.append(capture); decoded.append(dict(rail=name,**decoder.decode(capture)))
    active={}; cleanup=True; map_count=unmap_count=0
    for m in map_trace:
        f=m["fields"]; allocation=f["allocation"]
        if m["action"]=="map":
            map_count+=1
            if f["valid"]!="1" or allocation in active or int(f["bytes"])!=16384: cleanup=False
            active[allocation]=f["bytes"]
        else:
            unmap_count+=1
            if active.pop(allocation,None)!=f["bytes"]: cleanup=False
    cleanup=bool(cleanup and not active and map_count==unmap_count==4)
    summary=ends[0]["fields"] if len(ends)==1 else {}
    complete=bool(fuses_passed and rail_passed and cleanup and len(begins)==1 and
        begins[0]["fields"].get("logical")=="0" and begins[0]["fields"].get("affinity")=="0x1" and
        summary.get("fuse_stable")=="0x3f" and summary.get("gfx_stable")==summary.get("mx_stable")=="0x7f"
        and summary.get("gfx_error")==summary.get("mx_error")==summary.get("poisoned")=="0"
        and summary.get("observer_cmd_writes")=="28" and summary.get("regulator_writes")==summary.get("gpu_writes")=="0")
    stop=[x for x in temps if x["fields"].get("phase")=="stop"]
    peaks={}
    for r in temps:
        f=r["fields"]
        if f.get("valid")=="1" and "native_mC" in f:
            a=f["affinity"]; value=int(f["native_mC"]); peaks[a]=max(peaks.get(a,value),value)
    drops=[int(x) for x in re.findall(r"writer_contention_dropped_bytes=(\d+)",text)]
    return_record=read(RUN/"return-verification.json")
    no_panic=not re.search(r"panic\(cpu |Debugger message:\s*panic",text,re.I)
    passed=bool(complete and state["command_accepted"] and usb["emitted_kernel_stream_through_timer_verified"] and
        drops and drops[-1]==0 and return_record["passed"] and no_panic and
        {x["fields"].get("affinity") for x in stop if x["fields"].get("valid")=="1"}=={"0x0","0x1","0x100","0x101"})
    # Independent stock corner2 arithmetic from actual row63, without filling
    # any missing data. This narrow calculation does not authorize power-on.
    raw63=int(fuse_pairs[(0,63)]["fields"]["raw"],0) if fuses_passed else None
    init0=((raw63>>55)&31) if raw63 is not None else None
    model=None
    if init0 is not None:
        steps=-(init0&15) if init0&16 else init0&15
        untrimmed=670000+10000*steps
        unaged=max(520000,min(670000,((untrimmed-85000+4999)//5000)*5000))
        floor=max(520000,((unaged-70000+4999)//5000)*5000)
        model=dict(init0=init0,signed_steps=steps,source_fuse_uv=untrimmed,
            stock_trim_uv=-85000,unaged_open_loop_uv=unaged,unaged_floor_uv=floor,
            maximum_startup_aging_uv=15000,stock_startup_open_loop_uv=min(unaged+15000,670000),
            stock_startup_floor_uv=min(floor+15000,min(unaged+15000,670000)),
            scope="Independent corner2 arithmetic; formal pinned-policy result checked separately",gpu_write_permission=False)
    result=dict(schema="hakios-gpu-g1-precheck-saved-runtime-v1",analyzed_at=datetime.now(timezone.utc).isoformat(),
        attempt="gpu-g1-precheck-02",passed=passed,status="precheck_capture_passed" if passed else "precheck_incomplete",
        accepted_at=state["accepted_at"],image_sha256=state["image_sha256"],boot_args=state["boot_args"],
        fuse_capture_passed=fuses_passed,supply_capture_passed=rail_passed,
        cleanup_observed=cleanup,summary=summary,fuse_rows=fuse_rows,supply_rows=observations,routes=routes,
        rail_captures=captures,digital_supply_decoded=decoded,independent_stock133_arithmetic=model,
        usb=usb,receiver_terminal=receiver,return_verification=return_record,
        source_dropped_bytes=drops[-1] if drops else None,cpu_temperature_peak_native_mC=peaks,
        stop_temperature_rows=stop,map_trace=map_trace,g0_summary=g0_end,
        interleaved_printf_line_numbers=interleaved,printf_lines_are_all_atomic=False,
        no_panic_text_in_collected_stream=no_panic,physical_screen_observed=False,
        measured_gpu_chip_id=None,gpu_stage1_passed=False,gpu_acceleration_verified=False,
        regulator_writes=0,gpu_clock_writes=0,spmi_read_commands_observed=28,
        rail_settled_or_analog_voltage_verified=False,pas13_called=False,
        next="GFX enable + regulator-settled/RPM/bus/clock prerequisites before GPU domain power-on",
        input_sha256={p:sha((RUN/p).read_bytes()) for p in (
            "console.log","console.frames","capture-host.py","result.json","usb-status.json","return-verification.json")})
    write(OUT/"analysis.json",result)
    print(json.dumps({k:result[k] for k in ("passed","status","fuse_capture_passed","supply_capture_passed",
        "cleanup_observed","independent_stock133_arithmetic","digital_supply_decoded")},indent=2))
if __name__=="__main__": main()
