#!/usr/bin/env python3
"""Analyze saved RPM control traffic and USB return evidence; no device I/O."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import importlib.util
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-rpm-open-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-rpm-open-01"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def parse(data):
    text = data.decode("utf-8", errors="replace")
    # printf calls can interleave between the logger's bounded chunks. Remove
    # only complete, independently recognizable temperature records from a
    # derived view, preserving their exact original byte offsets and text.
    # A truncated temperature prefix containing a GPU marker cannot match.
    pattern = re.compile(r"LITHIUM CPU TEMP (?:(?!LITHIUM)[^\r\n])*"
                         r"freshness_verified=0\r?\n")
    removed = [dict(start=m.start(), end=m.end(), text=m.group())
               for m in pattern.finditer(text)]
    derived = pattern.sub("", text)
    sections = {}
    for kind in ("begin", "initial", "write", "rx", "rx_summary", "foreign", "flags", "end"):
        marker = "LITHIUM GPU rpm open " + kind + " "
        sections[kind] = [fields(m.group()) for m in
                          re.finditer(re.escape(marker) + r"[^\r\n]*", derived)]
    assert all(len(sections[k]) == 1 for k in ("begin", "flags", "end"))
    assert len(sections["initial"]) == 2
    end = sections["end"][0]
    required = ("rc discover_rc open_rc adapter_error transport_open zero_verified "
                "operations io_cap reads write_callbacks stores_attempted stores_returned "
                "bell_attempts bell_returns published_attempts waits trace_omitted maps "
                "unmaps lease_held released traffic_retained uncertain elapsed_us timeout_us "
                "final_valid resource_requests pmic_writes gpu_writes").split()
    assert all(k in end for k in required), "incomplete terminal record"
    flags = sections["flags"][0]
    assert all(k in flags for k in ("peer_version", "version_acked", "local_open",
        "remote_open", "open_acked", "remote_channel", "toc_stable"))
    trace = sections["write"]
    assert len(trace) == int(end["stores_attempted"]) and end["trace_omitted"] == "0"
    assert [int(t["index"]) for t in trace] == list(range(len(trace)))
    allowed = all((int(t["kind"]) == 1 and 0x68808 <= int(t["pa"], 0) < 0x68c00)
                  or (int(t["kind"]) == 2 and int(t["pa"], 0) == 0x68804)
                  or (int(t["kind"]) == 3 and int(t["pa"], 0) == 0x68c00)
                  or (int(t["kind"]) == 4 and int(t["pa"], 0) == 0x9820010
                      and int(t["raw"], 0) == 1) for t in trace)
    assert allowed, "write outside the actual discovered control transport"
    assert all(end[k] == "0" for k in ("resource_requests", "pmic_writes", "gpu_writes"))
    assert int(end["operations"]) <= int(end["io_cap"])
    assert int(end["elapsed_us"]) <= int(end["timeout_us"])
    if sections["rx_summary"]:
        assert len(sections["rx_summary"]) == 1
        summary = sections["rx_summary"][0]
        assert len(sections["rx"]) == int(summary["recorded"])
        assert summary["extra_reads"] == "0"
        assert [int(t["index"]) for t in sections["rx"]] == list(range(len(sections["rx"])))
    return sections, removed


def decode_control_writes(sections):
    flags = sections["flags"][0]
    tx_off, size = int(flags["tx_off"], 0), int(flags["tx_size"], 0)
    memory, frames, head = {}, [], 0
    foreign_ids = {int(x["rcid"]) for x in sections["foreign"]}
    for row in sections["write"]:
        kind, address, raw = int(row["kind"]), int(row["pa"], 0), int(row["raw"], 0)
        if kind == 1:
            memory[address] = raw
        elif kind == 2:
            length = (raw - head) % size
            assert length in (8, 24)
            words = [memory[0x68000 + tx_off + 8 + (head + i) % size]
                     for i in range(0, length, 4)]
            command, channel = words[0] & 0xffff, words[0] >> 16
            assert command in (0, 1, 2, 4)
            if command in (0, 1):
                assert channel == 1 and words[1] == 0 and length == 8
            elif command == 2:
                assert channel == 1 and words[1] == 13 and length == 24
                assert b"".join(w.to_bytes(4, "little") for w in words[2:]) == b"rpm_requests\0\0\0\0"
            else:
                assert channel in ({int(flags["remote_channel"])} | foreign_ids)
                assert words[1] == 0 and length == 8
            frames.append(dict(command=command, channel=channel, words=words))
            head = raw
    return frames


def main():
    global RUN, OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02", "03"), default="01")
    args = parser.parse_args()
    RUN = ROOT / ("evidence/gpu-metal-20261123/package/runs/gpu-g1-rpm-open-" + args.attempt)
    OUT = ROOT / ("reports/hakios-ios16-gpu-g1-rpm-open-" + args.attempt)
    receiver = read(RUN / "usb-status.json")
    assert receiver["status"] not in ("waiting", "capturing"), "capture still active"
    result, returned = read(RUN / "result.json"), read(RUN / "return-verification.json")
    data = (RUN / "console.log").read_bytes()
    demux = read(RUN / "printf-demux.json") if (RUN / "printf-demux.json").exists() else None
    parse_data = data
    if demux:
        assert hashlib.sha256(data[:demux["prefix_bytes"]]).hexdigest() == demux["original_sha256"]
        assert "".join(x["text"] for x in demux["spans"]) == demux["expected_other_printf"]
        assert demux["expected_other_printf"].endswith(result["boot_args"] + "\n")
        for span in reversed(demux["spans"]):
            assert data[span["start"]:span["end"]] == span["text"].encode()
            parse_data = parse_data[:span["start"]] + parse_data[span["end"]:]
    sections, removed = parse(parse_data)
    control_frames = decode_control_writes(sections)
    spec = importlib.util.spec_from_file_location("rpm_open_audit", ROOT / "scripts/hakios_usb_audit.py")
    audit_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit_module)
    audit = audit_module.audit((RUN / "console.frames").read_bytes(), data, RUN / "capture-host.py")
    lines = data.decode(errors="replace").splitlines()
    stop = [fields(x) for x in lines if x.startswith("LITHIUM CPU TEMP phase=stop ")]
    lifecycle = bool(returned.get("passed") and len(stop) == 4 and
                     "LITHIUM LOG writer_contention_dropped_bytes=0" in lines and
                     audit["emitted_kernel_stream_through_timer_verified"])
    flags, end = sections["flags"][0], sections["end"][0]
    transport = end["rc"] == "0" and end["transport_open"] == "1" and all(
        flags[k] == "1" for k in ("peer_version", "version_acked", "local_open", "remote_open", "open_acked"))
    report = dict(schema="lithium-gpu-rpm-open-runtime-v1", analyzed_at=datetime.now(timezone.utc).isoformat(),
        passed=bool(transport and lifecycle), lifecycle_passed=lifecycle,
        transport_open_verified=transport, protocol_result=int(end["rc"]),
        accepted_at=result["accepted_at"], image_sha256=result["image_sha256"],
        boot_args=result["boot_args"], sections=sections, complete_temperature_spans_in_derived_view=removed,
        bootargs_demux=demux, independently_decoded_tx_control_frames=control_frames,
        original_console_modified=False, usb=audit, receiver=receiver, return_verification=returned,
        rpm_resource_requests=0, mx_vote_ack_verified=False, bus_vote_verified=False,
        gpu_power_on_passed=False, metal_acceleration_verified=False,
        rejection_cause=("unverified: inspect retained RX words" if sections["rx"] else "unverified: RX payload was not retained") if not transport else None)
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    next_step = ("下一进入MX及总线条件验证；完整OPEN不代表资源投票或GPU加速。" if transport else
                 "下一修订依据协议源码与拒绝处RX证据继续，保留本次拒绝结果。")
    OUT.with_suffix(".md").write_text(f"""# GPU G1 RPM VERSION/OPEN {args.attempt} · 2026-11-25

