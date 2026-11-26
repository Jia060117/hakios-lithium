#!/usr/bin/env python3
"""Analyze saved G1 combination bytes; never contact the device."""
from datetime import datetime, timezone
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-power-clock-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-power-clock-01"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(text):
    return dict(re.findall(r"(\w+)=([^\s]+)", text))


def terminal_observations(data):
    """Record absent evidence as absent, without decoding zero-filled structs."""
    text = data.decode("utf-8", errors="replace")
    prefixes = {
        "combined_begin": "LITHIUM GPU g1 combined begin ",
        "combined_end": "LITHIUM GPU g1 combined end ",
        "trust_end": "LITHIUM GPU trust end ",
    }
    rows = {name: [fields(m.group()) for m in re.finditer(
        re.escape(prefix) + r"[^\r\n]*", text)] for name, prefix in prefixes.items()}
    return dict(rows=rows,
        normal_timer_marker_observed=b"--- loader_return_after expired ---" in data,
        stop_temperature_rows=len(re.findall(rb"LITHIUM CPU TEMP phase=stop ", data)),
        source_loss_record_observed=b"LITHIUM LOG writer_contention_dropped_bytes=" in data,
        panic_text_observed=bool(re.search(rb"(?im)^panic\(|^panic\(cpu|^Kernel panic", data)))


