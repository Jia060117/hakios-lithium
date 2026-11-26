#!/usr/bin/env python3
"""Record one completed CX revision from saved reports, without device I/O."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("02", "03"), required=True)
    args = parser.parse_args()
    n = int(args.attempt)
    report_name = "reports/hakios-ios16-gpu-g1-cx-cycle-" + args.attempt
    report = json.loads((ROOT / (report_name + ".json")).read_text(encoding="utf-8"))
    run = ROOT / ("evidence/gpu-metal-20261123/package/runs/gpu-g1-cx-cycle-" + args.attempt)
    result_path = run / "result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["command_accepted"] and result["accepted_at"] == report["accepted_at"]
    project_path = ROOT / "project.json"
    project = json.loads(project_path.read_text(encoding="utf-8-sig"))
    holders = [v for v in project.values() if isinstance(v, dict) and
               "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(holders) == 1
    counts = holders[0]
    target = (106 + n, 75 + n)
    actual = (counts["hakios_temporary_boot_count"], counts["ios16_boots_performed"])
    assert actual in ((target[0]-1, target[1]-1), target)
    counts.update(hakios_temporary_boot_count=target[0], ios16_boots_performed=target[1])
    gpu = project["gpu_bringup"]
    preserved_stage = gpu["hardware_stage_passed"]
    prefix = "g1_cx_cycle" + args.attempt
    gpu.update(phase="G1_CX_" + args.attempt + ("_local_cycle_verified" if report["passed"] else "_requires_followup"),
               latest_runtime_report=report_name + ".md")
    gpu.update({prefix + "_accepted_at": report["accepted_at"],
                prefix + "_image_sha256": report["image_sha256"],
                prefix + "_runtime_verified": report["passed"],
                prefix + "_auto_fastboot_observed": report["return_verification"].get("passed", False),
                prefix + "_local_restored": report["cx_local_configuration_restored"]})
    assert gpu["hardware_stage_passed"] == preserved_stage
    project_path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result.setdefault("outcome_before_analysis", result.get("outcome"))
    result.update(outcome="cx_cycle_verified" if report["passed"] else "cx_cycle_requires_followup",
                  cx_stage_passed=report["passed"], lifecycle_passed=report["lifecycle_passed"], runtime_report=report_name + ".json")
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    end = report["rows"]["end"][0]
    audit = report["usb"]
    summary = (
        f"{report['accepted_at'][:10]} GPU G1 CX{args.attempt}真机结案：{report['accepted_at']}单次RAM"
        f"{report['image_sha256'][:8]}，{target[0]}/{target[1]}；"
        f"{end['loads']}读/{end['stores']}写/{end['elapsed_us']}us，error{end['error']}/cleanup{end['cleanup_error']}，"
        f"ON验证{report['cx_on_observed']}、本地恢复{report['cx_local_configuration_restored']}，dirty{end['dirty']}/uncertain{end['uncertain']}。"
        f"180timer/四stop/source0/drain及唯一同机Fastboot终点{report['lifecycle_passed']}，"
        f"{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console独立逐字节重放，接收器原始失败状态保留。"
        "不把未采集值当0；CONTROL其余观测位语义未验证，未读GPU/SMMU、未改GX/PLL/电压/RPM票。"
        "CPU614/R20/low01/旧冻结及原索引保持；完整GPU/ID/CP/Metal未验。"
        f"见{report_name}.md/json。\n\n"
    )
    agents_path = ROOT / "AGENTS.md"
    agents = agents_path.read_text(encoding="utf-8")
    if not agents.startswith(summary):
        agents_path.write_text(summary + agents, encoding="utf-8")
    index = hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest()
    assert index == "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"
    print(json.dumps(dict(recorded=True, accepted_boots=target, index_unchanged=True,
                          cx_stage_passed=report["passed"], lifecycle_passed=report["lifecycle_passed"])))


if __name__ == "__main__":
    main()
