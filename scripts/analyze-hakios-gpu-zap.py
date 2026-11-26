#!/usr/bin/env python3
"""Analyze saved PAS experiment and optionally record its actual outcome."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
INDEX = "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def inspect(text):
    def rows(prefix):
        return [dict(line=n, **fields(line)) for n, line in enumerate(text.splitlines(), 1) if line.startswith(prefix)]
    prep = rows("LITHIUM GPU pas prepare end ")
    reservation = rows("LITHIUM GPU pas reservation ")
    before = rows("LITHIUM GPU pas smc before ")
    after = rows("LITHIUM GPU pas smc after ")
    end = rows("LITHIUM GPU pas end ")
    combined = rows("LITHIUM GPU g1 combined end ")
    prepared = len(prep) == 1 and prep[0].get("error") == "0"
    success = bool(prepared and len(end) == 1 and end[0].get("used") == "1" and
        end[0].get("error") == "0" and end[0].get("authenticated") == "1" and
        end[0].get("shutdown_ok") == "1" and end[0].get("hold_dependencies") == "0" and
        len(before) == len(after) and len(after) in (4, 5) and
        all(row.get("callback") == "0" and int(row["x0"], 0) == 0 for row in after))
    return dict(prepared=prepared, pas_authenticated_and_shutdown=success, prepare=prep,
        reservation=reservation, smc_before=before, smc_after=after, pas_end=end, combined_end=combined,
        diagnostic=rows("LITHIUM GPU pas metadata "), stock_direct=rows("LITHIUM GPU pas stock_direct "))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", default="01")
    parser.add_argument("--record", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"\d{2}", args.attempt):
        raise ValueError("two-digit attempt required")
    attempt = "gpu-g2-zap-" + args.attempt
    run = ROOT / "evidence/gpu-metal-20261123/package/runs" / attempt
    state, receiver = read(run / "result.json"), read(run / "usb-status.json")
    if receiver["status"] in ("waiting", "capturing"):
        raise RuntimeError("receiver still active; no finalization")
    spec = importlib.util.spec_from_file_location("zap_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
    auditor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(auditor)
    console = (run / "console.log").read_bytes()
    audit = auditor.audit((run / "console.frames").read_bytes(), console, run / "capture-host.py")
    text = console.decode(errors="replace")
    result = inspect(text)
    stops = [fields(line) for line in text.splitlines() if line.startswith("LITHIUM CPU TEMP phase=stop ")]
    losses = [int(value) for value in re.findall(r"writer_contention_dropped_bytes=(\d+)", text)]
    returned = read(run / "return-verification.json")
    lifecycle = bool(returned.get("passed") and audit["emitted_kernel_stream_through_timer_verified"] and
                     len(stops) == 4 and losses and not any(losses))
    result.update(schema="lithium-gpu-g2-zap-runtime-v1", attempt=attempt,
        analyzed_at=datetime.now(timezone.utc).isoformat(), accepted_at=state["accepted_at"],
        image_sha256=state["image_sha256"], boot_args=state["boot_args"],
        passed=bool(result["pas_authenticated_and_shutdown"] and lifecycle), lifecycle_passed=lifecycle,
        automatic_fastboot_observed=returned.get("passed") is True, usb=audit,
        receiver_status=receiver, return_verification=returned, stop_temperature_records=stops,
        source_log_drops=losses, cp_verified=False, metal_acceleration_verified=False)
    name = "reports/hakios-ios16-gpu-g2-zap-" + args.attempt
    (ROOT / (name + ".json")).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (run / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    document = f"""# GPU G2 PAS13 {args.attempt} · 2026-11-26

PAS 认证并关闭：{result['pas_authenticated_and_shutdown']}；内存准备通过：{result['prepared']}。
单次 RAM 接受时间 `{state['accepted_at']}`，镜像 `{state['image_sha256']}`。
CPU 保持614.4 MHz配置模型；未提交CP、未运行Metal。

## 实际调用和准备

```json
{json.dumps({key: result[key] for key in ('prepare', 'reservation', 'smc_before', 'smc_after', 'pas_end', 'combined_end', 'diagnostic')}, ensure_ascii=False, indent=2)}
```

未进入时的零初始化 PAS end 不代表认证成功。元数据未通过只代表准备拒绝，不推断安全世界返回。

## 运行终点

正常timer/drain：{audit['drain_success']}；stop温度记录{len(stops)}条；源争用丢字节{losses}。
USB独立重放{audit['transport_verified']}，{audit['frames']}帧 / {audit['raw_bytes']} raw / {audit['console_bytes']} console字节。
接收器原始终态 `{receiver['status']}` 保留；唯一同机Fastboot终点{result['automatic_fastboot_observed']}。
生命周期通过{lifecycle}，不等同PAS或GPU加速通过。

```text
{state['boot_args']}
```
"""
    (ROOT / (name + ".md")).write_text(document, encoding="utf-8")
    if args.record:
        assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
        path = ROOT / "project.json"
        project = read(path)
        gpu = project["gpu_bringup"]
        placeholder = gpu["hardware_stage_passed"]
        runs = gpu.setdefault("g2_pas_runs", {})
        first = attempt not in runs
        holder = [v for v in project.values() if isinstance(v, dict) and "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
        assert len(holder) == 1 and state["command_accepted"]
        if first:
            holder[0]["hakios_temporary_boot_count"] += 1
            holder[0]["ios16_boots_performed"] += 1
        counts = [holder[0]["hakios_temporary_boot_count"], holder[0]["ios16_boots_performed"]]
        runs[attempt] = dict(accepted_at=state["accepted_at"], image_sha256=state["image_sha256"],
            prepared=result["prepared"], authenticated_and_shutdown=result["pas_authenticated_and_shutdown"],
            automatic_fastboot_observed=result["automatic_fastboot_observed"], report=name + ".json")
        gpu.update(phase="G2_PAS_authenticated" if result["pas_authenticated_and_shutdown"] else "G2_PAS_followup",
                   latest_runtime_report=name + ".md")
        assert gpu["hardware_stage_passed"] == placeholder
        path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        state.update(outcome="pas_verified" if result["passed"] else "pas_requires_followup",
                     runtime_report=name + ".json", lifecycle_passed=lifecycle)
        (run / "result.json").write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        summary = (f"2026-11-26 GPU G2 PAS{args.attempt}真机结案：{state['accepted_at']}一次RAM{state['image_sha256'][:8]}，"
            f"{counts[0]}/{counts[1]}；prepared={result['prepared']}、认证并关闭={result['pas_authenticated_and_shutdown']}，"
            f"真实SMC before/after={len(result['smc_before'])}/{len(result['smc_after'])}，未进入时零end不算成功。"
            f"正常timer/drain={audit['drain_success']}，4stop={len(stops)==4}/source={losses}，"
            f"USB {audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console独立重放={audit['transport_verified']}；"
            f"唯一Fastboot={result['automatic_fastboot_observed']}，receiver={receiver['status']}原样保留。"
            f"CPU614/R20/旧冻结/索引保持；CP/Metal未验。见{name}.md/json。\n\n")
        path = ROOT / "AGENTS.md"
        current = path.read_text(encoding="utf-8")
        if summary not in current:
            path.write_text(summary + current, encoding="utf-8")
        assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    print(json.dumps({key: result[key] for key in ("passed", "prepared", "pas_authenticated_and_shutdown", "lifecycle_passed", "automatic_fastboot_observed")}))


if __name__ == "__main__":
    main()
