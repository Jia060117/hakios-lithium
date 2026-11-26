#!/usr/bin/env python3
"""Analyze the saved APID capture and return evidence; never contact a device."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT/"evidence/gpu-metal-20261123/package/runs/gpu-g1-access-01"
OUT = ROOT/"reports/hakios-ios16-gpu-g1-access-01"

def read(p): return json.loads(p.read_text(encoding="utf-8-sig"))
def fields(s): return dict(re.findall(r"(\w+)=([^\s]+)", s))
def main():
    state = read(RUN/"result.json")
    receiver = read(RUN/"usb-status.json")
    if receiver["status"] in ("capturing","waiting"):
        raise RuntimeError("capture remains active; do not finalize")
    spec=importlib.util.spec_from_file_location("access_usb_audit",ROOT/"scripts/hakios_usb_audit.py")
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    raw=(RUN/"console.frames").read_bytes();console=(RUN/"console.log").read_bytes()
    audit=mod.audit(raw,console,RUN/"capture-host.py")
    text=console.decode(errors="replace"); lines=text.splitlines()
    captures=[]; end=[]; stop=[]; temps=[]; drops=[]
    for n,line in enumerate(lines,1):
        if line.startswith("LITHIUM GPU access pass="):
            captures.append(dict(line=n,**fields(line)))
        if line.startswith("LITHIUM GPU access end "): end.append(fields(line))
        if line.startswith("LITHIUM CPU TEMP "):
            t=fields(line);temps.append(t)
            if t.get("phase")=="stop":stop.append(t)
        if "writer_contention_dropped_bytes=" in line: drops.append(fields(line))
    expected=[0x400f000,0x400f8d0,0x400f9fc,0x400a7d0,0x400a8fc,
              0x45a0004,0x45a0008,0x8c4034,0x8c4024,0x8c403c,0x8c4044,0x8c4028,0x8c40b0]
    pairs={(int(c["pass"]),int(c["field"])):c for c in captures}
    capture_ok=len(captures)==len(pairs)==26 and len(end)==1
    for f,pa in enumerate(expected):
        a=pairs.get((0,f),{});b=pairs.get((1,f),{})
        capture_ok=bool(capture_ok and a and b and a["valid"]==b["valid"]=="1" and
            int(a["physical"],0)==int(b["physical"],0)==pa and a["raw"]==b["raw"])
    summary=end[0] if end else {}
    capture_ok=bool(capture_ok and summary.get("error")=="0" and summary.get("reads")=="26"
        and summary.get("valid0")==summary.get("valid1")==summary.get("stable")=="0x1fff")
    return_info=read(RUN/"return-verification.json")
    return_ok=return_info.get("passed") is True
    no_source_drop=bool(drops and all(d.get("writer_contention_dropped_bytes")=="0" for d in drops))
    passed=bool(capture_ok and return_ok and audit["emitted_kernel_stream_through_timer_verified"]
                and no_source_drop and len(stop)==4)
    result=dict(schema="lithium-gpu-access-runtime-v1",analyzed_at=datetime.now(timezone.utc).isoformat(),
        passed=passed,capture_passed=capture_ok,automatic_fastboot_observed=return_ok,
        accepted_at=state["accepted_at"],image_sha256=state["image_sha256"],
        boot_args=state["boot_args"],summary=summary,captures=captures,
        cpu_peak_native_mC={a:max(int(t["native_mC"]) for t in temps if t.get("affinity")==a and t.get("valid")=="1")
            for a in sorted({t["affinity"] for t in temps})},
        cpu_temperature_is_not_gpu_temperature=True,source_log_drops_zero=no_source_drop,
        stop_temperature_records=stop,usb=audit,receiver_status=receiver,return_verification=return_info,
        gpu_power_on_passed=False,chip_id_read=False,pmic_commands=0,gpu_power_writes=0,
        firmware_loaded=False,metal_acceleration_verified=False)
    OUT.with_suffix(".json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (RUN/"usb-audit.json").write_text(json.dumps(audit,indent=2)+"\n")
    rows="\n".join(f"| {f} | `0x{pa:08x}` | `{pairs[(0,f)]['raw']}` | `{pairs[(1,f)]['raw']}` |"
                   for f,pa in enumerate(expected))
    md=f"""# GPU G1 APID 写归属采集 01 · 2026-11-23

实机采集{'通过' if passed else '未完整通过'}；GPU 尚未上电。CPU 保持 614.4 MHz 配置模型。
单次临时 RAM boot 接受时间 `{state['accepted_at']}`，镜像 SHA256 `{state['image_sha256']}`。
本轮 0 PMIC command、0 GPU/PMIC 电源写入，不访问 GPU aperture 或 SMMU。

## 本轮读数

13 字段双遍 26 读，valid/stable `0x1fff`。GFX APID52 写归属 EE0，MX APID127 归属 EE4。
GFX channel CONFIG0/STATUS1（DONE）支持继续准备 GFX 使能；不证明 RPM bus 已可用或电源已 settled。

| field | 物理地址 | pass0 | pass1 |
|---|---|---|---|
{rows}

## 退出和传输

正常180秒 timer，4路 stop 温度，source contention drops0：{no_source_drop}。
USB 独立逐字节重放：{audit['transport_verified']}；{audit['frames']}帧，
{audit['raw_bytes']}raw / {audit['console_bytes']}console bytes，drain：{audit['drain_success']}。
接收器终态原样保留 `{receiver['status']}`；Fastboot 唯一返回核验：{return_ok}。
未单独采集 UEFI 内部 Prepare/Commit，也不将 USB 断开单独当作返回成功。

## 实现与复核

主代理集成 `g1-access-entry.inc`、默认关闭 `gpu_access_diag`、构建打包和真机生命周期；
此前供电子代理 Ramanujan 实现 `g1-power-runtime.h` 与 adapter。
主代理独立核对全部26条原始读数、双遍一致性和 USB 重放。该采集不重复 fuse/S2事务。

下一步使用本轮 APID 结果补齐 STATUS 来源绑定、受控 GFX 使能/退场及 MX/RPMbus。
chipID、PAS13、SMMU、CP/fence、Metal 合成均未验证。

```text
{state['boot_args']}
```
"""
    OUT.with_suffix(".md").write_text(md,encoding="utf-8")
    print(json.dumps({k:result[k] for k in ["passed","capture_passed","automatic_fastboot_observed","summary"]}))

if __name__=="__main__":main()
