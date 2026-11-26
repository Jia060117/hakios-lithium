#!/usr/bin/env python3
"""Decode only the saved two-pass SMEM473 capture and independently replay USB."""
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-mx-plan-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-mx-plan-01"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(text):
    return dict(re.findall(r"(\w+)=([^\s]+)", text))


def parse(data):
    temp = re.compile(rb"LITHIUM CPU TEMP (?:(?!LITHIUM)[^\r\n])*freshness_verified=0\r?\n")
    spans = [dict(start=m.start(), end=m.end(), text=m.group().decode()) for m in temp.finditer(data)]
    text = temp.sub(b"", data).decode("utf-8", errors="replace")
    rows = {k: [fields(m.group()) for m in re.finditer(
        re.escape("LITHIUM GPU mx plan " + k + " ") + r"[^\r\n]*", text)]
        for k in ("begin", "toc", "word", "decode", "end")}
    assert all(len(rows[k]) == 1 for k in ("begin", "decode", "end"))
    begin, end = rows["begin"][0], rows["end"][0]
    assert begin["logical"] == "0" and int(begin["affinity"], 0) == 1
    assert int(begin["smem_base"], 0) == 0x86000000 and begin["item"] == "473"
    assert int(begin["bytes"], 0) == 0x200000 and begin["passes"] == "2"
    assert begin["load_cap"] == "66" and begin["timeout_us"] == "10000" and begin["writes"] == "0"
    assert len(rows["toc"]) == 10 and int(end["loads"]) <= 66 and end["load_cap"] == "66"
    assert len(rows["word"]) == 2 * int(end["words"]) <= 56
    assert end["writes"] == end["polls"] == end["retries"] == end["votes"] == "0"
    assert end["gpu_aperture"] == end["smmu"] == "unread"
    assert end["rail_ready_observed"] == end["full_gpu_supply_certified"] == "0"
    descriptor_pa = (0x860000c0, 0x86001e60, 0x86001e64, 0x86001e68, 0x86001e6c)
    for p in (0, 1):
        toc = [r for r in rows["toc"] if int(r["pass"]) == p]
        words = [r for r in rows["word"] if int(r["pass"]) == p]
        assert [int(r["index"]) for r in toc] == list(range(5))
        assert [int(r["pa"], 0) for r in toc] == list(descriptor_pa)
        assert [int(r["index"]) for r in words] == list(range(int(end["words"])))
        assert len({int(r["offset"], 0) for r in words}) == len(words)
    assert sum(int(r["loaded"]) for r in rows["toc"] + rows["word"]) == int(end["loads"])
    return rows, spans


