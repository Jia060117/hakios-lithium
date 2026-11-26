#!/usr/bin/env python3
"""Analyze one saved, read-only GPU bus capture. Never opens a device."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import importlib.util
import json
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-bus-read-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-bus-read-01"
ADDRESSES = [0x346018, 0x309010, 0x8c5024, 0x8c5000, 0x8c5004,
             0x40c300, 0x300000, 0x300004, 0x300008, 0x30000c,
             0x300010, 0x35200c, 0x8c0000, 0x8c0004, 0x8c0008,
             0x8c000c, 0x8c0010]


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(text):
    return dict(re.findall(r"(\w+)=([^\s]+)", text))


def parse(data):
    # Remove only whole independently delimited temperature records from a
    # derived byte view. Partial records containing a GPU marker cannot match.
    pattern = re.compile(rb"LITHIUM CPU TEMP (?:(?!LITHIUM)[^\r\n])*"
                         rb"freshness_verified=0\r?\n")
    spans = [dict(start=m.start(), end=m.end(), text=m.group().decode())
             for m in pattern.finditer(data)]
    view = pattern.sub(b"", data).decode("utf-8", errors="replace")
    sections = {}
    for kind in ("begin", "word", "end"):
        sections[kind] = [fields(m.group()) for m in re.finditer(
            re.escape("LITHIUM GPU bus " + kind + " ") + r"[^\r\n]*", view)]
    assert len(sections["begin"]) == len(sections["end"]) == 1
    begin, end = sections["begin"][0], sections["end"][0]
    assert begin["logical"] == "0" and int(begin["affinity"], 0) == 1
    assert len(sections["word"]) == 34
    words = [{}, {}]
    for row in sections["word"]:
        p, i = int(row["pass"]), int(row["index"])
        assert p in (0, 1) and 0 <= i < 17 and i not in words[p]
        assert int(row["pa"], 0) == ADDRESSES[i]
        assert row["loaded"] == row["valid"] == ("0" if i == 5 else "1")
        assert row["skipped"] == ("1" if i == 5 else "0")
        words[p][i] = int(row["raw"], 0) if i != 5 else None
    assert words[0] == words[1]
    expected = dict(rc="0", loads="32", load_cap="34", maps="6", unmaps="6",
                    rejected="0", timeout_us="1000", bke_read="0",
                    bke_safety_verified="0", complete17="0", mx_corner4_proven="0",
                    bimc_200mhz_proven="0", gpu_aperture_allowed="0",
                    writes="0", pmic="0", votes="0", bus_abort_proven="0")
    assert all(end.get(k) == v for k, v in expected.items()), end
    assert all(int(end[k], 0) == 0x1ffdf for k in ("valid0", "valid1", "stable"))
    assert 0 <= int(end["elapsed_us"]) < 1000
    return sections, spans, words[0]


def main():
    receiver = read(RUN / "usb-status.json")
    assert receiver["status"] not in ("waiting", "capturing")
    result, returned = read(RUN / "result.json"), read(RUN / "return-verification.json")
    data = (RUN / "console.log").read_bytes()
    sections, spans, words = parse(data)
    spec = importlib.util.spec_from_file_location("bus_saved_audit", ROOT / "scripts/hakios_usb_audit.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    audit = module.audit((RUN / "console.frames").read_bytes(), data, RUN / "capture-host.py")
    text = data.decode("utf-8", errors="replace")
    stop = [fields(m.group()) for m in re.finditer(r"LITHIUM CPU TEMP phase=stop [^\r\n]*", text)]
    lifecycle = bool(returned.get("passed") and len(stop) == 4 and
        "LITHIUM LOG writer_contention_dropped_bytes=0" in text and
        audit["emitted_kernel_stream_through_timer_verified"])
    # Branch enable bit alone is insufficient: halt bit31 is also retained.
    branches = [dict(index=i, pa=hex(ADDRESSES[i]), raw=hex(words[i]),
        enable=bool(words[i] & 1), halt=bool(words[i] & 0x80000000)) for i in (0, 1, 2)]
    ahb_xo = words[3] == 0 and words[4] == 1 and words[2] == 1
    facts = dict(branches=branches, ahb_config_hz=19200000 if ahb_xo else None,
        ahb_measured_hz=None, bke=None, bke_read=False, physical_bimc_hz=None,
        mx_corner4_proven=False, gpu_aperture_allowed=False)
    report = dict(schema="lithium-gpu-bus-read-runtime-v1", passed=lifecycle,
        analyzed_at=datetime.now(timezone.utc).isoformat(),
        accepted_at=result["accepted_at"], image_sha256=result["image_sha256"],
        boot_args=result["boot_args"], capture_verified=True, lifecycle_passed=lifecycle,
        sections=sections, raw_words_by_index=words, derived_facts=facts,
        complete_temperature_spans_in_derived_view=spans, original_console_modified=False,
        original_console_sha256=hashlib.sha256(data).hexdigest(),
        stop_temperatures=stop, usb=audit, receiver=receiver, return_verification=returned,
        gpu_power_on_passed=False, chip_id=None, metal_acceleration_verified=False)
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2)+"\n", encoding="utf-8")
    end = sections["end"][0]
    rows = "\n".join(f"| {i} | `0x{ADDRESSES[i]:08x}` | " +
        ("未读（不解释为0）" if words[i] is None else f"`0x{words[i]:08x}`") + " |" for i in range(17))
    OUT.with_suffix(".md").write_text(f"""# GPU G1 总线只读 01 · 2026-11-25

一次 RAM 启动 `{result['accepted_at']}`，镜像 `{result['image_sha256']}`。
CPU 保持614.4 MHz配置模型。本次读取16字段双遍、共32次，全部有效且一致；
valid/stable均`0x1ffdf`，耗时{end['elapsed_us']}µs，6段映射全部释放。
没有RPM资源请求、PMIC、电源或时钟写入，没有读取GPU aperture和SMMU。

| 索引 | 物理地址 | 两遍一致的原值 |
| ---: | --- | --- |
{rows}

`GCC_BIMC_GFX_CBCR=1`、`MMAGIC_AHB_CBCR=1`，但
`GCC_MMSS_BIMC_GFX_CBCR=0x80004221`的halt位仍置1，不能把enable位1当作已运行。
AHB CMD0/CFG1对应XO旁路，19.2MHz只是配置推导，非频率测量。
BKE未映射/未读，MX corner和BIMC实际速率仍未验证，不允许据此访问GPU寄存器。

180秒正常timer、四路stop、源争用丢0、drain和同机Fastboot终点综合核验：`{lifecycle}`。
USB独立重放{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console字节。
接收器终态`{receiver['status']}` / `{receiver.get('failure', '')}`保留。
唯一返回核验`{returned.get('finished_at')}` / `{returned.get('passed')}`。

原日志字节未改。派生视图只删除完整温度记录并保存原字节偏移，
复原被完整温度printf插入的end行，不补猜任何字节。
主代理执行实际完整内核构建、镜像读回、一次上机与USB独立审计；
实际adapter18个专项及parser17个ASan/UBSan用例通过，未重复历史大回归。
下一推进缺失的MMSS→BIMC总线依赖和有ACK的RPM资源请求；不是再次执行OPEN。
G1芯片ID、CP fence、Metal界面加速仍未验。
""", encoding="utf-8")
    print(json.dumps(dict(passed=lifecycle, capture_verified=True, elapsed_us=int(end["elapsed_us"]),
        frames=audit["frames"], console_bytes=audit["console_bytes"], auto_fastboot=returned.get("passed"))))
    return 0 if lifecycle else 1


if __name__ == "__main__":
    raise SystemExit(main())
