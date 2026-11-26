#!/usr/bin/env python3
"""Saved-only BIMC counter/configuration-restoration and USB analysis."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import importlib.util
import json
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-bimc-measure-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-bimc-measure-01"


def read(p):
    return json.loads(p.read_text(encoding="utf-8-sig"))


def fields(s):
    return dict(re.findall(r"(\w+)=([^\s]+)", s))


def parse(data):
    pattern = re.compile(rb"LITHIUM CPU TEMP (?:(?!LITHIUM)[^\r\n])*freshness_verified=0\r?\n")
    removed = [dict(start=m.start(), end=m.end(), text=m.group().decode()) for m in pattern.finditer(data)]
    view = pattern.sub(b"", data).decode("utf-8", errors="replace")
    rows = {kind: [fields(m.group()) for m in re.finditer(re.escape(
        "LITHIUM GPU bimc measure "+kind+" ")+r"[^\r\n]*", view)]
        for kind in ("begin", "config", "window", "write", "end")}
    assert len(rows["begin"]) == len(rows["end"]) == 1
    assert len(rows["config"]) == 3 and len(rows["window"]) == 2
    begin, end = rows["begin"][0], rows["end"][0]
    assert begin["logical"] == "0" and int(begin["affinity"], 0) == 1
    assert begin["selector"] == "0xad" and begin["postdiv"] == "0"
    required = "rc error cleanup_error measurement_valid clock_off hz cleanup_complete restore_verified dirty uncertain ambiguous_stores loads stores maps unmaps elapsed_us global_overrun global_us window_us poll_cap adapter_error rpm_votes plltest_writes gpu_writes bke smmu".split()
    assert all(x in end for x in required), end
    assert int(end["loads"]) <= 4096 and int(end["stores"]) <= 16
    assert len(rows["write"]) == int(end["stores"])
    assert [int(x["index"]) for x in rows["write"]] == list(range(len(rows["write"])))
    assert all(int(x["pa"], 0) in (0x362000, 0x362004, 0x343008) for x in rows["write"])
    assert all(end[x] == "0" for x in ("rpm_votes", "plltest_writes", "gpu_writes"))
    assert end["bke"] == end["smmu"] == "unread"
    assert [int(x["pa"], 0) for x in rows["config"]] == [0x362000, 0x362004, 0x343008]
    assert [int(x["ticks"], 0) for x in rows["window"]] == [0x1000, 0x10000]
    return rows, removed


def main():
    receiver = read(RUN/"usb-status.json")
    assert receiver["status"] not in ("waiting", "capturing")
    result, returned = read(RUN/"result.json"), read(RUN/"return-verification.json")
    data = (RUN/"console.log").read_bytes()
    rows, spans = parse(data)
    end = rows["end"][0]
    windows, configs = rows["window"], rows["config"]
    measured = end["measurement_valid"] == "1"
    if measured:
        assert all(x["valid"] == "1" for x in windows)
        short, full = [int(x["count"]) for x in windows]
        assert 0 < short < full < 0x1ffffff
        assert all((int(x["ready"], 0) & 0x2000000) == 0 and
                   (int(x["done"], 0) & 0x2000000) != 0 and
                   (int(x["done"], 0) & 0x1ffffff) == int(x["count"]) for x in windows)
        hz = ((full*10+15)*4800000)//(0x10000*10+35)
        assert hz == int(end["hz"])
    else:
        hz = None
    clean = all(int(end[x], 0) == 0 for x in ("cleanup_error", "dirty", "uncertain", "global_overrun")) and end["cleanup_complete"] == "1"
    restored = clean and int(end["restore_verified"], 0) == 7
    if restored:
        for i, row in enumerate(configs):
            mask = 0xfffffff if i == 2 else 0xffffffff
            assert row["snapshot_valid"] == row["restore_verified"] == "1"
            assert (int(row["original"], 0)&mask) == (int(row["final"], 0)&mask)
    spec = importlib.util.spec_from_file_location("bimc_saved_audit", ROOT/"scripts/hakios_usb_audit.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    audit = module.audit((RUN/"console.frames").read_bytes(), data, RUN/"capture-host.py")
    text = data.decode("utf-8", errors="replace")
    stop = [fields(m.group()) for m in re.finditer(r"LITHIUM CPU TEMP phase=stop [^\r\n]*", text)]
    lifecycle = bool(returned.get("passed") and len(stop) == 4 and
        "LITHIUM LOG writer_contention_dropped_bytes=0" in text and audit["emitted_kernel_stream_through_timer_verified"])
    passed = bool(measured and restored and lifecycle and end["rc"] == "0" and
        end["error"] == end["adapter_error"] == "0" and int(end["elapsed_us"]) < 45000)
    report = dict(schema="lithium-gpu-bimc-measure-runtime-v1", passed=passed,
        analyzed_at=datetime.now(timezone.utc).isoformat(), accepted_at=result["accepted_at"],
        image_sha256=result["image_sha256"], boot_args=result["boot_args"],
        measurement_valid=measured, bimc_measured_hz=hz,
        reference="inherited XO/4 assumed 4.8MHz; not independently calibrated",
        configurations_restored=restored, status_count_restoration_claimed=False,
        lifecycle_passed=lifecycle, rows=rows,
        complete_temperature_spans_in_derived_view=spans, original_console_modified=False,
        original_console_sha256=hashlib.sha256(data).hexdigest(),
        stop_temperatures=stop, usb=audit, receiver=receiver, return_verification=returned,
        mx_corner4_proven=False, gpu_aperture_allowed=False,
        gpu_chip_id=None, gpu_power_on_passed=False, metal_acceleration_verified=False)
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    (RUN/"usb-audit.json").write_text(json.dumps(audit, indent=2)+"\n", encoding="utf-8")
    window_rows = "\n".join(f"| {i} | `{x['ticks']}` | {x['valid']} | `{x['ready']}` | `{x['done']}` | {x['count']} | {x['polls']} |" for i, x in enumerate(windows))
    config_rows = "\n".join(f"| `{x['pa']}` | `{x['original']}` | `{x['final']}` | {x['restore_verified']} |" for x in configs)
    OUT.with_suffix(".md").write_text(f"""# GPU G1 BIMC计频 01 · {result['accepted_at'][:10]}

