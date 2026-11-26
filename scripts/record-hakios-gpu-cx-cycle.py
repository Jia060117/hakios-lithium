#!/usr/bin/env python3
"""Record saved CX evidence only; never opens or queries the phone."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]


def main():
    report_name = "reports/hakios-ios16-gpu-g1-cx-cycle-01"
    report = json.loads((ROOT / (report_name + ".json")).read_text(encoding="utf-8"))
    run = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-cx-cycle-01"
    result_path = run / "result.json"
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["command_accepted"]
    project_path = ROOT / "project.json"
    project = json.loads(project_path.read_text(encoding="utf-8-sig"))
    counters = [v for v in project.values() if isinstance(v, dict) and
                "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(counters) == 1
    counts = counters[0]
    assert (counts["hakios_temporary_boot_count"], counts["ios16_boots_performed"]) in ((106, 75), (107, 76))
    counts.update(hakios_temporary_boot_count=107, ios16_boots_performed=76)
    gpu = project["gpu_bringup"]
    preserved_stage = gpu["hardware_stage_passed"]
    gpu.update(
        phase="G1_CX_cycle01_passed" if report["passed"] else "G1_CX_cycle01_readback_restore_unverified",
        latest_runtime_report=report_name + ".md",
        g1_cx_cycle01_accepted_at=report["accepted_at"],
        g1_cx_cycle01_image_sha256=report["image_sha256"],
        g1_cx_cycle01_runtime_verified=report["passed"],
        g1_cx_cycle01_auto_fastboot_observed=report["return_verification"].get("passed", False),
        g1_cx_cycle01_local_restored=report["cx_local_configuration_restored"],
    )
    assert gpu["hardware_stage_passed"] == preserved_stage
    project_path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    end = report["rows"]["end"][0]
    audit = report["usb"]
    returned = report["return_verification"]
    result.setdefault("outcome_before_analysis", result.get("outcome"))
    result.update(outcome="cx_cycle_passed_and_fastboot_verified" if report["passed"] else
                  "cx_readback_failed_restore_unverified_lifecycle_" + str(report["lifecycle_passed"]).lower(),
                  cx_stage_passed=report["passed"], runtime_report=report_name + ".json")
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    summary = (
        f"{report['accepted_at'][:10]} GPU G1 CX本地请求01真机结案：{report['accepted_at']}一次RAMdd4f5ee3，107/76；"
        f"初始CONTROL222001/STATUS5ff0，实际{end['loads']}读/{end['stores']}写/{end['elapsed_us']}us，"
        f"error{end['error']}/cleanup{end['cleanup_error']}，ON验证{report['cx_on_observed']}、本地恢复{report['cx_local_configuration_restored']}，"
        f"dirty{end['dirty']}/uncertain{end['uncertain']}。"
        "写轨迹282001→282000，首版未记录失败读回原值，不能据此猜未知动态位或放宽检查；保持失败，不补认CX/GPU上电。"
        f"180timer/四stop/source0/drain与唯一同机Fastboot终点{report['lifecycle_passed']}，"
        f"{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console独立逐字节重放，Pipeerror保留，终点{returned.get('finished_at')}。"
        "main实际完整内核ac82e735构建/包读回/上机，Aristotle37adapter专项与ARM64；下一只补原回调读回值和失败位置，不增加硬件操作或猜掩码。"
        "无GX/PLL/电压/RPM票/GPU或SMMU访问，CPU614/R20/low01/旧冻结及原索引保持。"
        f"见{report_name}.md/json。\n\n"
    )
    agents_path = ROOT / "AGENTS.md"
    agents = agents_path.read_text(encoding="utf-8")
    if not agents.startswith(summary):
        agents_path.write_text(summary + agents, encoding="utf-8")
    index = hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest()
    assert index == "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"
    print(json.dumps(dict(recorded=True, accepted_boots=[107, 76], index_unchanged=True,
                          cx_stage_passed=report["passed"], lifecycle_passed=report["lifecycle_passed"])))


if __name__ == "__main__":
    main()
