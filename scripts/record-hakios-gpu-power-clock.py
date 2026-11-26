#!/usr/bin/env python3
"""Record accepted combined GPU experiments using saved evidence only."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INDEX = "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02", "03", "04"), required=True)
    suffix = parser.parse_args().attempt
    assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    name = "reports/hakios-ios16-gpu-g1-power-clock-" + suffix
    report = json.loads((ROOT / (name + ".json")).read_text(encoding="utf-8"))
    run = ROOT / ("evidence/gpu-metal-20261123/package/runs/gpu-g1-power-clock-" + suffix)
    result_path = run / "result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["command_accepted"] and result["accepted_at"] == report["accepted_at"]
    project_path = ROOT / "project.json"
    project = json.loads(project_path.read_text(encoding="utf-8-sig"))
    holders = [v for v in project.values() if isinstance(v, dict) and
               "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(holders) == 1
    target = (110 + int(suffix), 79 + int(suffix))
    current = (holders[0]["hakios_temporary_boot_count"], holders[0]["ios16_boots_performed"])
    assert current in (target, (target[0]-1, target[1]-1))
    holders[0].update(hakios_temporary_boot_count=target[0], ios16_boots_performed=target[1])
    gpu = project["gpu_bringup"]
    preserved_stage = gpu["hardware_stage_passed"]
    records = gpu.setdefault("power_clock_runs", {})
    incomplete = report.get("observation_status") == "incomplete_stage_auto_return_not_observed"
    end = report["rows"]["end"][0] if report["rows"]["end"] else {}
    records[suffix] = dict(accepted_at=report["accepted_at"], image_sha256=report["image_sha256"],
        hardware_stage_passed=report["hardware_stage_passed"],
        normal_timer_and_transport_passed=report["normal_timer_and_transport_passed"],
        auto_fastboot_observed=bool(report["return_verification"].get("passed")),
        return_status=report["return_verification"].get("status", "endpoint_checked"),
        blocker=end.get("blocker_name", report.get("failure_reason", "unobserved")),
        observation_status=report.get("observation_status", "terminal_observed"), runtime_report=name + ".json")
    phase = "G2_trust_requires_followup" if incomplete and report.get("trust_requested") else (
        "G1_power_clock_verified" if report["passed"] else "G1_power_clock_requires_followup")
    gpu.update(phase=phase,
        latest_runtime_report=name + ".md")
    assert gpu["hardware_stage_passed"] == preserved_stage
    project_path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result.setdefault("outcome_before_analysis", result.get("outcome"))
    result.update(outcome=report["observation_status"] if incomplete else (
        "power_clock_verified" if report["passed"] else "power_clock_requires_followup"),
        runtime_report=name + ".json", hardware_stage_passed=report["hardware_stage_passed"],
        normal_timer_and_transport_passed=report["normal_timer_and_transport_passed"],
        lifecycle_passed=report["lifecycle_passed"])
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    usb = report["usb"]
    if incomplete:
        summary = (f"{report['accepted_at'][:10]} GPU TRUST实验{suffix}未完整退出：{report['accepted_at']}"
            f"单次RAM{report['image_sha256'][:8]}，{target[0]}/{target[1]}；"
            "仅捕获combined begin，无combined/TRUST结束、正常timer/四stop/drain或最终源丢计数。"
            f"{report['return_verification'].get('finished_at', '时间未记录')}唯一Fastboot核验未通过，"
            "用户终屏反馈独立保存，未获照片前保留原因未知。"
            f"{usb['frames']}帧/{usb['raw_bytes']}raw/{usb['console_bytes']}console独立重放连续，"
            f"接收器{report['receiver']['status']}为主机stop结束，不记Pipeerror或正常设备退出。"
            "本轮TRUST访问/供电清理未验证，不移用03成功；不重查/重启/重试，不放宽返回条件。"
            f"CPU614/R20/旧冻结/原索引保持。见{name}.md/json。\n\n")
    else:
        s1 = report["rows"]["s1"]["end"][0]
        summary = (f"{report['accepted_at'][:10]} GPU G1组合电源/时钟{suffix}真机结案：{report['accepted_at']}"
        f"单次RAM{report['image_sha256'][:8]}，{target[0]}/{target[1]}；"
        f"stage={report['hardware_stage_passed']}，blocker={end['blocker_name']}；"
        f"S1 error={s1['error']}/commands={s1['commands']}/loads={s1['loads']}，"
        f"clock stores={end['clock_stores']}，VBIF valid={end['vbif_valid']}。"
        f"正常timer/四stop/source0/排空={report['normal_timer_and_transport_passed']}，"
        f"同机Fastboot终点={bool(report['return_verification'].get('passed'))}"
        f"（{report['return_verification'].get('status', 'endpoint_checked')}）；"
        f"{usb['frames']}帧/{usb['raw_bytes']}raw/{usb['console_bytes']}console独立逐字节重放，Pipeerror保留。"
        "无效原始0不作电压或APID测量；启动表非模拟实测。未提交CP/Metal，CPU614/R20/旧冻结/原索引保持。"
        f"见{name}.md/json。\n\n")
    agents_path = ROOT / "AGENTS.md"
    agents = agents_path.read_text(encoding="utf-8")
    if summary not in agents:
        agents_path.write_text(summary + agents, encoding="utf-8")
    assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    print(json.dumps(dict(recorded=True, counts=target, stage=report["hardware_stage_passed"],
        timer_transport=report["normal_timer_and_transport_passed"], lifecycle=report["lifecycle_passed"],
        index_unchanged=True)))


if __name__ == "__main__":
    main()