一次隔离RAM启动`{result['accepted_at']}`，镜像`{result['image_sha256']}`。
BIMC本体selector0xad，两次新鲜计数有效`{measured}`，独立公式重算结果`{hz}`Hz。
参考为继承的XO/4=4.8MHz，未单独校准参考源；不是GPU核心频率或RPM原AP票值。
本阶段综合通过`{passed}`，配置恢复`{restored}`，生命周期`{lifecycle}`。

| 窗口 | ticks | valid | READY | DONE | count | polls |
| ---: | --- | ---: | --- | --- | ---: | ---: |
{window_rows}

| 寄存器 | 原配置 | 最终值 | 恢复核对 |
| --- | --- | --- | ---: |
{config_rows}

仅MUX/CTL/XO三个debug配置地址有界写，STATUS从未写入；
XO比较排除只读动态状态[31:28]，不声称DONE/count恢复成原值。
{end['loads']}读/{end['stores']}写，{end['elapsed_us']}µs，2段映射释放{end['unmaps']}。
rc/error/cleanup_error=`{end['rc']}/{end['error']}/{end['cleanup_error']}`；
dirty/uncertain=`{end['dirty']}/{end['uncertain']}`。
每窗口20ms/2000poll，正常40ms、总45ms且保留清理预算；不重试。
未发RPM资源票、未改父时钟或PLLTEST、PMIC/GPU电源时钟未写，未读GPU/SMMU/BKE。

正常180秒timer、四路stop/source0/drain及唯一同机Fastboot检查`{lifecycle}`。
USB独立重放{audit['frames']}帧/{audit['raw_bytes']}raw/{audit['console_bytes']}console；
接收器`{receiver['status']}`/`{receiver.get('failure', '')}`保留。
返回核验时间`{returned.get('finished_at')}`。

Aristotle完成实际core/adapter30个ASan/UBSan专项与ARM64编译；main完成23个参数专项、
完整HakiOS实际构建/镜像读回/上机/新鲜计数与配置恢复独立核对及USB逐字节重放。
计频时序/selector/公式绑定MiCode1492c8e4373b7428bb0accc4f4881e97b36508b9精确下载源码。
原日志未改；派生视图只移除完整温度记录并保留字节偏移。
这不授予未知MX原AP投票，不证明GPU可访问。芯片ID、CP/fence和Metal加速仍未验。
""", encoding="utf-8")
    print(json.dumps(dict(passed=passed, measured_hz=hz, configurations_restored=restored,
        lifecycle_passed=lifecycle, frames=audit["frames"])))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
