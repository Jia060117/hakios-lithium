#!/usr/bin/env python3
"""Record the accepted BIMC run and evidence, without any device access."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]


def main():
    r = json.loads((ROOT/"reports/hakios-ios16-gpu-g1-bimc-measure-01.json").read_text(encoding="utf-8"))
    run = ROOT/"evidence/gpu-metal-20261123/package/runs/gpu-g1-bimc-measure-01"
    result = json.loads((run/"result.json").read_text(encoding="utf-8"))
    assert result["command_accepted"]
    p = ROOT/"project.json"
    j = json.loads(p.read_text(encoding="utf-8-sig"))
    owners = [v for v in j.values() if isinstance(v, dict) and
        "hakios_temporary_boot_count" in v and "ios16_boots_performed" in v]
    assert len(owners) == 1
    counts = owners[0]
    assert (counts["hakios_temporary_boot_count"], counts["ios16_boots_performed"]) in ((105,74),(106,75))
    counts.update(hakios_temporary_boot_count=106, ios16_boots_performed=75)
    gpu = j["gpu_bringup"]
    original_placeholder = gpu["hardware_stage_passed"]
    gpu.update(phase="G1_BIMC_measured_GPU_power_chain_next" if r["passed"] else "G1_BIMC_measurement_requires_followup",
        latest_runtime_report="reports/hakios-ios16-gpu-g1-bimc-measure-01.md",
        device_operations_this_phase=10, current_phase_device_operations=10,
        device_operations_revised_plan=10,
        g1_bimc_measure01_accepted_at=r["accepted_at"],
        g1_bimc_measure01_image_sha256=r["image_sha256"],
        g1_bimc_measure01_runtime_verified=r["passed"],
        g1_bimc_measure01_auto_fastboot_observed=r["return_verification"].get("passed",False),
        g1_bimc_configurations_restored=r["configurations_restored"],
        g1_physical_bimc_hz=r["bimc_measured_hz"],
        g1_bimc_measure_reference=r["reference"])
    assert gpu["hardware_stage_passed"] == original_placeholder
    p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    end = r["rows"]["end"][0]
    audit = r["usb"]
    returned = r["return_verification"]
    short,full = [x["count"] for x in r["rows"]["window"]]
    summary = (f"{r['accepted_at'][:10]} GPU G1 BIMC计频01真机结案：00:00:57一次RAM3f37209d，106/75；"
        f"selectorad测BIMC本体short{short}/full{full}，新鲜DONE0→1两窗有效，公式{r['bimc_measured_hz']}Hz（继承XO/4参考未独立校准）。"
        f"{end['loads']}读/{end['stores']}debug写/{end['elapsed_us']}us，restore7/dirty0/uncertain0，两16KiB映射均释放；"
        "只写362000/362004/343008，未写STATUS/PLLTEST/PMIC/RPM票/GPUpowerclock，未读GPU/SMMU/BKE。"
        f"180timer/四stop/source0/drain及唯一同机Fastboot核验{r['lifecycle_passed']}，"
        f"{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console独立逐字节连续重放；"
        f"接收器Pipeerror保留，终点{returned.get('finished_at')}。"
        "Aristotle实际adapter30专项/ARM64，main23参数专项/完整内核a28b8d7a构建读回/上机/恢复与USB独立核对。"
        "BIMC已运行且高于200MHz，不能把GPU-facing分支OFF当上游停钟；MX原AP票仍未知，"
        "不再重测OPEN或重复写已置1的branch，下一GPU自身供电/域/时钟。"
        "CPU614/R20/low01/旧冻结/原索引保持，G1芯片ID/CP/Metal未验。"
        "见reports/hakios-ios16-gpu-g1-bimc-measure-01.md/json。\n\n")
    if not r["passed"]:
        summary = f"{r['accepted_at'][:10]} GPU G1 BIMC计频01一次RAM已接受，106/75；阶段未完整通过，原始结果见reports/hakios-ios16-gpu-g1-bimc-measure-01.md/json，不补认计频/恢复/自动终点。\n\n"
    agents=ROOT/"AGENTS.md"
    old=agents.read_text(encoding="utf-8")
    if not old.startswith(summary):
        agents.write_text(summary+old,encoding="utf-8")
    progress=ROOT/"reports/hakios-ios16-gpu-progress-20261125.md"
    marker="## BIMC本体计频已上机（11月26日00:00）"
    old=progress.read_text(encoding="utf-8")
    if marker not in old:
        progress.write_text(old+f"\n{marker}\n\n"
            f"新鲜short/full为{short}/{full}，BIMC公式{r['bimc_measured_hz']}Hz；"
            f"三项配置恢复{r['configurations_restored']}、自动终点{r['return_verification'].get('passed')}。"
            "本次只使用debug计数器，未改电压或资源票；总计106/75。"
            "上游BIMC已高于200MHz，下一推进GPU自身电源域/时钟与只读识别。"
            "MX原AP投票、chipID、CP/fence和Metal加速尚未验。\n",encoding="utf-8")
    index=hashlib.sha256((ROOT/".git/index").read_bytes()).hexdigest()
    assert index=="fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"
    print(json.dumps(dict(recorded=True, accepted_boots=[106,75], index_unchanged=True)))


if __name__ == "__main__":
    main()
