#!/usr/bin/env python3
"""Record a completed saved GPU bus experiment without device I/O."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]


def main():
    report_path = ROOT / "reports/hakios-ios16-gpu-g1-bus-read-01.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert report["passed"] and report["capture_verified"]
    p = ROOT / "project.json"
    project = json.loads(p.read_text(encoding="utf-8-sig"))
    testing = project["testing"] if "testing" in project else None
    owners = [v for v in project.values() if isinstance(v, dict) and
              "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(owners) == 1
    counts = owners[0]
    assert (counts["hakios_temporary_boot_count"], counts["ios16_boots_performed"]) in ((104, 73), (105, 74))
    counts["hakios_temporary_boot_count"] = 105
    counts["ios16_boots_performed"] = 74
    gpu = project["gpu_bringup"]
    placeholder = gpu["hardware_stage_passed"]
    gpu.update(phase="G1_bus_clocks_captured_BIMC_measurement_next",
        latest_runtime_report="reports/hakios-ios16-gpu-g1-bus-read-01.md",
        device_operations_this_phase=9, current_phase_device_operations=9,
        device_operations_revised_plan=9,
        g1_bus_read01_accepted_at=report["accepted_at"],
        g1_bus_read01_image_sha256=report["image_sha256"],
        g1_bus_read01_runtime_verified=True, g1_bus_read01_auto_fastboot_observed=True,
        g1_bus_read01_reads=32, g1_bus_read01_elapsed_us=110,
        g1_bus_read01_valid_mask="0x1ffdf", g1_bus_read01_bke_read=False,
        g1_ahb_config_hz=19200000, g1_bimc_gfx_branch_on=True,
        g1_mmss_bimc_gfx_branch_on=False, g1_physical_bimc_hz=None)
    assert gpu["hardware_stage_passed"] == placeholder
    p.write_text(json.dumps(project, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    summary = ("2026-11-25 GPU G1 bus-read01真机结案：23:41:19一次RAM6d6eb8ec，105/74；"
        "logical0affinity1采集16字段双遍32读/110us，全valid/stable1ffdf，6map全部释放，"
        "BKE明确跳过不解释为0；346018=1、8c5024=1，309010=80004221的enable1但halt1，"
        "AHBCMD0/CFG1为19.2MHz配置模型非实测，GPLL/MMPLL原值完整保留。"
        "0资源/PMIC/GPUclock写，未读GPU/SMMU。正常180timer/四stop/source0/drain1target61276，"
        "248帧69261raw/61325console独立逐字节连续通过，Pipeerror保留；23:45:47唯一devices/product核实同机Fastboot。"
        "main实际完整内核b6a34a06构建、镜像读回、18adapter/17parser专项、生命周期/USB独立复核；"
        "end中完整温度printf交错以保留原偏移的派生视图解析，未补字节。"
        "下一测BIMC本体debug计数并恢复本地配置，不能反复重写已置位branch或假设原AP票0；"
        "G1完整上电/ID/CP/Metal未验，CPU614/R20/low01/旧冻结及原索引保持。"
        "见reports/hakios-ios16-gpu-g1-bus-read-01.md/json。\n\n")
    agents = ROOT / "AGENTS.md"
    original = agents.read_text(encoding="utf-8")
    if not original.startswith(summary):
        agents.write_text(summary+original, encoding="utf-8")
    progress = ROOT / "reports/hakios-ios16-gpu-progress-20261125.md"
    marker = "## 总线状态已实机采集（23:45）"
    text = progress.read_text(encoding="utf-8")
    if marker not in text:
        progress.write_text(text+f"\n{marker}\n\n"
            "一次RAM完成16字段双遍32读/110µs，6映射全部释放；"
            "248帧/61325console独立连续重放，正常timer/drain/source0及同机自动Fastboot核验通过。"
            "总计105/74。MMSS→BIMC分支enable1而halt1，AHB继承XO19.2MHz配置；"
            "BKE未读，不把它当作0，也不把OFF分支定位为某个GDSC。"
            "下一最小动作是BIMC本体debug计频与配置恢复，"
            "不再重复OPEN，不发原AP投票未知的降票/恢复票。"
            "芯片ID、CP/fence、Metal加速仍未验。\n", encoding="utf-8")
    index = ROOT / ".git/index"
    sha = hashlib.sha256(index.read_bytes()).hexdigest()
    assert sha == "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"
    print(json.dumps(dict(recorded=True, accepted_boots=[105, 74], index_sha256=sha)))


if __name__ == "__main__":
    main()
