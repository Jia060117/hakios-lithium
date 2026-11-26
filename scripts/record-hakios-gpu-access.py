#!/usr/bin/env python3
"""Record the completed saved-file GPU access test; no device operations."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "evidence/gpu-metal-20261123/main/access-closeout"
REPORT = "reports/hakios-ios16-gpu-g1-access-01"
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-access-01"


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    report = load(ROOT / (REPORT + ".json"))
    assert report["passed"] and report["automatic_fastboot_observed"]
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    for name in ("project.json", "AGENTS.md"):
        destination = EVIDENCE / "before" / name
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
    project_path = ROOT / "project.json"
    project = load(project_path)
    work = project["work_status"]
    gpu = project["gpu_bringup"]
    if gpu.get("g1_access_authorization_consumed") is not True:
        assert work["hakios_temporary_boot_count"] == 98
        assert work["ios16_boots_performed"] == 67
        work["hakios_temporary_boot_count"] += 1
        work["ios16_boots_performed"] += 1
    work.update(
        status="gpu_G1_access_complete_left_in_fastboot",
        next_step="Controlled GFX enable/restore and acknowledged MX/RPM bus; CPU614.4; A5xx Metal host work in parallel",
        latest_evidence_recorded_at=datetime.now(timezone.utc).isoformat(),
        latest_host_report=REPORT + ".md",
    )
    gpu.update(
        phase="G1_access_verified_controlled_enable_next",
        next_device_approval_required=False,
        authorization_scope="User continued connected-device GPU testing; staged default-off RAM experiments with automatic return",
        report=REPORT + ".md",
        latest_runtime_report=REPORT + ".md",
        g1_device_authorized=True,
        g1_access_authorization_consumed=True,
        g1_access_capture_passed=True,
        g1_access_auto_fastboot_observed=True,
        g1_access_image_sha256=report["image_sha256"],
        g1_access_gfx_write_ee=0,
        g1_access_mx_write_ee=4,
        g1_access_fields=13,
        g1_access_reads=26,
        g1_power_on_passed=False,
        device_operations_revised_plan=8,
        current_phase_device_operations=8,
    )
    # Preserve all unmentioned fields, including literal placeholder values.
    project_path.write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    agents_path = ROOT / "AGENTS.md"
    agents = agents_path.read_text(encoding="utf-8-sig")
    heading = "2026-11-23 GPU G1 access01真机结案"
    if not agents.startswith(heading):
        audit = report["usb"]
        text = (
            f"{heading}：18:19:49单次RAM402375d3，99/68；"
            "logical0affinity1采集13字段双遍26读valid/stable1fff，"
            "GFX APID52/PPID317写EE0、channel CONFIG0/STATUS1；MX APID127/PPID117归属EE4，"
            "不能直接写MX。CX/GX及GPU分支保持关闭，0PMIC事务/0GPU电源写，未读GPU/SMMU。"
            "正常180timer/4stop/source0/drain1target60092，"
            f"{audit['frames']}帧{audit['raw_bytes']}raw/{audit['console_bytes']}console独立逐字节连续通过，"
            "接收器Pipeerror原样保留；18:25:20唯一devices/product核实同机Fastboot。"
            "CPU614.4配置模型保持，G1上电/ID/CP/Metal未验；"
            "主代理负责实际构建、生命周期及26读/USB独立复核，"
            "并行Aristotle补STATUS与恢复、Tesla补RPM、Godel补AIR桥、Chandrasekhar补SMMU。"
            "用户持续授权后续分阶段RAM测试，无需重复审批；"
            "无分区/刷写/ADB/QEMU/重试，原索引和614/R20/low01/旧冻结保持。"
            f"见{REPORT}.md/json。\n\n"
        )
        agents_path.write_text(text + agents, encoding="utf-8")
    result_path = RUN / "result.json"
    result = load(result_path)
    result.update(
        outcome="readonly_access_capture_passed_fastboot_verified",
        runtime_report=REPORT + ".json",
        automatic_fastboot_observed=True,
        gpu_power_on_passed=False,
        accepted_boot_counts={"hakios": 99, "ios16": 68},
    )
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    index_hash = hashlib.sha256((ROOT / ".git/index").read_bytes()).hexdigest()
    assert index_hash == "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"
    (EVIDENCE / "record.json").write_text(json.dumps({
        "saved_runtime_passed": True,
        "accepted_boot_counts": result["accepted_boot_counts"],
        "index_sha256": index_hash,
        "device_operations_by_this_script": 0,
    }, indent=2) + "\n", encoding="utf-8")
    print("Recorded GPU access01: 99/68; saved capture and Fastboot verification passed; index preserved.")


if __name__ == "__main__":
    main()
