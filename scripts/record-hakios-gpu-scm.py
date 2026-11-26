#!/usr/bin/env python3
"""Record the single SCM INFO run using saved evidence only."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INDEX = "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"


def main():
    assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    report_name = "reports/hakios-ios16-gpu-g2-scm-info-01"
    report = json.loads((ROOT / (report_name + ".json")).read_text(encoding="utf-8"))
    run = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g2-scm-info-01"
    state_path = run / "result.json"
    state = json.loads(state_path.read_text(encoding="utf-8"))
    assert state["command_accepted"] and state["accepted_at"] == report["accepted_at"]
    project_path = ROOT / "project.json"
    project = json.loads(project_path.read_text(encoding="utf-8-sig"))
    holders = [value for value in project.values() if isinstance(value, dict) and
               "hakios_temporary_boot_count" in value and "ios16_boots_performed" in value]
    assert len(holders) == 1
    holder = holders[0]
    assert (holder["hakios_temporary_boot_count"], holder["ios16_boots_performed"]) in ((114, 83), (115, 84))
    holder.update(hakios_temporary_boot_count=115, ios16_boots_performed=84)
    gpu = project["gpu_bringup"]
    preserved = gpu["hardware_stage_passed"]
    gpu.update(phase="G2_SCM_available" if report["query_passed"] else "G2_SCM_requires_followup",
        latest_runtime_report=report_name + ".md", scm_info01=dict(
            accepted_at=report["accepted_at"], image_sha256=report["image_sha256"],
            query_passed=report["query_passed"], automatic_fastboot_observed=report["automatic_fastboot_observed"],
            summary=report["summary"], runtime_report=report_name + ".json"))
    assert gpu["hardware_stage_passed"] == preserved
    project_path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    state.setdefault("outcome_before_analysis", state.get("outcome"))
    state.update(outcome="scm_info_verified" if report["passed"] else "scm_info_requires_followup",
                 runtime_report=report_name + ".json", query_passed=report["query_passed"],
                 lifecycle_passed=report["passed"])
    state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    usb = report["usb"]
    summary = (f"2026-11-26 GPU G2 SCM INFO01真机结案：{report['accepted_at']}单次RAM"
        f"{report['image_sha256'][:8]}，115/84；独立内核线程logical0affinity1，"
        f"四次INFO查询通过={report['query_passed']}，SCM64 self和INIT/MEM/AUTH可用性原始x0..x3均保存；"
        "0GPU/PMIC电源写、0GPU aperture访问、0PAS操作，PAS13/认证/CP/Metal仍未验。"
        f"正常180timer/drain={usb['drain_success']}，{len(report['stop_temperature_records'])}路stop，"
        f"source={report['source_log_drops']}，{usb['frames']}帧/{usb['raw_bytes']}raw/{usb['console_bytes']}console"
        f"独立逐字节重放={usb['transport_verified']}，接收器{report['receiver_status']['status']}原样保留；"
        f"唯一同机Fastboot核验={report['automatic_fastboot_observed']}。"
        "04手动恢复及自动返回失败保持，不重复TRUST实验、不放宽返回门禁；CPU614/R20/旧冻结/原索引保持。"
        f"见{report_name}.md/json。\n\n")
    agents_path = ROOT / "AGENTS.md"
    current = agents_path.read_text(encoding="utf-8")
    if summary not in current:
        agents_path.write_text(summary + current, encoding="utf-8")
    progress_path = ROOT / "reports/hakios-ios16-gpu-progress-20261126.md"
    progress = progress_path.read_text(encoding="utf-8")
    title = "# SCM INFO01 实机接口查询完成\n\n"
    if not progress.startswith(title):
        progress_path.write_text(title + summary + progress, encoding="utf-8")
    assert hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest() == INDEX
    print(json.dumps(dict(recorded=True, counts=[115, 84], query_passed=report["query_passed"],
                          lifecycle_passed=report["passed"], index_unchanged=True)))


if __name__ == "__main__":
    main()
