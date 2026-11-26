#!/usr/bin/env python3
"""Analyze saved CX-only request, ON status, exact local restore and USB."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import importlib.util
import json
import re

ROOT=Path(__file__).resolve().parents[1]
RUN=ROOT/"evidence/gpu-metal-20261123/package/runs/gpu-g1-cx-cycle-01"
OUT=ROOT/"reports/hakios-ios16-gpu-g1-cx-cycle-01"


def read(p):
    return json.loads(p.read_text(encoding="utf-8-sig"))


def fields(s):
    return dict(re.findall(r"(\w+)=([^\s]+)",s))


def parse(data):
    pat=re.compile(rb"LITHIUM CPU TEMP (?:(?!LITHIUM)[^\r\n])*freshness_verified=0\r?\n")
    spans=[dict(start=m.start(),end=m.end(),text=m.group().decode()) for m in pat.finditer(data)]
    text=pat.sub(b"",data).decode("utf-8",errors="replace")
    rows={k:[fields(m.group()) for m in re.finditer(re.escape("LITHIUM GPU cx cycle "+k+" ")+r"[^\r\n]*",text)] for k in ("begin","snapshot","on","write","end")}
    assert all(len(rows[k])==1 for k in ("begin","snapshot","on","end"))
    begin,end=rows["begin"][0],rows["end"][0]
    assert begin["logical"]=="0" and int(begin["affinity"],0)==1
    assert int(begin["control_pa"],0)==0x8c4034 and int(begin["status_pa"],0)==0x8c4038
    required="rc error cleanup_error request_attempted active_verified inherited restore_verified cleanup_complete final_control final_control_valid final_status final_status_valid restore_mask dirty uncertain ambiguous_stores loads load_cap stores store_cap maps unmaps elapsed_us global_us global_overrun adapter_error global_off_required gx pll pmic rpm_votes gpu_aperture smmu".split()
    assert all(k in end for k in required),end
    assert int(end["loads"])<=1024 and end["load_cap"]=="1024"
    assert len(rows["write"])==int(end["stores"])<=4 and end["store_cap"]=="4"
    assert [int(x["index"]) for x in rows["write"]]==list(range(len(rows["write"])))
    assert all(int(x["pa"],0)==0x8c4034 for x in rows["write"])
    assert end["global_off_required"]==end["pmic"]==end["rpm_votes"]=="0"
    assert end["gx"]==end["pll"]=="untouched" and end["gpu_aperture"]==end["smmu"]=="unread"
    assert int(end["restore_mask"],0)==0xfff001
    return rows,spans


def validate_active(rows, snapshot, on):
    assert on["pair_valid"]==on["on_observed"]=="1"
    assert int(on["status"],0)&0x80000000 and int(on["control"],0)&7==0
    assert int(on["control"],0)&0xfff000==0x282000
    original=int(snapshot["control"],0)&0x7fffffff
    timed=(original&~0xfff000)|0x282000
    expected=([timed] if original!=timed else [])+[timed&~1,timed]
    if original!=timed:
        expected.append(original)
    assert [int(x["raw"],0)&0x7fffffff for x in rows["write"]]==expected


def main():
    receiver=read(RUN/"usb-status.json")
    assert receiver["status"] not in ("waiting","capturing")
    result,returned=read(RUN/"result.json"),read(RUN/"return-verification.json")
    data=(RUN/"console.log").read_bytes()
    rows,spans=parse(data)
    end,snapshot,on=rows["end"][0],rows["snapshot"][0],rows["on"][0]
    active=end["active_verified"]=="1" and on["active_verified"]=="1"
    if active:
        validate_active(rows, snapshot, on)
    restored=end["restore_verified"]==end["final_control_valid"]==end["cleanup_complete"]=="1" and all(int(end[k],0)==0 for k in ("cleanup_error","dirty","uncertain","global_overrun"))
    if restored:
        assert (int(end["final_control"],0)^int(snapshot["control"],0))&0xfff001==0
    spec=importlib.util.spec_from_file_location("cx_saved_usb",ROOT/"scripts/hakios_usb_audit.py")
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    audit=mod.audit((RUN/"console.frames").read_bytes(),data,RUN/"capture-host.py")
    text=data.decode("utf-8",errors="replace")
    stop=[fields(m.group()) for m in re.finditer(r"LITHIUM CPU TEMP phase=stop [^\r\n]*",text)]
    lifecycle=bool(returned.get("passed") and len(stop)==4 and
        "LITHIUM LOG writer_contention_dropped_bytes=0" in text and audit["emitted_kernel_stream_through_timer_verified"])
    passed=bool(active and restored and lifecycle and all(int(end[k],0)==0 for k in ("rc","error","adapter_error")) and int(end["elapsed_us"])<20000)
    report=dict(schema="lithium-gpu-cx-cycle-runtime-v1",passed=passed,
        analyzed_at=datetime.now(timezone.utc).isoformat(),accepted_at=result["accepted_at"],
        image_sha256=result["image_sha256"],boot_args=result["boot_args"],
        cx_on_observed=active,cx_local_configuration_restored=restored,
        global_off_required=False,lifecycle_passed=lifecycle,rows=rows,
        original_console_modified=False,original_console_sha256=hashlib.sha256(data).hexdigest(),
        complete_temperature_spans_in_derived_view=spans,stop_temperatures=stop,
        usb=audit,receiver=receiver,return_verification=returned,
        gx_enabled=False,gpu_clock_initialized=False,chip_id=None,
        gpu_power_on_passed=False,metal_acceleration_verified=False)
    OUT.with_suffix(".json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (RUN/"usb-audit.json").write_text(json.dumps(audit,indent=2)+"\n",encoding="utf-8")
    observed = lambda valid, value: value if valid == "1" else "UNREAD/INVALID"
    OUT.with_suffix(".md").write_text(f"""# GPU G1 CX本地使能/恢复01 · {result['accepted_at'][:10]}