def main():
    receiver = read(RUN / "usb-status.json")
    assert receiver["status"] not in ("waiting", "capturing")
    result, returned = read(RUN / "result.json"), read(RUN / "return-verification.json")
    data = (RUN / "console.log").read_bytes()
    rows, spans = parse(data)
    end, decoded = rows["end"][0], rows["decode"][0]
    audit_module = module("mx_plan_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
    audit = audit_module.audit((RUN / "console.frames").read_bytes(), data, RUN / "capture-host.py")
    text = data.decode("utf-8", errors="replace")
    stop = re.findall(r"LITHIUM CPU TEMP phase=stop [^\r\n]*", text)
    lifecycle = bool(returned.get("passed") and len(stop) == 4 and
        "LITHIUM LOG writer_contention_dropped_bytes=0" in text and audit["emitted_kernel_stream_through_timer_verified"])
    valid_reads = all(r["loaded"] == r["valid"] == "1" for r in rows["toc"] + rows["word"])
    capture_passed = bool(lifecycle and valid_reads and end["sweeps"] == "2" and end["stable"] == "1" and
        all(int(end[k], 0) == 0 for k in ("io_error", "adapter_error", "global_overrun", "rejected")) and
        end["maps"] == end["unmaps"] and int(end["maps"]) <= 3 and int(end["elapsed_us"]) < 10000)
    sparse = dict(toc_sweeps=[[int(r["raw"], 0) for r in rows["toc"] if int(r["pass"]) == p] for p in (0, 1)],
        item_word_sweeps=[[[int(r["offset"], 0), int(r["raw"], 0)] for r in rows["word"] if int(r["pass"]) == p] for p in (0, 1)])
    sparse_path = RUN / "smem473-sparse.json"
    sparse_path.write_text(json.dumps(sparse, indent=2) + "\n")
    independent = None
    if end["plan_valid"] == "1":
        assert capture_passed and decoded["decoded_valid"] == decoded["required_valid"] == "1"
        decoder = module("stock_rpm_v1_decoder", ROOT / "evidence/gpu-metal-20261126/mx-after-cx/extract-rpm-plan.py")
        independent = decoder.decode_sparse(sparse_path)
        assert independent["required_upper_bound_uV"] == int(decoded["required_uv"])
        assert independent["inherited_850_meets_decoded_voltage_bound"] == bool(int(end["inherited850_bound"]))
    else:
        assert decoded["required_valid"] == "0"
    passed = bool(capture_passed and independent is not None)
    report = dict(schema="lithium-gpu-mx-plan-runtime-v1", passed=passed, read_capture_passed=capture_passed,
        analyzed_at=datetime.now(timezone.utc).isoformat(), accepted_at=result["accepted_at"],
        image_sha256=result["image_sha256"], boot_args=result["boot_args"], rows=rows,
        independent_stock_rpm_v1_decode=independent, lifecycle_passed=lifecycle, usb=audit,
        original_console_sha256=hashlib.sha256(data).hexdigest(), original_console_modified=False,
        complete_temperature_spans_in_derived_view=spans, receiver=receiver, return_verification=returned,
        full_gpu_supply_certified=False, installed_firmware_identity_verified=False,
        voltage_or_rpm_writes=0, gpu_power_on_passed=False, metal_acceleration_verified=False)
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n")
    required = independent["required_upper_bound_uV"] if independent else "未验证"
    OUT.with_suffix(".md").write_text(f"""# GPU G1 MX启动配置只读01 · {result['accepted_at'][:10]}

一次隔离RAM启动`{result['accepted_at']}`，镜像`{result['image_sha256']}`。
只读采集通过`{capture_passed}`，目标档位解析通过`{passed}`，生命周期`{lifecycle}`。

TOC与选中稀疏数据双遍保存，实际{end['loads']}次read32、零写/零投票/零轮询/零重试，
{end['maps']}映射/{end['unmaps']}释放、{end['elapsed_us']}µs。SMEM仅限`86000000..86200000`。
item473 version1、rail0x100/internal corner3需求上界：`{required}`uV；
内核继承850mV边界判定`{end['inherited850_bound']}`。无有效标志的原始0不是测得电压。

独立解码：`{json.dumps(independent, ensure_ascii=False)}`。
kind1表示扩展CPR记录，不能单凭kind推断当前闭环已启用；floor/initial不能替代ceiling。
这是启动交接配置，非实时PMIC测量，也不认证当前RPM与本地stock二进制完全一致。
GPU供电使用前仍需同次rail-ready与数字设定读回；本轮未取得该同次PMIC证据。
未操作GX/PLL/电压或RPM资源票，未读GPU/SMMU，不等于完整GPU上电、ID、CP或Metal验收。

正常180秒timer/四stop/source0/drain与唯一同机Fastboot终点`{lifecycle}`；
USB{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console独立逐字节重放，
接收器`{receiver['status']}`/`{receiver.get('failure', '')}`原样保留。
CPU614.4MHz配置模型/R20显示/旧冻结与原索引保持，无物理UFS provider。
""", encoding="utf-8")
    print(json.dumps(dict(passed=passed, capture_passed=capture_passed, required_uv=required,
                          inherited850_bound=end["inherited850_bound"], lifecycle_passed=lifecycle, frames=audit["frames"])))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
