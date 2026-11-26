#!/usr/bin/env python3
"""Record saved G3 discovery evidence; never contact a device."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OWN = ROOT / "evidence/gpu-metal-20261126/g3-package"
INDEX = "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02"), default="01")
    number = parser.parse_args().attempt
    attempt = "gpu-g3-read-" + number
    runtime = read(OWN / ("runtime-" + number + ".json"))
    run = OWN / "runs" / attempt
    state = read(run / "result.json")
    assert state["attempt_id"] == attempt and state["command_accepted"]
    assert runtime["final"] and runtime["accepted_at"] == state["accepted_at"]
    assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    name = "reports/hakios-ios16-gpu-g3-read-" + number
    report = dict(runtime, recorded_at=datetime.now().astimezone().isoformat())
    report["scm_interrupted_observation"] = [row for row in runtime["pas"]["smc_after"]
        if int(row.get("x0", "-1"), 0) == 1]
    report["interpretation_limit"] = (
        "SCM x0=1 is an interrupted service requiring protocol continuation, not a final secure rejection. "
        "The saved test did not implement continuation. A later build must preserve this raw result. "
        "A missing Fastboot endpoint does not by itself identify the physical retained screen or reset cause.")
    (ROOT / (name + ".json")).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    usb = runtime["usb"]
    summary = runtime["discover"]
    returned = runtime["return_verification"].get("passed") is True
    document = f"""# GPU G3 read {number} · 2026-11-26

单次隔离 RAM 镜像 `{state['image_sha256']}`，接受于 `{state['accepted_at']}`。
SMMU 完整发现通过：{runtime['smmu_read_only_discovery_passed']}；同机 Fastboot 终点：{returned}。
CPU 保持 614.4 MHz 配置模型，CP、Metal 和 CoreAnimation 硬件执行尚未验证。

## 实际发现

```json
{json.dumps({'discover': summary, 'geometry': runtime['geometry'], 'raw': runtime['raw'], 'pas_end': runtime['pas']['pas_end'], 'interrupted': report['scm_interrupted_observation']}, ensure_ascii=False, indent=2)}
```

只有 valid=3 且双遍相同的值参与解码。未采集路由、CB、页表和 fault 值保持 null。
本轮没有 SMMU 写入、页表分配、TLB 失效或 CP 提交。原始 ID7 与旧 capability 拒绝保留，
后续按固定 Linux compatible 选择版本的改动不会补认本轮完整通过。

SCM 的 interrupted 原值必须保留；本轮没有续接该调用，不能把它当作最终安全世界拒绝。
供电依赖是否恢复以 combined 原始记录为准，不移用上一轮成功结果。

## 退出与日志

180 秒正常 timer/drain：{usb['emitted_kernel_stream_through_timer_verified']}；
源争用丢字节：{runtime['source_log_drops']}；stop 温度记录 {len(runtime['stop_temperature_records'])} 条。
USB {usb['frames']} 帧、{usb['raw_bytes']} raw / {usb['console_bytes']} console 字节，独立重放 {usb['transport_verified']}。
接收器原始终态 `{runtime['receiver_status']['status']}` 保留。
唯一返回检查 `{runtime['return_verification']['started_at']}`：Fastboot={returned}，不重查、不自动重试。
Fastboot 未找到不能单独证明物理终屏或复位失败原因。

```text
{state['boot_args']}
```
"""
    (ROOT / (name + ".md")).write_text(document, encoding="utf-8")
    path = ROOT / "project.json"
    project = read(path)
    gpu = project["gpu_bringup"]
    placeholder = gpu["hardware_stage_passed"]
    runs = gpu.setdefault("g3_discovery_runs", {})
    counters = [v for v in project.values() if isinstance(v, dict) and
        "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(counters) == 1
    counter = counters[0]
    if attempt not in runs:
        counter["hakios_temporary_boot_count"] += 1
        counter["ios16_boots_performed"] += 1
    runs[attempt] = dict(accepted_at=state["accepted_at"], image_sha256=state["image_sha256"],
        complete_discovery=runtime["smmu_read_only_discovery_passed"], automatic_fastboot_observed=returned,
        report=name + ".json")
    gpu.update(phase="G3_live_discovery", latest_runtime_report=name + ".md")
    assert gpu["hardware_stage_passed"] == placeholder
    path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    state.update(runtime_report=name + ".json", outcome="discovery_verified" if report["passed"] else
        "discovery_followup_required", lifecycle_passed=runtime["lifecycle_passed"])
    (run / "result.json").write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    header = (f"2026-11-26 GPU G3 read{number}真机结案：{state['accepted_at']}一次RAM{state['image_sha256'][:8]}，"
        f"{counter['hakios_temporary_boot_count']}/{counter['ios16_boots_performed']}；完整发现={runtime['smmu_read_only_discovery_passed']}，"
        f"仅按有效原始采样解码，SCM interrupted原值保留；正常timer/drain={usb['drain_success']}，"
        f"USB{usb['frames']}帧/{usb['raw_bytes']}raw/{usb['console_bytes']}console独立重放={usb['transport_verified']}，"
        f"唯一Fastboot={returned}，不重查或重试。0SMMU写/CP提交，Metal未验；CPU614/R20/旧冻结/索引保持。见{name}.md/json。\n\n")
    path = ROOT / "AGENTS.md"
    old = path.read_text(encoding="utf-8")
    if header not in old:
        path.write_text(header + old, encoding="utf-8")
    assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    print(json.dumps(dict(recorded=attempt, counts=[counter["hakios_temporary_boot_count"],
        counter["ios16_boots_performed"]], complete=runtime["smmu_read_only_discovery_passed"], returned=returned)))


if __name__ == "__main__":
    main()
