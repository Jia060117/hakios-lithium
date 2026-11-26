#!/usr/bin/env python3
"""Analyze one saved R40 attempt; never access a phone, build or change a runner.

Inputs are exclusively files inside reports/local/r40-attempts/ATTEMPT.
Writes r40-analysis.json there and reports/hakios-ios16-device-r40-ATTEMPT.md/json.
A growing capture is a pending snapshot, not a completed hardware acceptance.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import statistics
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
ATTEMPTS = ROOT / "reports/local/r40-attempts"
REPORTS = ROOT / "reports"
RECEIVER_SHA = "d5a9aecdd3c5ac728b233628d9ae810a598a35ba05d9068d0db3ed70fde3791c"
CORES = {"0x0": 4, "0x1": 6, "0x100": 9, "0x101": 11}
COUNTERS = ("expected", "queued", "bbd_copyout", "hid_dispatch_returned",
            "cleanup", "cleanup_evicted", "queue_depth", "swaps", "sb_commits", "max_read_us")
PHASES = {"wait", "warmup", "busy", "idle", "done", "abort", "disabled", "stop"}
FULL_FRAME_BAND = 1
FRAMEBUFFER_BAND_SOURCE = (
    "ports/hakios-lithium/patches/0008-lithium-thread-wait-profile.patch:177-180; "
    "R40 main/expanded-before/iokit/Kernel/MiniIOMobileFramebuffer.cpp:1288-1291")
spec = importlib.util.spec_from_file_location("r40_saved_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
auditor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(auditor)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fields(line):
    return dict(re.findall(r"([A-Za-z_]\w*)=([^\s]+)", line))


def number(value):
    try:
        return int(value, 16 if value.lower().startswith(("0x", "-0x")) else 10)
    except (ValueError, TypeError, AttributeError):
        return None


def stats(values):
    return dict(samples=len(values), min=min(values) if values else None,
                max=max(values) if values else None,
                mean=round(statistics.mean(values), 3) if values else None,
                median=statistics.median(values) if values else None)


def framebuffer_by_activity(records):
    summary = {}
    for phase in ("all", "busy", "idle"):
        chosen = [row for row in records if phase == "all" or row["activity_phase"] == phase]
        summary[phase] = {key: stats([number(r["fields"][key]) for r in chosen
            if number(r["fields"].get(key)) is not None])
            for key in ("cpu_us", "wall_us", "noncpu_us")}
    return summary


def framebuffer_summary(records):
    groups = {}
    for row in records:
        band = number(row["fields"].get("band"))
        key = str(band) if band is not None else "unknown"
        groups.setdefault(key, []).append(row)
    by_band = {}
    for key, chosen in groups.items():
        band = number(key)
        scope = ("setup_overhead" if band == 0 else
                 "full_draw_job_rows" if band == FULL_FRAME_BAND else
                 "draw_rows_partition" if band is not None and 2 <= band <= 9 else
                 "unknown_band_scope")
        by_band[key] = dict(band=band, scope=scope, records=len(chosen),
                            by_activity=framebuffer_by_activity(chosen))
    # Keep the existing primary by_activity interface, now strictly band=1.
    # Missing/unknown bands never fall back to mixed statistics or partition sums.
    full_frame = [r for r in records if number(r["fields"].get("band")) == FULL_FRAME_BAND]
    return dict(available=bool(records), records=records, by_band=by_band,
        full_frame_available=bool(full_frame), primary_statistics_band=FULL_FRAME_BAND,
        by_activity=framebuffer_by_activity(full_frame),
        band_semantics_source=FRAMEBUFFER_BAND_SOURCE,
        scope="band=0 setup; band=1 complete draw job row extent; band=2..9 individual row partitions. "
              "Primary statistics use band=1 only; partitions are neither mixed nor summed. "
              "CPU and wall are separate; wall includes possible scheduling time; no physical FPS inferred")


def read_saved(run):
    files, problems, pins = {}, [], {}
    for name in ("result.json", "usb-status.json", "commands.json", "return-verification.json",
                 "console.log", "console.frames", "capture-host.py"):
        path = run / name
        if not path.is_file():
            files[name] = None
            continue
        data = path.read_bytes()
        pins[name] = dict(bytes=len(data), sha256=digest(data))
        try:
            files[name] = json.loads(data) if name.endswith(".json") else data
        except (ValueError, UnicodeError) as exc:
            files[name] = None
            problems.append(f"{name}: incomplete/invalid saved JSON: {exc}")
    return files, problems, pins


def transport_audit(run, files):
    raw, console, capture = (files[name] for name in ("console.frames", "console.log", "capture-host.py"))
    if raw is None or console is None or capture is None:
        return dict(available=False, transport_verified=False,
                    emitted_kernel_stream_through_timer_verified=False,
                    reason="saved console/frames/frozen receiver not all present")
    if digest(capture) != RECEIVER_SHA:
        return dict(available=False, transport_verified=False,
                    emitted_kernel_stream_through_timer_verified=False,
                    reason="saved receiver SHA differs; do not import unknown capture code")
    try:
        return dict(available=True, **auditor.audit(raw, console, run / "capture-host.py"))
    except Exception as exc:
        return dict(available=True, transport_verified=False,
                    emitted_kernel_stream_through_timer_verified=False, audit_error=str(exc))


def endpoint_evidence(files, state, attempt):
    receipt = files["return-verification.json"]
    rows = files["commands.json"] or []
    result = dict(present=receipt is not None, record=receipt,
                  saved_command_endpoint_verified=False, fastboot_endpoint_verified=False,
                  automatic_fastboot_verified=False)
    if not isinstance(receipt, dict):
        return result
    post = [r for r in rows if r.get("phase") == "post_return"]
    target = state.get("target", {})
    serial, product = target.get("serial"), target.get("product")
    elapsed = receipt.get("elapsed_seconds")
    device_rows, observed = [], None
    if post:
        text = post[0].get("stdout", "") + "\n" + post[0].get("stderr", "")
        device_rows = [line.split() for line in text.splitlines() if line.strip()]
    if len(post) == 2:
        text = post[1].get("stdout", "") + "\n" + post[1].get("stderr", "")
        matches = re.findall(r"(?m)^product:[ \t]*([^\r\n]+)", text)
        observed = matches[0].strip() if len(matches) == 1 else None
    command_ok = bool(serial and product and len(post) == 2 and
        all(r.get("returncode") == 0 and not r.get("exception") and r.get("retry") is False
            for r in post) and
        post[0].get("argv", [])[1:] == ["devices"] and
        post[1].get("argv", [])[1:] == ["-s", serial, "getvar", "product"] and
        device_rows == [[serial, "fastboot"]] and observed == product)
    receipt_ok = bool(receipt.get("attempt") == attempt and receipt.get("passed") is True and
        receipt.get("fastboot_endpoint_verified") is True and
        receipt.get("devices_query_count") == receipt.get("product_query_count") == 1 and
        receipt.get("device_rows") == [[serial, "fastboot"]] and
        receipt.get("observed_product") == product and
        isinstance(elapsed, (int, float)) and 660 <= elapsed <= 960)
    result.update(saved_post_return_commands=post, saved_command_endpoint_verified=command_ok,
                  receipt_consistent=receipt_ok,
                  fastboot_endpoint_verified=bool(command_ok and receipt_ok))
    return result


def analyze(run, attempt=None):
    attempt = attempt or run.name
    files, warnings, pins = read_saved(run)
    state, usb = files["result.json"] or {}, files["usb-status.json"] or {}
    if state.get("attempt", attempt) != attempt:
        raise ValueError("saved result belongs to another attempt")
    data = files["console.log"] or b""
    lines = data.decode("utf-8", errors="replace").splitlines()
    partial_tail = bool(data and not data.endswith(b"\n"))
    if partial_tail:
        lines = lines[:-1]  # Never treat a concurrently written partial field as a complete record.
    transport = transport_audit(run, files)
    events, samples, display, ufs, fastboot, sb_samples, drops = [], [], [], [], [], [], []
    activity, stops, panic_lines = "before_ui", [], []
    for lineno, line in enumerate(lines, 1):
        f = fields(line)
        if "LITHIUM UI CYCLE " in line:
            phase = f.get("phase")
            if phase in PHASES:
                activity = phase
            events.append(dict(line=lineno, activity_phase=activity, fields=f))
        if "LITHIUM CPU TEMP " in line and ("affinity" in f or "sensor_affinity" in f):
            affinity = f.get("affinity", f.get("sensor_affinity"))
            try:
                affinity = hex(int(affinity, 16))
            except (TypeError, ValueError):
                pass
            raw_value = f.get("raw", "")
            try:
                raw = int(raw_value, 16)
            except ValueError:
                raw = None
            mc = number(f.get("native_mC", f.get("native_mc")))
            code = (raw & 0xfff) if raw is not None else None
            if code is not None and code >= 2048:
                code -= 4096
            channel_ok = affinity in CORES and number(f.get("id", f.get("client"))) == CORES[affinity]
            raw_ok = bool(raw is not None and raw != 0xffffffff and raw & (1 << 21)
                          and code * 100 == mc)
            valid = f.get("valid") == "1" and raw_ok and channel_ok
            sample = dict(line=lineno, affinity=affinity, sensor_id=number(f.get("id", f.get("client"))),
                phase=f.get("phase"), activity_phase="prelaunch" if f.get("source") == "uefi" else activity,
                reported_valid=f.get("valid") == "1", valid=valid, raw=raw_value,
                native_mC=mc if valid else None, reported_native_mC=mc,
                raw_decode_matches=raw_ok, channel_matches=channel_ok, fields=f)
            samples.append(sample)
            if f.get("valid") == "1" and not valid:
                warnings.append(f"line {lineno}: reported-valid TSENS sample fails raw/channel verification")
            if f.get("phase") == "stop":
                stops.append(sample)
        if "LITHIUM DISPLAY " in line and ("wall_us" in f or "cpu_us" in f):
            display.append(dict(line=lineno, activity_phase=activity, fields=f))
        if "LITHIUM SB SAMPLE " in line:
            sb_samples.append(dict(line=lineno, activity_phase=activity, fields=f))
        if "LITHIUM UFS RETURN " in line or "LITHIUM HFS RETURN " in line:
            ufs.append(dict(line=lineno, fields=f, error=number(f.get("error"))))
        if "LITHIUM FASTBOOT " in line:
            fastboot.append(dict(line=lineno, fields=f))
        if "LITHIUM LOG " in line and "writer_contention_dropped_bytes" in f:
            drops.append(dict(line=lineno, value=number(f["writer_contention_dropped_bytes"])))
        if re.search(r"panic\(cpu|\bPANIC CAUSE\b", line):
            panic_lines.append(dict(line=lineno, text=line))

    temperature = {}
    for phase in ("all", "busy", "idle"):
        temperature[phase] = {}
        for affinity, sensor in CORES.items():
            chosen = [s for s in samples if s["affinity"] == affinity and
                      (phase == "all" or s["activity_phase"] == phase)]
            values = [s["native_mC"] for s in chosen if s["valid"]]
            temperature[phase][affinity] = dict(sensor_id=sensor, native_mC=stats(values),
                                               invalid_samples=sum(not s["valid"] for s in chosen))
    counters = {}
    for key in COUNTERS:
        values = [number(e["fields"][key]) for e in events if key in e["fields"]]
        values = [v for v in values if v is not None]
        counters[key] = dict(last=values[-1] if values else None, max=max(values) if values else None,
                             observations=len(values),
                             decreased=any(b < a for a, b in zip(values, values[1:])))
    framebuffer = framebuffer_summary(display)
    phase_order = ("process_stop", "unmount", "seal", "finalized")
    # These fields attest what the saved kernel reported, not post-reboot durability.
    successful = {}
    for phase in phase_order:
        matching = [row for row in ufs if row["fields"].get("phase") == phase]
        successful[phase] = matching[-1] if matching else None
    a, b, c, d = (successful[phase] for phase in phase_order)
    ufs_complete = bool(a and b and c and d and
        a["line"] < b["line"] < c["line"] < d["line"] and
        b["error"] == c["error"] == d["error"] == 0 and d["fields"].get("ready") == "1" and
        number(c["fields"].get("request")) is not None and number(c["fields"]["request"]) > 0 and
        c["fields"]["request"] == d["fields"].get("request"))
    returned = endpoint_evidence(files, state, attempt)
    normal = bool(transport.get("emitted_kernel_stream_through_timer_verified"))
    source_drop = drops[-1]["value"] if drops else None
    stop_cores = sorted({s["affinity"] for s in stops if s["affinity"] in CORES})
    issues = []
    if panic_lines:
        issues.append("panic_seen")
    if any(row["error"] not in (None, 0) for row in ufs):
        issues.append("ufs_return_error")
    if source_drop is not None and source_drop > 0:
        issues.append("source_writer_contention_drop")
    aborts = [e for e in events if e["fields"].get("phase") in ("abort", "disabled")]
    if aborts:
        issues.append("ui_cycle_aborted_or_disabled")
    saved_active = usb.get("status") in ("waiting", "connecting", "capturing") and not usb.get("finished_at")
    elapsed = None
    try:
        accepted = datetime.fromisoformat(state["accepted_at"])
        if accepted.tzinfo is not None:
            elapsed = (datetime.now().astimezone() - accepted).total_seconds()
    except (KeyError, ValueError, TypeError):
        pass
    pending = not returned["present"]
    if pending:
        reason = ("boot_acceptance_not_proven" if not state.get("command_accepted") else
                  "normal_timer_seen_return_verification_pending" if normal else
                  "accepted_run_saved_capture_prefix" if saved_active else
                  "accepted_run_terminal_evidence_pending")
        status = "pending"
    else:
        reason = None
        status = "observations_complete" if returned["fastboot_endpoint_verified"] else "return_not_verified"
    if saved_active and (not transport.get("transport_verified") or partial_tail):
        warnings.append("capture last saved as active: raw/console snapshot tails may be temporarily out of sync")
    return dict(schema="hakios-r40-saved-analysis-v1", analysis_completed=True,
        analyzed_at=datetime.now().astimezone().isoformat(), attempt=attempt,
        status=status, pending=pending, pending_reason=reason, observed_issues=issues,
        accepted_at=state.get("accepted_at"), command_accepted=state.get("command_accepted", False),
        elapsed_since_acceptance_seconds=elapsed, image_sha256=state.get("image_sha256"),
        boot_args=state.get("boot_args"), target=state.get("target"), input_snapshots=pins,
        capture_last_saved_status=usb.get("status"), capture_record=usb,
        capture_last_saved_as_active=bool(saved_active), capture_currently_running_proven=False,
        complete_console_lines=len(lines), partial_console_tail_ignored=partial_tail,
        ui=dict(last_phase=events[-1]["fields"].get("phase") if events else None,
            last_activity_phase=activity, phase_counts=dict(Counter(e["fields"].get("phase") for e in events)),
            events=events, counters=counters, aborts=aborts,
            consumer_copyout_log_rows=sum(e["fields"].get("phase") == "consumer" for e in events),
            sb_observer_samples=sb_samples,
            counter_scope="cumulative snapshots, never summed; queue intent / read copyout / HID call return / after-CA observer are distinct",
            physical_response_verified=False, navigation_verified=False,
            physical_frame_count=None, hardware_ui_acceptance="pending_main_physical_review"),
        temperature=dict(by_activity=temperature, samples=samples,
            phase_assignment="saved console order between UI state transitions; not exact cross-thread timestamp alignment",
            units="native_mC", four_channels=[dict(affinity=a, sensor_id=s) for a, s in CORES.items()],
            thermal_stop_rows=len(stops), thermal_stop_affinities=stop_cores,
            four_channel_stop_seen=set(stop_cores) == set(CORES),
            calibration_verified=False, sample_freshness_verified=False),
        framebuffer=framebuffer,
        ufs_return=dict(events=ufs, phases_seen=dict(Counter(r["fields"].get("phase") for r in ufs)),
            errors=[r for r in ufs if r["error"] not in (None, 0)],
            kernel_reported_ordered_finalization_complete=ufs_complete,
            durable_storage_recovery_verified=False),
        normal_timer=dict(expiry_seen=b"--- loader_return_after expired ---" in data,
            normal_boundary_seen=b"LITHIUM USB: normal timer return boundary; draining before quiesce" in data,
            drain_records=transport.get("drain_records", []),
            saved_stream_through_timer_verified=normal, fastboot_log_records=fastboot),
        source_writer_contention_dropped_bytes=source_drop, source_drop_records=drops,
        source_zero_drop_reported=source_drop == 0, transport=transport,
        return_verification=returned, panic_seen=bool(panic_lines), panic_records=panic_lines,
        automatic_fastboot_verified=False, overall_hardware_acceptance="pending_main_review",
        measured_cpu_mhz=None, warnings=warnings, device_operations=0)


def markdown(report):
    text = (f"# R40 保存日志分析：{report['attempt']}\n\n"
        f"状态：**{report['status']}**。生成时间：{report['analyzed_at']}。\n\n"
        f"pending 原因：{report['pending_reason'] or '无待生成的返回记录'}。"
        f"采集器最后保存状态：{report['capture_last_saved_status']}；"
        "只据保存文件分析，未证明当前物理运行状态。\n\n"
        f"镜像 SHA256：`{report['image_sha256']}`。\n\n"
        "## UI 软件观测\n\n"
        f"最后阶段 `{report['ui']['last_phase']}`；"
        f"最后活动阶段 `{report['ui']['last_activity_phase']}`。\n\n"
        "| 计数 | 最后观测 | 最大观测 | 记录数 |\n|---|---:|---:|---:|\n")
    for key, value in report["ui"]["counters"].items():
        text += f"| {key} | {value['last']} | {value['max']} | {value['observations']} |\n"
    text += ("\nqueued 是入队意图；bbd_copyout 是用户态读出；hid_dispatch_returned 是调用返回；"
             "sb_commits 是 after-CA runloop 观测。累计计数不求和，均不能证明物理响应、物理帧或导航验收。\n\n"
             "## TSENS 四路忙闲温度\n\n"
             "按日志中 UI 阶段边界归类，并非精确跨线程时间对齐；单位为原生 °C，校准与新鲜度未验。\n\n"
             "| 阶段 | affinity / sensor | 有效数 | 无效数 | 最低 °C | 最高 °C | 中位 °C |\n"
             "|---|---|---:|---:|---:|---:|---:|\n")
    for phase in ("all", "busy", "idle"):
        for affinity, value in report["temperature"]["by_activity"][phase].items():
            s = value["native_mC"]
            def degrees(v):
                return "未取得" if v is None else f"{v / 1000:g}"
            text += (f"| {phase} | {affinity} / {value['sensor_id']} | {s['samples']} | "
                     f"{value['invalid_samples']} | {degrees(s['min'])} | "
                     f"{degrees(s['max'])} | {degrees(s['median'])} |\n")
    text += ("\n## Framebuffer 整帧绘制 CPU / wall（仅 band=1）\n\n"
             "源码中 band=0 为 setup 开销，band=1 为整个 draw job 的行范围，band=2..9 为各行分区；"
             "本次 2040 行日志的四分区为 band=2..5。主表只取 band=1，不混入 setup 或分区，"
             "也不把分区求和。缺少 band=1 时保留未取得；wall 可能包含调度等待，不推算物理 FPS。\n\n"
             f"band 定义来源：`{report['framebuffer']['band_semantics_source']}`。\n\n"
             "| 阶段 | CPU 样本 / 中位 µs | wall 样本 / 中位 µs |\n|---|---|---|\n")
    for phase, value in report["framebuffer"]["by_activity"].items():
        text += f"| {phase} | {value['cpu_us']['samples']} / {value['cpu_us']['median']} | {value['wall_us']['samples']} / {value['wall_us']['median']} |\n"
    text += ("\n分 band 统计（all 阶段；各 band 原始记录及 busy/idle 分组保留在 JSON）：\n\n"
             "| band | 范围 | 记录数 | CPU 中位 µs | wall 中位 µs |\n|---|---|---:|---:|---:|\n")
    for band, group in report["framebuffer"]["by_band"].items():
        value = group["by_activity"]["all"]
        text += f"| {band} | {group['scope']} | {group['records']} | {value['cpu_us']['median']} | {value['wall_us']['median']} |\n"
    text += ("\n## UFS 返回与终态\n\n"
             "| 行 | phase | error | ready | request |\n|---:|---|---|---|---|\n")
    for row in report["ufs_return"]["events"]:
        f = row["fields"]
        text += f"| {row['line']} | {f.get('phase')} | {row['error']} | {f.get('ready')} | {f.get('request')} |\n"
    text += (f"\n内核日志有序 finalization 完成：{report['ufs_return']['kernel_reported_ordered_finalization_complete']}；"
        f"正常 timer/drain 独立串流核验：{report['normal_timer']['saved_stream_through_timer_verified']}；"
        f"四路 stop：{report['temperature']['four_channel_stop_seen']}；"
        f"source dropped：{report['source_writer_contention_dropped_bytes']}（None 为缺证，非零丢失通过）。\n\n"
        f"USB 连续性：{report['transport'].get('transport_verified', False)}；"
        f"frames：{report['transport'].get('frames')}；"
        f"console bytes：{report['transport'].get('console_bytes')}。\n\n"
        f"已保存 devices/product 同机终点：{report['return_verification']['fastboot_endpoint_verified']}。"
        "不据 manifest、日志队列或终点单独标记自动返回全链、恢复耐久性或 UI 导航验收通过。\n\n"
        "返回核验原始记录：\n\n```json\n" +
        json.dumps(report["return_verification"]["record"], ensure_ascii=False, indent=2) + "\n```\n\n"
        f"观察问题：{', '.join(report['observed_issues']) or '当前前缀未观察到列举的问题'}。\n\n")
    for warning in report["warnings"]:
        text += "- " + warning + "\n"
    return text


def write_report(path, text):
    temp = path.with_name(path.name + ".tmp")
    temp.write_text(text, encoding="utf-8", newline="\n")
    temp.replace(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", required=True, help="saved unique R40 runner attempt identifier")
    args = parser.parse_args(argv)
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,79}", args.attempt) is None:
        parser.error("invalid --attempt identifier")
    run = ATTEMPTS / args.attempt
    if not run.is_dir():
        parser.error("saved attempt directory missing: " + str(run))
    try:
        report = analyze(run, args.attempt)
        encoded = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        standard = REPORTS / ("hakios-ios16-device-r40-" + args.attempt)
        write_report(run / "r40-analysis.json", encoded)
        write_report(standard.with_suffix(".json"), encoded)
        write_report(standard.with_suffix(".md"), markdown(report))
        print(json.dumps(dict(attempt=args.attempt, status=report["status"],
            pending=report["pending"], ui_phase=report["ui"]["last_activity_phase"],
            analysis=str(run / "r40-analysis.json"), report=str(standard.with_suffix(".md")),
            device_operations=0), ensure_ascii=False, indent=2))
        return 0  # pending is an observation, not an analyzer execution failure.
    except (OSError, ValueError, TypeError, KeyError) as exc:
        parser.exit(1, f"R40 saved analysis stopped: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