一次隔离RAM启动`{result['accepted_at']}`，镜像`{result['image_sha256']}`。
CX上电观察`{active}`，本地配置恢复`{restored}`，生命周期`{lifecycle}`，本阶段通过`{passed}`。

| 时刻 | CONTROL 0x8c4034 | STATUS 0x8c4038 |
| --- | --- | --- |
| 初始 | `{snapshot['control']}` | `{snapshot['status']}` |
| ON | `{observed(on['pair_valid'], on['control'])}` | `{observed(on['pair_valid'], on['status'])}` |
| 退出 | `{observed(end['final_control_valid'], end['final_control'])}` | `{observed(end['final_status_valid'], end['final_status'])}` |

只写CONTROL，STATUS只读；ON观察要求硬件STATUS31=1及本地collapse0。
本地原软件位恢复mask`0x00fff001`，不强制votable域的全局PWR_ON归零，
不覆盖其他请求者、不循环关断。实际每次写入轨迹保存在JSON中；只有ON与恢复均成立才认可完整2或4步。
{end['loads']}读/{end['stores']}写/{end['elapsed_us']}µs，{end['maps']}映射/{end['unmaps']}释放。
error/cleanup=`{end['error']}/{end['cleanup_error']}`，dirty/uncertain=`{end['dirty']}/{end['uncertain']}`。
没有GX/clamp/PLL/RCG/分支、PMIC或RPM资源票操作；未读GPU/SMMU。
本轮CX本地请求结果按实际字段判断，不声称完整GPU上电、芯片ID或GPU加速。

180秒正常timer/四stop/source0/drain及唯一同机Fastboot终点`{lifecycle}`。
USB独立逐字节重放{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console；
接收器`{receiver['status']}`/`{receiver.get('failure','')}`保留，返回核验`{returned.get('finished_at')}`。
原日志未改，派生视图只移除完整温度记录并保存原偏移。
主代理实际构建完整HakiOS、镜像读回、上机及USB审计；
Aristotle完成实际adapter专项和ARM64，main另有24个实际参数ASan/UBSan用例。
CPU仍614.4MHz配置模型、R20显示，RAM包无物理UFS。
下一是源支持的GFX/GX/133MHz链路；MX850mV数字ready仍不冒充已证明MX4。
""",encoding="utf-8")
    print(json.dumps(dict(passed=passed,cx_on=active,restored=restored,lifecycle_passed=lifecycle,frames=audit["frames"])))
    return 0 if passed else 1


if __name__=="__main__":
    raise SystemExit(main())
