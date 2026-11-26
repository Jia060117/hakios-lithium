#!/usr/bin/env python3
"""Record one accepted screening run without commanding the device or index."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt",required=True)
    args=parser.parse_args()
    if not re.fullmatch(r"r37-frequency-\d{2}",args.attempt):
        raise ValueError("bad phase-scoped identifier")
    run=ROOT/"reports/local/r37-attempts"/args.attempt
    state=load(run/"result.json")
    if not state.get("command_accepted"):raise ValueError("boot not accepted")
    target=int(re.search(r"\bcpu_perf_hz=(\d+)\b",state["boot_args"])[1])
    project=load(ROOT/"project.json")
    search=project.setdefault("r39_frequency_search",dict(
        goal="Stable high-frequency sweet spot from sequential stock corners",
        baseline_hakios_boots=79,baseline_ios16_boots=48,
        runs={},final_sweet_spot_verified=False))
    search["runs"][args.attempt]=dict(
        accepted_at=state["accepted_at"],target_hz=target,
        image_sha256=state["image_sha256"],status="running")
    analysis_path=run/"r39-analysis.json"
    analysis=load(analysis_path) if analysis_path.exists() else None
    if analysis:
        search["runs"][args.attempt].update(
            status="screen_passed" if analysis["passed"] else
                   "no_kernel_log_obtained" if analysis["transport"]["frames"]==0 else
                   "thermal_limit_stopped" if analysis["soak"].get("hotstop")=="1" else
                   "correctness_passed_return_not_verified"
                   if analysis["correctness_soak_passed"] and
                      analysis["normal_return_and_stream"] and
                      not analysis["fastboot_endpoint_verified"] else "screen_failed",
            benchmark=analysis["benchmarks"],temperature=analysis["temperature"],
            soak=analysis["soak"],return_verified=analysis["fastboot_endpoint_verified"],
            report="reports/hakios-ios16-device-r39-"+args.attempt+".json")
    counts=dict(hakios=search["baseline_hakios_boots"]+len(search["runs"]),
                ios16=search["baseline_ios16_boots"]+len(search["runs"]))
    project["work_status"].update(
        hakios_temporary_boot_count=counts["hakios"],ios16_boots_performed=counts["ios16"],
        status="r39_stock_frequency_search_active")
    project["stage"]="R39: sequential stock-frequency screening; sweet spot not yet qualified"
    search["updated_at"]=datetime.now().astimezone().isoformat()
    save(ROOT/"project.json",project)
    state["boot_counts"]=counts
    state["r39_screening_status"]=search["runs"][args.attempt]["status"]
    save(run/"result.json",state)
    report=ROOT/("reports/hakios-ios16-device-r39-"+args.attempt)
    save(report.with_suffix(".json"),dict(
        attempt=state,analysis=analysis,screening_only=True,
        final_sweet_spot_verified=False))
    text=(f"# R39 {target/1000000:g} MHz 固定档筛选\n\n"
          f"单次接受：{state['accepted_at']}。镜像 `{state['image_sha256']}`。\n\n"
          f"启动计数 {counts['hakios']}/{counts['ios16']}。隔离 RAM，三颗 XNU 核，"
          "共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。\n\n"
          f"```text\n{state['boot_args']}\n```\n\n")
    if analysis:
        if analysis["transport"]["frames"]==0:
            text+="接收器未取得内核 USB 日志。本轮 PLL/MUX 实际值、XNU 是否启动、温度、负载、timer/drain 及返回机制均未知；不能将零日志当作零错误或频率不稳定的证明。应先保留当前屏幕再恢复 Fastboot。\n\n"
        text+=("本轮筛选："+("通过" if analysis["passed"] else "未通过")+
               "。这不是最终甜点频率或桌面长稳结论。\n\n"
               "| 物理核 | 原生温度 min–max °C | 样本 |\n|---|---:|---:|\n")
        for affinity,temperature in analysis["temperature"].items():
            text+=(f"| {affinity} | {temperature['min_mC']/1000:g}–"
                   f"{temperature['max_mC']/1000:g} | {temperature['samples']} |\n")
        if analysis["soak"].get("hotstop")=="1":
            text+="\n本轮因温度上限提前停止；不能把未完成的负载记为稳定性通过，也不能将 hotstop 计数当作运算错误。各核校验原始记录保留在对应 JSON。\n"
        text+="\n短基准原始字段：\n\n```json\n"+json.dumps(
            analysis["benchmarks"],indent=2)+"\n```\n\n"
        parallel=analysis.get("simultaneous_load_tested",False)
        perf_only=analysis.get("pwr_housekeeping_only",False)
        duration=analysis.get("requested_load_seconds",90)
        text+=(f"{duration} 秒性能簇双核并发，引导核仅作 housekeeping" if perf_only else
               f"{duration} 秒三核并发负载结果" if parallel else f"{duration} 秒轮换逐核负载结果")+":\n\n```json\n"+json.dumps(
            analysis["soak"],indent=2)+"\n```\n\n"
        text+=(f"USB {analysis['transport']['frames']} 帧 / "
               f"{analysis['transport']['console_bytes']} console 字节；"
               f"正常 timer/drain：{analysis['normal_return_and_stream']}；"
               f"Fastboot 同设备终点：{analysis['fastboot_endpoint_verified']}。\n\n"
               +("本轮未给引导核施加持续合成负载，不是三核满载验收；" if perf_only else
                 "三核绑定并发负载通过不等于长期稳定；" if parallel else
                 "轮换负载不是三个核同时负载；")+"PMU 实际 MHz、模拟电压、校准、"
               "最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error "
               "与完整保存字节、返回终点分开记录。\n")
    else:
        text+="当前 USB 运行观察中，尚未取得最终温度、负载正确性和 Fastboot 返回证据。\n"
    report.with_suffix(".md").write_text(text,encoding="utf-8")
    print(json.dumps(dict(attempt=args.attempt,target_hz=target,counts=counts,
                          status=search["runs"][args.attempt]["status"]),indent=2))


if __name__=="__main__":
    main()