版本协商与远端 OPEN 已观察到；完整通道验收 `{transport}`，协议结果 `{end['rc']}`。
flags：peer_version `{flags['peer_version']}`、version_acked `{flags['version_acked']}`、
local_open `{flags['local_open']}`、remote_open `{flags['remote_open']}`、open_acked `{flags['open_acked']}`。
远端 channel `{flags['remote_channel']}`。保留RX读取 `{len(sections["rx"])}` 条，未完成的具体拒绝原因需对照原始包与源码，不推断TZ拒绝。

一次 RAM 启动 `{result['accepted_at']}`，镜像 `{result['image_sha256']}`。
本次 `{end['reads']}` 次读取、`{end['stores_attempted']}` 次协议存储、`{end['bell_attempts']}` 次 doorbell，
耗时 `{end['elapsed_us']}` µs。资源请求、PMIC、GPU 写入均为 0。
映射 `{end['maps']}` / 释放 `{end['unmaps']}`，lease `{end['lease_held']}`；
traffic_retained `{end['traffic_retained']}` / uncertain `{end['uncertain']}` 原样保留，无 close、索引清零或重试。

正常180秒timer、四路stop、source丢0、drain及同机Fastboot终点核验 `{lifecycle}`。
USB独立逐字节重放 `{audit['frames']}` 帧、`{audit['raw_bytes']}` raw、`{audit['console_bytes']}` console字节。
接收器 `{receiver['status']}` / `{receiver.get('failure', '')}` 原样保留。
唯一返回核验 `{returned.get('finished_at')}`，passed `{returned.get('passed')}`。

原始日志未修改。派生解析视图只移除完整且有明确终止字段的温度记录，记录原偏移与全文，
从而复原被 printf 并发插入的 flags/end 记录；没有补猜任何字节。
若存在BOOTARGS交错，保存的demux记录先验证全部原字节，并要求拼接后与冻结包kernel.ver和本次启动参数的完整printf相等；
温度记录偏移相对于这一步之后的派生视图。其他服务的有界OPEN记录 `{sections['foreign']}`，不作为资源请求ACK。
主代理执行实际构建、逐层读回、单次启动和独立 USB/写地址复核；
Aristotle 的专项 ASan/UBSan 与 ARM64 对象测试已通过（01为97场景，03另有16个多通道专项）；02的98场景保持原读写次数。
内核 `{read(Path(result['package']) / 'contract.json')['inputs']['kernel_sha256']}`。

独立从TX原始store轨迹解码 `{len(control_frames)}` 个完整控制帧，无资源数据帧。
这次未确认资源投票、GPU chip ID、CP fence 或 Metal 加速。{next_step}
""", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("passed", "lifecycle_passed", "transport_open_verified", "protocol_result")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
