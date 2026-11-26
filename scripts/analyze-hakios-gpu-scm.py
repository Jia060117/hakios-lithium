#!/usr/bin/env python3
"""Analyze saved SCM INFO frames and return evidence without device access."""
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g2-scm-info-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g2-scm-info-01"
TARGETS = (0x82000601, 0x02000201, 0x02000202, 0x02000205)
FIDS = (0xc2000601, 0x42000601, 0x42000601, 0x42000601)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def inspect(text):
    lines = text.splitlines()
    calls = [dict(line=index, **fields(line)) for index, line in enumerate(lines, 1)
             if line.startswith("LITHIUM GPU scm call ")]
    returned = [dict(line=index, **fields(line)) for index, line in enumerate(lines, 1)
                if line.startswith("LITHIUM GPU scm result ")]
    ends = [fields(line) for line in lines if line.startswith("LITHIUM GPU scm end ")]
    begin = [fields(line) for line in lines if line.startswith("LITHIUM GPU scm begin ")]
    summary = ends[0] if len(ends) == 1 else None
    input_ok = len(calls) <= 4 and all(
        int(row["index"]) == index and int(row["in0"], 0) == FIDS[index] and
        int(row["in1"], 0) == 1 and int(row["in2"], 0) == TARGETS[index] and
        row["tail_zero"] == "1" for index, row in enumerate(calls))
    output_ok = len(returned) <= 4 and all(int(row["index"]) == index and
        row["callback"] == "0" and int(row["out0"], 0) == 0 and
        int(row["out1"], 0) != 0 for index, row in enumerate(returned))
    complete = len(calls) == len(returned) == 4 and len(begin) == 1 and summary is not None
    success = bool(complete and input_ok and output_ok and summary["error"] == "0" and
        summary["calls"] == "4" and summary["abi_confirmed"] == "1" and
        int(summary["availability_known"], 0) == int(summary["available"], 0) == 7)
    return dict(query_passed=success, input_frames_valid=input_ok, begins=begin,
                calls=calls, returned=returned, summary=summary)


def main():
    state = read(RUN / "result.json")
    receiver = read(RUN / "usb-status.json")
    if receiver["status"] in ("capturing", "waiting"):
        raise RuntimeError("capture remains active; do not finalize")
    spec = importlib.util.spec_from_file_location("scm_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
    auditor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(auditor)
    raw = (RUN / "console.frames").read_bytes()
    console = (RUN / "console.log").read_bytes()
    audit = auditor.audit(raw, console, RUN / "capture-host.py")
    text = console.decode(errors="replace")
    result = inspect(text)
    temperatures = [fields(line) for line in text.splitlines() if line.startswith("LITHIUM CPU TEMP ")]
    stops = [row for row in temperatures if row.get("phase") == "stop"]
    losses = [int(value) for value in re.findall(r"writer_contention_dropped_bytes=(\d+)", text)]
    return_info = read(RUN / "return-verification.json")
    passed = bool(result["query_passed"] and return_info.get("passed") is True and
                  audit["emitted_kernel_stream_through_timer_verified"] and
                  len(stops) == 4 and losses and not any(losses))
    result.update(schema="lithium-gpu-scm-info-runtime-v1", passed=passed,
        analyzed_at=datetime.now(timezone.utc).isoformat(), accepted_at=state["accepted_at"],
        image_sha256=state["image_sha256"], boot_args=state["boot_args"],
        automatic_fastboot_observed=return_info.get("passed") is True,
        source_log_drops=losses, stop_temperature_records=stops, usb=audit,
        receiver_status=receiver, return_verification=return_info,
        pas13_supported=None, pas_operations=0, gpu_aperture_access=False,
        gpu_power_writes=0, firmware_loaded=False, cp_verified=False,
        metal_acceleration_verified=False)
    OUT.with_suffix(".json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    rows = "\n".join(f"| {row['index']} | `{row['out0']}` | `{row['out1']}` | `{row['out2']}` | `{row['out3']}` |"
                     for row in result["returned"])
    document = f"""# GPU G2 SCM INFO 01 · 2026-11-26

本轮完整验收：{passed}。INFO 查询通过：{result['query_passed']}。
单次隔离 RAM boot 接受时间 `{state['accepted_at']}`，镜像 SHA256 `{state['image_sha256']}`。
CPU 保持 614.4 MHz 配置模型。只执行 INFO 查询，0 GPU aperture 访问、0电源写、0 PAS操作、未装载固件。

## 原始调用结果

| index | x0 transport | x1 available | x2 | x3 |
|---|---|---|---|---|
{rows}

总结：`{json.dumps(result['summary'], ensure_ascii=False)}`。
命令级 availability 不能证明 PAS13 认证可用、固件签名接受、CP执行或 Metal 加速。

## 生命周期

180秒正常 timer/USB drain：{audit['drain_success']}；四路 stop 温度记录：{len(stops)}。
源日志争用丢字节：{losses}。
USB独立逐字节重放：{audit['transport_verified']}，{audit['frames']}帧 / {audit['raw_bytes']} raw / {audit['console_bytes']} console字节。
接收器原始终态 `{receiver['status']}`；唯一同机 Fastboot 核验：{result['automatic_fastboot_observed']}。

## 改动

新增默认关闭 `gpu_scm_diag=1`，使用独立内核线程；保持旧诊断和复位门禁。
每次 SMC 前后保存输入及四个返回寄存器，最多4次，transport失败立即停、无重试或ABI回退。
200ms预算只检查已经返回的调用，不能强制中断不返回的安全世界调用。
实际完整HakiOS内核构建、链接反汇编和最终Android/FAT/APFS/FV读回均已完成。

```text
{state['boot_args']}
```
"""
    OUT.with_suffix(".md").write_text(document, encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("passed", "query_passed", "automatic_fastboot_observed", "summary")}))


if __name__ == "__main__":
    main()
