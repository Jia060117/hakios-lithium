#!/usr/bin/env python3
"""Validate saved RPM read evidence and the completed return; no device I/O."""
from datetime import datetime, timezone
from pathlib import Path
import importlib.util
import json
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-rpm-read-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-rpm-read-01"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def main():
    receiver = read(RUN / "usb-status.json")
    assert receiver["status"] not in ("waiting", "capturing"), "capture still active"
    result = read(RUN / "result.json")
    returned = read(RUN / "return-verification.json")
    data = (RUN / "console.log").read_bytes()
    spec = importlib.util.spec_from_file_location("rpm_saved_audit", ROOT / "scripts/hakios_usb_audit.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    audit = module.audit((RUN / "console.frames").read_bytes(), data, RUN / "capture-host.py")
    lines = data.decode(errors="replace").splitlines()
    sections = {}
    toc, indices, temperatures, interleaved = [], [], [], []
    for n, line in enumerate(lines, 1):
        for name in ("begin", "layout", "end"):
            if line.startswith("LITHIUM GPU rpm " + name + " "):
                sections.setdefault(name, []).append(dict(line=n, **fields(line)))
        marker = "LITHIUM GPU rpm toc "
        position = line.find(marker)
        if position >= 0:
            # The logger preserves bytes but its bounded writes can interleave
            # two printf prefixes. Decode the complete TOC record from its
            # own marker, retaining the original line and prefix as evidence.
            record = line[position:]
            assert re.fullmatch(
                r"LITHIUM GPU rpm toc pass=\d+ word=\d+ offset=0x[0-9a-f]+ raw=0x[0-9a-f]+ attempted=[01] loaded=[01] valid=[01]",
                record), "incomplete TOC record; do not infer missing fields"
            toc.append(dict(line=n, **fields(record)))
            if position:
                interleaved.append({"line": n, "prefix": line[:position], "record": record})
        if line.startswith("LITHIUM GPU rpm index "):
            indices.append(dict(line=n, **fields(line)))
        if line.startswith("LITHIUM CPU TEMP "):
            temperatures.append(fields(line))
    assert all(len(sections.get(k, [])) == 1 for k in ("begin", "layout", "end"))
    layout, end = sections["layout"][0], sections["end"][0]
    pairs = {(int(t["pass"]), int(t["word"])): t for t in toc}
    capture_ok = len(pairs) == len(toc) == 128
    for i in range(64):
        a, b = pairs.get((0, i), {}), pairs.get((1, i), {})
        capture_ok = bool(capture_ok and a and b and
            a["valid"] == b["valid"] == a["loaded"] == b["loaded"] == "1" and
            int(a["offset"], 0) == int(b["offset"], 0) == 0x5f00 + 4 * i and
            a["raw"] == b["raw"])
    zero_writes = all(end[k] == "0" for k in ("write_attempts", "core_writes", "core_kicks"))
    index_ok = len(indices) == 8 and all(
        t["valid"] == t["loaded"] == t["in_range"] == t["stable"] == "1" for t in indices)
    layout_ok = layout["valid"] == "1" and all(
        end[k] == "0" for k in ("rc", "discover_rc", "adapter_error")) and index_ok
    lifecycle_ok = bool(returned.get("passed") and audit["emitted_kernel_stream_through_timer_verified"]
        and "LITHIUM LOG writer_contention_dropped_bytes=0" in lines
        and sum(t.get("phase") == "stop" for t in temperatures) == 4)
    passed = bool(capture_ok and zero_writes and layout_ok and lifecycle_ok)
    report = dict(
        schema="lithium-gpu-rpm-read-runtime-v1", analyzed_at=datetime.now(timezone.utc).isoformat(),
        passed=passed, capture_passed=capture_ok, valid_layout_and_indices=layout_ok,
        lifecycle_passed=lifecycle_ok, zero_writes=zero_writes,
        accepted_at=result["accepted_at"], image_sha256=result["image_sha256"],
        boot_args=result["boot_args"], begin=sections["begin"][0], layout=layout, end=end,
        toc=toc, indices=indices, interleaved_toc_prefixes=interleaved,
        usb=audit, receiver=receiver, return_verification=returned,
        all_four_indices_zero=index_ok and all(int(t["raw"], 0) == 0 for t in indices),
        ownership_proven=False, rpm_open_ack_verified=False, mx_vote_ack_verified=False,
        bus_vote_verified=False, gpu_power_on_passed=False, metal_acceleration_verified=False,
    )
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    OUT.with_suffix(".md").write_text(f"""# GPU G1 RPM只读通道采集01 · 2026-11-25

实机采集{'通过' if passed else '未完整通过'}。CPU614.4配置模型保持，隔离RAM；没有FIFO/doorbell、PMIC、GPU或SMMU写入。

启动时间 `{result['accepted_at']}`，镜像SHA256 `{result['image_sha256']}`。
消息RAM `0x68000 + 0x6000`，TOC `+0x5f00`，64word双遍全部有效一致。
TX offset=`{layout['tx_off']}`、RX offset=`{layout['rx_off']}`，
数据长度 `{layout['tx_size']}` / `{layout['rx_size']}`，四个继承索引两遍全零：`{report['all_four_indices_zero']}`。
本次 `{end['reads']}`次读取、`{end['elapsed_us']}`µs，一次完整VM页映射已释放；写入回调0、core写0、doorbell0。

这是存在有效TOC/FIFO且当前为空的证据，不代表通道所有权、版本协商、OPEN ACK、MX corner或bus投票已获证实。

## 退出和传输

180秒正常timer、四路stop/source丢0、drain及独立重放：`{lifecycle_ok}`。
保存 `{audit['frames']}`帧 / `{audit['raw_bytes']}`raw / `{audit['console_bytes']}`console字节。
接收器终态 `{receiver['status']}` / `{receiver.get('failure', '')}` 保留；
一次Fastboot终点核验 `{returned['passed']}`，时间 `{returned.get('finished_at')}`。

## 实现、构建和复核

Aristotle实现`gpu/rpm/rpm-read-adapter.inc`及169场景actual-adapter专项，ASan/UBSan与AArch64对象；主代理核对源码与实际编译树字节一致。
主代理接入默认关闭`gpu_rpm_diag`、15参数专项、实际完整HakiOS链接、打包/逐层读回和全部设备操作。
实际内核SHA256 `32e10b383ec1e68932714450c196e54d73c414306af4f3da894e86e11afb9776`。
主代理独立核对128个TOC word、8个index、地址范围和USB原始帧，不把mock当硬件ACK。
{len(interleaved)}条TOC记录前缀与CPU温度printf发生交错，完整TOC字段均保留；分析器从独立标记严格解析，
原始字节和交错前缀保存在证据中，未修改原始日志或推测缺失字段。
证据与可回退补丁位于`evidence/gpu-metal-20261125/rpm-read/`。

下一步在同一RAM约束下尝试RPM版本协商和OPEN，不复用这次历史索引作为新运行的准入证明。
MX/bus、GPU时钟/GDSC/chipID、PAS13/SMMU/CP/Metal仍待实际执行。

```text
{result['boot_args']}
```
""", encoding="utf-8")
    print(json.dumps({k: report[k] for k in (
        "passed", "capture_passed", "valid_layout_and_indices", "all_four_indices_zero", "zero_writes")}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