def emit_incomplete(run, out, data, status, result, returned, audit, observations):
    """Close an incomplete hardware observation without inventing a terminal result."""
    trust_requested = "gpu_trust_zero=1" in result["boot_args"].split()
    report = dict(schema="lithium-gpu-g1-power-clock-runtime-v1", passed=False,
        hardware_stage_passed=False, lifecycle_passed=False,
        observation_status="incomplete_stage_auto_return_not_observed",
        failure_reason="combined_terminal_record_missing",
        analyzed_at=datetime.now(timezone.utc).isoformat(),
        normal_timer_and_transport_passed=False,
        power_clock_stage_passed=None, trust_requested=trust_requested,
        trust_register_experiment_passed=None,
        trust_rows=observations["rows"]["trust_end"],
        g2_cp_nonsecure_verified=False, accepted_at=result["accepted_at"],
        image_sha256=result["image_sha256"], boot_args=result["boot_args"],
        rows={"begin": observations["rows"]["combined_begin"], "end": [],
              "s1": {"end": []}}, terminal_observations=observations,
        independent_mx_plan=None, independent_s1_plan=None, independent_s1_digital=None,
        same_boot_s1_voltage_bound_met=None, hardware_chip_id=None,
        vbif_version_raw=None, gpu_clock_configuration_hz=None, gpu_clock_measured_hz=None,
        cp_or_gpu_submission_verified=False, metal_acceleration_verified=False,
        usb=audit, receiver=status, return_verification=returned,
        original_console_modified=False,
        original_console_sha256=hashlib.sha256(data).hexdigest(),
        limitation="No combined terminal record: actual TRUST access, cleanup, timer return, "
                   "and retained-screen reason are not recoverable from the saved stream.")
    observation_path = run / "user-terminal-observation.json"
    if observation_path.exists():
        report["user_terminal_observation"] = read(observation_path)
    user_message = report.get("user_terminal_observation", {}).get("message", "未收到本轮物理终屏反馈")
    out.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (run / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    out.with_suffix(".md").write_text(f"""# GPU G1组合电源/时钟与TRUST实验：未完整退出

单次RAM接受 `{result['accepted_at']}`，镜像 `{result['image_sha256']}`。
本轮自动返回失败：唯一Fastboot核验 `{returned.get('finished_at', returned.get('started_at'))}`
未找到目标（passed=`{returned.get('passed', False)}`）；不再次查询、启动或重启。
用户终屏反馈：{user_message}。报告生成时尚无该总结画面的照片或文字。

保存日志有测试开始记录，但无组合阶段结束或TRUST结束记录，无正常timer、四路stop及drain证明。
这不能证明TRUST读/写成功，不能确定访问在哪一条指令停止，也不能把旧03结果作为本轮清理成功。
本轮供电/时钟恢复、TRUST值、源日志最终丢字节计数与总结保留原因均未验证；缺失值保持null。

USB `{audit['frames']}`帧 / `{audit['raw_bytes']}`原始字节 / `{audit['console_bytes']}`控制台字节，
独立逐字节重放连续性=`{audit['transport_verified']}`；这只证明已收集流，无正常结束证明。
接收器状态 `{status['status']}`，原始失败字段 `{status.get('failure', '')}`。
返回核验主机stop请求=`{returned.get('capture_stop_requested', False)}`，不据此推断设备正常断开或Pipe error。

当前结论：TRUST直接访问实验未验收，自动退场要求未满足；保留旧03上电/时钟/VBIF通过记录。
下一证据是当前总结页的panic/retained reason行。照片保存后才进行人工Fastboot恢复，
不重复执行04，不放宽panic/正常timer/排空条件来强制重启。
未验证完整chip ID、CP/fence或Metal加速，CPU614.4/R20及旧冻结保持。
""", encoding="utf-8")
    return report


def parse(data):
    temp = re.compile(rb"LITHIUM CPU TEMP (?:(?!LITHIUM)[^\r\n])*freshness_verified=0\r?\n")
    spans = [dict(kind="complete_temperature", start=m.start(), end=m.end(), text=m.group().decode()) for m in temp.finditer(data)]
    # Exact complete boot printf interleaved inside the MX decode printf.
    # Retain original byte offsets and require both surrounding fragments.
    boot = b"No POST boot-arg set.\n"
    for match in re.finditer(re.escape(boot), data):
        before, after = data[max(0, match.start()-26):match.start()], data[match.end():match.end()+25]
        if before.endswith(b"mode_count=3 modes_offset") and after.startswith(b"=0x1a8 mode_row=0x1a8 "):
            spans.append(dict(kind="complete_boot_printf", start=match.start(), end=match.end(), text=boot.decode()))
    spans.sort(key=lambda s: s["start"])
    assert all(a["end"] <= b["start"] for a, b in zip(spans, spans[1:]))
    parts, cursor = [], 0
    for span in spans:
        parts.append(data[cursor:span["start"]]); cursor = span["end"]
    parts.append(data[cursor:])
    text = b"".join(parts).decode("utf-8", errors="replace")
    rows = {}
    for key in ("begin", "write", "clock", "parent", "bus_post", "end"):
        rows[key] = [fields(m.group()) for m in re.finditer(
            re.escape("LITHIUM GPU g1 combined " + key + " ") + r"[^\r\n]*", text)]
    assert all(len(rows[k]) == 1 for k in ("begin", "clock", "parent", "bus_post", "end"))
    begin, end = rows["begin"][0], rows["end"][0]
    assert begin["logical"] == "0" and int(begin["affinity"], 0) == 1
    assert begin["total_us"] == end["total_us"] == "200000"
    assert end["rpm_votes"] == end["mx_writes"] == end["hardware_chip_id_valid"] == "0"
    assert end["smmu"] == "unread" and int(end["clock_stores"]) <= 128
    assert len(rows["write"]) == int(end["clock_stores"])
    assert [int(r["index"]) for r in rows["write"]] == list(range(len(rows["write"])))
    for prefix in ("LITHIUM GPU S1 ", "LITHIUM GPU s1 plan ", "LITHIUM GPU mx plan "):
        name = {"LITHIUM GPU S1 ": "s1", "LITHIUM GPU s1 plan ": "s1_plan", "LITHIUM GPU mx plan ": "mx_plan"}[prefix]
        rows[name] = {}
        for key in (("byte", "end") if name == "s1" else ("toc", "word", "decode", "end")):
            rows[name][key] = [fields(m.group()) for m in re.finditer(re.escape(prefix + key + " ") + r"[^\r\n]*", text)]
        assert len(rows[name]["end"]) == 1
    return rows, spans


def independent_plan(rows, rail, corner):
    if len(rows["decode"]) != 1 or rows["end"][0]["plan_valid"] != "1":
        return None
    toc = [[int(r["raw"], 0) for r in rows["toc"] if int(r["pass"]) == p] for p in (0, 1)]
    words = [[(int(r["offset"], 0), int(r["raw"], 0)) for r in rows["word"] if int(r["pass"]) == p] for p in (0, 1)]
    assert toc[0] == toc[1] and len(toc[0]) == 5 and words[0] == words[1] and len(words[0]) <= 28
    assert all(r["loaded"] == r["valid"] == "1" for r in rows["toc"] + rows["word"])
    initialized, allocated, off, size, aux = toc[0]
    assert initialized == allocated == 1 and 200 <= size <= 4096 and not size % 8
    assert off >= 0x1e70 and not off % 8 and off + size <= 0x200000 and (aux & 0xfffffffc) in (0, 0x86000000)
    d = dict(words[0]); assert len(d) == len(words[0]) and all(not k % 4 and 0 <= k <= size-4 for k in d)
    assert d[0] == 1 and 1 <= d[196] <= 8
    headers = [4+24*i for i in range(d[196]) if d[4+24*i] == rail]
    assert len(headers) == 1
    h = headers[0]; kind, _, _, n, start = [d[h+4*i] for i in range(1, 6)]
    assert kind in (0, 1) and 1 <= n <= 10 and start >= 200 and not start % 4
    stride = 48 if kind else 8
    assert start + n*stride <= size
    corners = [d[start+i*stride] & 255 for i in range(n)]
    assert len(set(corners)) == n and all(c < 10 for c in corners) and corners.count(corner) == 1
    row = start + corners.index(corner)*stride
    if kind:
        ceiling, floor, initial = d[row+4], d[row+8], d[row+44]
        assert 350000 <= floor <= initial <= ceiling <= 1355000
        required = ceiling
    else:
        required = d[row+4]; assert 350000 <= required <= 1355000
        ceiling = floor = initial = None
    assert required == int(rows["decode"][0]["required_uv"])
    return dict(rail_id=rail, internal_corner=corner, kind=kind, required_uv=required,
        ceiling_uv=ceiling, floor_uv=floor, initial_uv=initial, captured_mode_corners=corners,
        live_analog_voltage=False, installed_rpm_private_copy_identity_verified=False)


def independent_s1(rows):
    end = rows["end"][0]
    if end["error"] != "0" or end["complete"] != "1":
        return None
    expected = (0x1404, 0x1405, 0x1440, 0x1441, 0x1445, 0x1446, 0x1461, 0x1408)
    samples = []
    for sweep in (0, 1):
        fields = [r for r in rows["byte"] if int(r["pass"]) == sweep]
        assert [int(r["field"]) for r in fields] == list(range(8))
        assert [int(r["addr"], 0) for r in fields] == list(expected)
        assert all(r["valid"] == "1" and 0 <= int(r["raw"], 0) <= 255 for r in fields)
        samples.append([int(r["raw"], 0) for r in fields])
    assert len(rows["byte"]) == 16 and samples[0] == samples[1]
    kind, subtype, voltage_range, vset, mode, enable, step, status = samples[1]
    # Source-bound legacy FTS2.5 subtype09 only; do not invent other decoders.
    assert (kind, subtype) == (0x1c, 9) and voltage_range in (0, 1)
    base, increment = (80000, 5000) if voltage_range == 0 else (160000, 10000)
    value = base + increment * vset
    low, high = (350000, 1355000) if voltage_range == 0 else (1360000, 2200000)
    assert low <= value <= high and value == int(end["digital_uv"])
    ready = bool(enable & 0x80 and status & 0x81 == 0x81)
    assert ready == bool(int(end["ready"]))
    apid = int(end["apid"])
    assert 0 <= apid < 256 and (int(end["mapping"], 0) >> 8) & 0xfff == 0x114
    return dict(apid=apid, digital_uv=value, ready=ready, raw_byte_sweeps=samples,
        type=kind, subtype=subtype, mode_raw=mode, step_raw=step, status_raw=status,
        analog_voltage_measured=False)


def main():
    global RUN, OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02", "03", "04"), default="01")
    args = parser.parse_args()
    RUN = ROOT / ("evidence/gpu-metal-20261123/package/runs/gpu-g1-power-clock-" + args.attempt)
    OUT = ROOT / ("reports/hakios-ios16-gpu-g1-power-clock-" + args.attempt)
    status, result = [read(RUN / n) for n in ("usb-status.json", "result.json")]
    returned_path = RUN / "return-verification.json"
    returned = read(returned_path if returned_path.exists() else RUN / "return-window-missed.json")
    assert status["status"] not in ("waiting", "capturing")
    data = (RUN / "console.log").read_bytes()
    spec = importlib.util.spec_from_file_location("combined_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
    mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
    audit = mod.audit((RUN / "console.frames").read_bytes(), data, RUN / "capture-host.py")
    observations = terminal_observations(data)
    if not observations["rows"]["combined_end"]:
        report = emit_incomplete(RUN, OUT, data, status, result, returned, audit, observations)
        print(json.dumps(dict(passed=False, stage=None, lifecycle=False,
            observation_status=report["observation_status"], transport=audit["transport_verified"])))
        return 0
    rows, spans = parse(data)
    text = data.decode("utf-8", errors="replace")
    timer_and_transport = bool(audit["emitted_kernel_stream_through_timer_verified"] and
        len(re.findall(r"LITHIUM CPU TEMP phase=stop ", text)) == 4 and
        "LITHIUM LOG writer_contention_dropped_bytes=0" in text)
    lifecycle = bool(returned.get("passed") and timer_and_transport)
    end, clock, parent = [rows[k][0] for k in ("end", "clock", "parent")]
    mx = independent_plan(rows["mx_plan"], 0x100, 3)
    s1 = independent_plan(rows["s1_plan"], 0x101, 2)
    s1_digital = independent_s1(rows["s1"])
    rail = rows["s1"]["end"][0]
    physical_s1 = bool(s1 and s1_digital and s1_digital["ready"] and
        rail["digital_valid"] == "1" and s1_digital["digital_uv"] >= s1["required_uv"])
    stage = bool(physical_s1 and mx and mx["required_uv"] <= 850000 and end["error"] == "0" and
        end["vbif_valid"] == end["consumer_safe"] == end["original_verified"] == end["restore_ack"] == "1" and
        end["retain_power"] == end["uncertain"] == "0" and clock["readback_valid"] == clock["cleanup_complete"] == "1" and
        int(clock["cleanup_error"]) == int(clock["retained_dependencies"], 0) == 0 and
        parent["ready"] == parent["restored"] == "1" and parent["dirty"] == parent["uncertain"] == parent["retained"] == "0")
    power_clock_passed = stage
    trust_requested = "gpu_trust_zero=1" in result["boot_args"].split()
    trust_rows = [fields(m.group()) for m in re.finditer(r"LITHIUM GPU trust end [^\r\n]*", text)]
    trust_passed = False
    if trust_requested:
        assert len(trust_rows) == 1
        t = trust_rows[0]
        assert int(t["pa"], 0) == 0xb3d000 and t["cp_nonsecure_verified"] == "0"
        trust_passed = bool(t["error"] == t["call_rc"] == "0" and t["used"] == t["experimental"] == "1" and
            t["linux_enodev_path"] == "0" and t["pre_valid"] == t["post_valid"] == t["zero_readback"] == "1" and
            int(t["post"], 0) == 0 and t["native_reads"] == "2" and t["native_stores"] == "1" and
            t["maps"] == t["unmaps"] == "1" and t["uncertain"] == "0" and int(t["elapsed_us"]) < 2000)
        stage = bool(stage and trust_passed)
    else:
        assert not trust_rows
    passed = bool(stage and lifecycle)
    report = dict(schema="lithium-gpu-g1-power-clock-runtime-v1", passed=passed, hardware_stage_passed=stage,
        lifecycle_passed=lifecycle, analyzed_at=datetime.now(timezone.utc).isoformat(),
        normal_timer_and_transport_passed=timer_and_transport,
        power_clock_stage_passed=power_clock_passed, trust_requested=trust_requested,
        trust_register_experiment_passed=trust_passed, trust_rows=trust_rows, g2_cp_nonsecure_verified=False,
        accepted_at=result["accepted_at"], image_sha256=result["image_sha256"], boot_args=result["boot_args"],
        rows=rows, independent_mx_plan=mx, independent_s1_plan=s1, independent_s1_digital=s1_digital,
        same_boot_s1_voltage_bound_met=physical_s1,
        hardware_chip_id=None, vbif_version_raw=int(end["vbif_raw"], 0) if end["vbif_valid"] == "1" else None,
        gpu_clock_configuration_hz=int(clock["configured_hz"]) if clock["readback_valid"] == "1" else None,
        gpu_clock_measured_hz=None, cp_or_gpu_submission_verified=False, metal_acceleration_verified=False,
        usb=audit, receiver=status, return_verification=returned, original_console_modified=False,
        original_console_sha256=hashlib.sha256(data).hexdigest(), complete_temperature_spans_in_derived_view=spans)
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    OUT.with_suffix(".md").write_text(f"""# GPU G1组合电源/时钟{args.attempt}

单次RAM接受 `{result['accepted_at']}`，镜像 `{result['image_sha256']}`。
组合阶段通过 `{stage}`，正常定时与日志排空 `{timer_and_transport}`，同机Fastboot终点 `{returned.get('passed', False)}`。
返回核验记录：`{json.dumps(returned, ensure_ascii=False)}`。错过原核验窗口不补认自动返回。

同次S1启动计划：`{json.dumps(s1, ensure_ascii=False)}`。
实际S1数字设定/ready：`{json.dumps(rail, ensure_ascii=False)}`；电压上界比较 `{physical_s1}`。
独立字节解码：`{json.dumps(s1_digital, ensure_ascii=False)}`；不是模拟电压测量。
MX启动计划：`{json.dumps(mx, ensure_ascii=False)}`。启动表不等实时模拟电压或RPM私有副本身份证明。

终态：`{json.dumps(end, ensure_ascii=False)}`。
时钟与恢复：`{json.dumps(clock, ensure_ascii=False)}`。
父分支与恢复：`{json.dumps(parent, ensure_ascii=False)}`。

GPU时钟仅配置模型 `{report['gpu_clock_configuration_hz']}`Hz，非PMU/频率计实测。
VBIF_VERSION raw `{report['vbif_version_raw']}`只表示对应访问与版本观测；硬件完整chip ID未验证。
本轮不装载固件/不提交CP，不等于GPU绘制或Metal加速。CPU614.4/R20/旧冻结保持，默认关。
TRUST寄存器实验：请求`{trust_requested}`、通过`{trust_passed}`、记录`{json.dumps(trust_rows)}`。
该实验不是Linux ENODEV分支执行证明，不写回原安全状态，不等于CP已非安全运行。

USB `{audit['frames']}`帧/`{audit['raw_bytes']}`raw/`{audit['console_bytes']}`console独立逐字节重放；
接收器 `{status['status']}` / `{status.get('failure', '')}` 原样保留，源字节未修改。
""", encoding="utf-8")
    print(json.dumps(dict(passed=passed, stage=stage, lifecycle=lifecycle, error=end["error"], vbif=report["vbif_version_raw"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
