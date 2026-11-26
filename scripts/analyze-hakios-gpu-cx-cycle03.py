#!/usr/bin/env python3
"""Replay saved owned03 bytes and retain the actual failed readback."""
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("private_cx01_saved_parser", ROOT / "scripts/analyze-hakios-gpu-cx-cycle.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
base.RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-cx-cycle-03"
base.OUT = ROOT / "reports/hakios-ios16-gpu-g1-cx-cycle-03"
original_parse = base.parse


def parse(data):
    rows, spans = original_parse(data)
    text = data
    for span in reversed(spans):
        text = text[:span["start"]] + text[span["end"]:]
    text = text.decode("utf-8", errors="replace")
    for key in ("capture", "control_read", "failure"):
        rows[key] = [base.fields(m.group()) for m in re.finditer(
            re.escape("LITHIUM GPU cx cycle " + key + " ") + r"[^\r\n]*", text)]
    assert len(rows["failure"]) == 1
    failure = rows["failure"][0]
    assert failure["capture_rev"] == "3"
    assert len(rows["capture"]) == int(failure["capture_count"]) <= 4
    assert len(rows["control_read"]) == int(failure["control_count"]) <= 16
    assert failure["capture_dropped"] == failure["control_dropped"] == "0"
    assert int(failure["error"]) == int(rows["end"][0]["error"])
    assert int(failure["cleanup_error"]) == int(rows["end"][0]["cleanup_error"])
    for row in rows["capture"]:
        assert int(row["compare_mask"], 0) == 0xfff007
        if row["mismatch_valid"] == "1":
            assert row["expected_valid"] == row["after_callback_ok"] == "1"
            assert int(row["mismatch"], 0) == ((int(row["after"], 0) ^ int(row["expected"], 0)) & 0xfff007)
    return rows, spans


def validate_active(rows, snapshot, on):
    assert on["pair_valid"] == on["on_observed"] == "1"
    assert int(on["status"], 0) & 0x80000000
    assert int(on["control"], 0) & 0xfff007 == 0x282000
    captures = rows["capture"]
    stored = []
    for row in captures:
        mask = int(row["mask"], 0)
        assert mask in (1, 0xfff000)
        if row["expected_valid"] == "1":
            pre = int(row["pre"], 0)
            expected = int(row["expected"], 0)
            assert expected == (pre & ~mask) | (int(row["value"], 0) & mask)
            assert ((expected ^ pre) & ~mask) == 0
        if row["store_attempted"] == "1":
            assert row["expected_valid"] == "1"
            stored.append(int(row["expected"], 0))
        if row["mismatch_valid"] == "1":
            assert int(row["raw_xor"], 0) == (int(row["after"], 0) ^ int(row["expected"], 0))
        if int(row["rc"]) == 0 and row["after_valid"] == "1":
            assert int(row["mismatch"], 0) == 0
    assert [int(r["raw"], 0) for r in rows["write"]] == stored
    if rows["end"][0]["restore_verified"] == "1":
        assert [r["phase"] for r in captures] == ["timing_set", "request_on", "restore_request", "restore_timing"]
        assert ((int(rows["end"][0]["final_control"], 0) ^ int(snapshot["control"], 0)) & 0xfff007) == 0


def main():
    base.parse = parse
    base.validate_active = validate_active
    status = base.main()
    path = base.OUT.with_suffix(".json")
    report = json.loads(path.read_text(encoding="utf-8"))
    report["capture_revision"] = 3
    report["software_field_compare_mask"] = "0x00fff007"
    report["unowned_control_bit_semantics_verified"] = False
    report["readback_failures"] = [r for r in report["rows"]["capture"] if int(r["rc"]) != 0]
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["\n失败位置与原有回调读回（未新增硬件读写）：\n"]
    for row in report["readback_failures"]:
        lines.append(f"- phase=`{row['phase']}` step=`{row['step']}` rc=`{row['rc']}`；"
                     f"pre=`{row['pre']}` valid=`{row['pre_valid']}`，"
                     f"expected=`{row['expected']}` valid=`{row['expected_valid']}`，"
                     f"after=`{row['after']}` valid=`{row['after_valid']}`，"
                     f"mismatch=`{row['mismatch']}` valid=`{row['mismatch_valid']}`。\n")
    md = base.OUT.with_suffix(".md")
    text = md.read_text(encoding="utf-8").replace("CX本地使能/恢复01", "CX本地使能/恢复03")
    md.write_text(text + "".join(lines), encoding="utf-8")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
