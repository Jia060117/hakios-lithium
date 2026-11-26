#!/usr/bin/env python3
"""Record the completed MX SMEM capture from saved evidence only."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    report_name = "reports/hakios-ios16-gpu-g1-mx-plan-01"
    report = json.loads((ROOT / (report_name + ".json")).read_text(encoding="utf-8"))
    run = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-mx-plan-01"
    result_path = run / "result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["command_accepted"] and result["accepted_at"] == report["accepted_at"]
    project_path = ROOT / "project.json"
    project = json.loads(project_path.read_text(encoding="utf-8-sig"))
    holders = [v for v in project.values() if isinstance(v, dict) and
               "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(holders) == 1
    counts = holders[0]
    assert (counts["hakios_temporary_boot_count"], counts["ios16_boots_performed"]) in ((109, 78), (110, 79))
    counts.update(hakios_temporary_boot_count=110, ios16_boots_performed=79)
    gpu = project["gpu_bringup"]
    preserved_stage = gpu["hardware_stage_passed"]
    independent = report["independent_stock_rpm_v1_decode"]
    required = independent["required_upper_bound_uV"] if independent else None
    bound = bool(independent and independent["inherited_850_meets_decoded_voltage_bound"])
    gpu.update(phase="G1_MX_boot_plan_captured" if report["passed"] else "G1_MX_boot_plan_requires_followup",
        latest_runtime_report=report_name + ".md",
        g1_mx_plan01_accepted_at=report["accepted_at"],
        g1_mx_plan01_image_sha256=report["image_sha256"],
        g1_mx_plan01_capture_verified=report["read_capture_passed"],
        g1_mx_plan01_decoded=report["passed"],
        g1_mx_plan01_required_upper_bound_uv=required,
        g1_mx_plan01_inherited850_bound=bound,
        g1_mx_plan01_same_boot_rail_ready_observed=False,
        g1_mx_plan01_auto_fastboot_observed=report["return_verification"].get("passed", False))
    assert gpu["hardware_stage_passed"] == preserved_stage
    project_path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result.setdefault("outcome_before_analysis", result.get("outcome"))
    result.update(outcome="mx_boot_plan_verified" if report["passed"] else "mx_boot_plan_requires_followup",
        mx_plan_capture_passed=report["read_capture_passed"], mx_plan_decoded=report["passed"],
        lifecycle_passed=report["lifecycle_passed"], runtime_report=report_name + ".json")
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    end, audit = report["rows"]["end"][0], report["usb"]
    summary = (
        f"{report['accepted_at'][:10]} GPU G1 MX启动配置01真机结案：{report['accepted_at']}单次RAM"
        f"{report['image_sha256'][:8]}，110/79；SMEM473双遍{end['loads']}只读/{end['elapsed_us']}us，"
        f"capture{report['read_capture_passed']}/decode{report['passed']}，目标上界{required}uV、850边界{bound}。"
        f"{end['maps']}map/{end['unmaps']}unmap，0写/票/重试，未读GPU/SMMU。"
        f"180timer/四stop/source0/drain及同机Fastboot终点{report['lifecycle_passed']}，"
        f"{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console独立逐字节重放；Pipeerror原样保留。"
        "启动交接配置不等当前RPM私有副本或实时模拟电压；上电仍需同次PMIC数字设定/ready。"
        "CPU614/R20/low01/旧冻结/原索引保持，G1完整上电/ID/CP/Metal未验。"
        f"见{report_name}.md/json。\n\n"
    )
    agents_path = ROOT / "AGENTS.md"
    agents = agents_path.read_text(encoding="utf-8")
    if not agents.startswith(summary):
        agents_path.write_text(summary + agents, encoding="utf-8")
    index = hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest()
    assert index == "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"
    print(json.dumps(dict(recorded=True, accepted_boots=[110, 79], index_unchanged=True,
        mx_plan_decoded=report["passed"], required_uv=required, lifecycle_passed=report["lifecycle_passed"])))


if __name__ == "__main__":
    main()
