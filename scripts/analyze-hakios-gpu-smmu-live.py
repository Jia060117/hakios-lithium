#!/usr/bin/env python3
"""Decode saved G3 SMMU raw samples; no device access or shared report updates."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OWN = ROOT / "evidence/gpu-metal-20261126/g3-package"
RUN = OWN / "runs/gpu-g3-read-01"
WIDTHS = (32, 36, 40, 42, 44, 48)
CB_REGS = (0, 4, 0x10, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34,
           0x38, 0x3c, 0x58, 0x60, 0x64, 0x68, 0x6c)
STATUS = ("OK", "DISABLED", "ARGUMENT", "USED", "CONTEXT", "TIME", "IO", "LIMIT",
          "UNSTABLE", "GEOMETRY", "CAPABILITY", "ROUTING", "OWNERSHIP", "MEMORY", "BUSY", "DIRTY")
LEGACY_RULES = dict(profile="kernel22-extra-id7-check-no-native-resume",
    kernel_sha256="690b1b8f0ecdd632d5271c697352c04a6fccd6e9937e7558ec9147e9151e5034",
    smmu_source_sha256="eb13ae3296e713e748a4dd3ec33edc39a6426eae7136bc0a4c46eed4e5f4057f",
    id7_major_required=True, scm_interrupted_resume=False)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def number(value):
    try:
        return int(value, 0)
    except (TypeError, ValueError):
        return None


def width(code):
    return WIDTHS[code] if code is not None and 0 <= code < len(WIDTHS) else None


def rows(text, prefix):
    return [dict(line=i, **fields(line)) for i, line in enumerate(text.splitlines(), 1) if line.startswith(prefix)]


def one(records):
    return records[0] if len(records) == 1 else {}


def zeros(row, names):
    return all(number(row.get(name)) == 0 for name in names)


def inspect(text, rules=None):
    rules = LEGACY_RULES if rules is None else rules
    summaries = rows(text, "LITHIUM SMMU discover ")
    summary = one(summaries)
    cpu_rows = rows(text, "LITHIUM SMMU cpu_mmfr0 ")
    draft_rows = rows(text, "LITHIUM SMMU draft ")
    tlb_rows = rows(text, "LITHIUM SMMU tlb ")
    stores = rows(text, "LITHIUM SMMU store ")
    raw_rows = rows(text, "LITHIUM SMMU raw ")
    errors, samples, by_offset = [], [], {}
    for raw in raw_rows:
        off, valid, a, b = (number(raw.get(k)) for k in ("off", "valid", "a", "b"))
        shape = (off is not None and 0 <= off < 0x10000 and off % 4 == 0 and valid in (0, 1, 2, 3) and
                 a is not None and b is not None and 0 <= a <= 0xffffffff and 0 <= b <= 0xffffffff)
        stable = bool(shape and valid == 3 and a == b)
        sample = dict(line=raw["line"], offset=off, valid=valid, raw_a=a, raw_b=b,
                      a=a if shape and valid & 1 else None, b=b if shape and valid & 2 else None,
                      stable=stable, value=b if stable else None)
        samples.append(sample)
        if not shape:
            errors.append("malformed raw row at line " + str(raw["line"]))
        elif off in by_offset:
            errors.append("duplicate raw offset " + hex(off))
            by_offset[off] = None
        else:
            by_offset[off] = sample

    def value(off):
        record = by_offset.get(off)
        return record["value"] if record else None

    def pair(off):
        lo, hi = value(off), value(off + 4)
        return (hi << 32) | lo if lo is not None and hi is not None else None

    ids = [value(off) for off in (0x20, 0x24, 0x28, 0x3c)]
    geometry = None
    expected_offsets = None
    if all(v is not None for v in ids):
        id0, id1, id2, id7 = ids
        cpu = one(cpu_rows)
        mmfr = number(cpu.get("raw")) if number(cpu.get("valid")) == 1 else None
        cpu_bits = width(mmfr & 15) if mmfr is not None else None
        ias, oas, ubs = (width((id2 >> shift) & 15) for shift in (0, 4, 8))
        cb_count, s2cb_count = id1 & 255, (id1 >> 16) & 255
        smr_count, page_shift, page_index = id0 & 255, 16 if id1 >> 31 else 12, (id1 >> 28) & 7
        cb_mask = number(summary.get("cb_mask"))
        profile = (page_shift == 12 and page_index == 2 and 1 <= cb_count <= 8 and
                   s2cb_count <= cb_count and cb_mask is not None and 0 <= cb_mask <= 255 and cb_mask >> cb_count == 0)
        id7_accepted = not rules["id7_major_required"] or ((id7 >> 4) & 15) == 2
        capability = id7_accepted and bool(id0 & (1 << 27)) and 1 <= smr_count <= 128 and not (id0 & 256) and all((ias, oas, ubs))
        effective = min(cpu_bits, ias, oas) if all((cpu_bits, ias, oas)) else None
        matches = all(number(summary.get(k)) == v for k, v in
                      (("cpu_pa", cpu_bits), ("ias", ias), ("oas", oas), ("ubs", ubs), ("cap_pa", effective)))
        geometry = dict(id0=id0, id1=id1, id2=id2, id7=id7, major=(id7 >> 4) & 15, minor=id7 & 15,
            register_page_shift=page_shift, numpagendxb=page_index, num_smr=smr_count,
            num_cb=cb_count, num_s2cb=s2cb_count, cb_read_mask=cb_mask,
            stage1_supported=bool(id0 & (1 << 30)), stream_match_supported=bool(id0 & (1 << 27)),
            coherent_walk_supported=bool(id0 & (1 << 14)), ptfs_4k=bool(id2 & (1 << 12)),
            extended_sid_encoding=bool(id0 & 256), cpu_pa_bits=cpu_bits, ias_bits=ias,
            oas_bits=oas, ubs_bits=ubs, effective_pa_bits=effective,
            source_bound_profile_supported=profile, capability_checks_passed=bool(capability),
            id7_extra_major_check_required=rules["id7_major_required"], id7_extra_major_check_passed=id7_accepted,
            logged_widths_match_decode=bool(matches), cpu_mmfr0=cpu_rows)
        if profile and capability:
            expected_offsets = [0x20, 0x24, 0x28, 0x3c, 0, 0x48, 0x50, 0x54, 0x58]
            for index in range(smr_count):
                expected_offsets += [0x800 + 4 * index, 0xc00 + 4 * index]
            for cb in range(cb_count):
                if cb_mask & (1 << cb):
                    expected_offsets += [0x1000 + 4 * cb, 0x1400 + 4 * cb, 0x1800 + 4 * cb]
                    expected_offsets += [0x8000 + 0x1000 * cb + off for off in CB_REGS]

    routes = []
    if geometry and geometry["source_bound_profile_supported"] and 0 < geometry["num_smr"] <= 128:
        for index in range(geometry["num_smr"]):
            smr, s2cr = value(0x800 + 4 * index), value(0xc00 + 4 * index)
            entry = dict(index=index, smr=smr, s2cr=s2cr, decoded=smr is not None and s2cr is not None,
                         active=None, sid=None, mask=None, type=None, cb_index=None, privcfg=None,
                         matches_sid0=None, matches_sid2=None)
            if smr is not None and s2cr is not None:
                sid, mask, active = smr & 65535, (smr >> 16) & 0x7fff, bool(smr & 0x80000000)
                entry.update(active=active, sid=sid, mask=mask, type=("TRANS", "BYPASS", "FAULT", "RESERVED")[(s2cr >> 16) & 3],
                             cb_index=s2cr & 255, privcfg=(s2cr >> 24) & 3,
                             matches_sid0=active and ((sid ^ 0) & ~mask & 65535) == 0,
                             matches_sid2=active and ((sid ^ 2) & ~mask & 65535) == 0)
            routes.append(entry)
    contexts = []
    if geometry and geometry["source_bound_profile_supported"]:
        for cb in range(geometry["num_cb"]):
            if not geometry["cb_read_mask"] & (1 << cb):
                continue
            base = 0x8000 + 0x1000 * cb
            sctlr = value(base)
            contexts.append(dict(index=cb, cbar=value(0x1000 + 4 * cb), cba2r=value(0x1800 + 4 * cb),
                cbfrsynra=value(0x1400 + 4 * cb), sctlr=sctlr, enabled=bool(sctlr & 1) if sctlr is not None else None,
                actlr=value(base + 4), tcr2=value(base + 0x10), ttbr0=pair(base + 0x20), ttbr1=pair(base + 0x28),
                tcr=value(base + 0x30), contextidr=value(base + 0x34), mair0=value(base + 0x38), mair1=value(base + 0x3c),
                fsr=value(base + 0x58), far=pair(base + 0x60), fsynr0=value(base + 0x68), fsynr1=value(base + 0x6c)))
    readonly = bool(len(summaries) == len(draft_rows) == len(tlb_rows) == 1 and not stores and
                    zeros(summary, ("writes", "installed", "hold")) and
                    zeros(draft_rows[0], ("prepare_used", "prepared", "program_used", "attempts")) and
                    zeros(tlb_rows[0], ("polls", "valid")))
    reads, elapsed = number(summary.get("reads")), number(summary.get("elapsed_us"))
    complete = bool(len(summaries) == 1 and not errors and number(summary.get("used")) == 1 and
        zeros(summary, ("entry_rc", "rc")) and number(summary.get("complete")) == 1 and
        number(summary.get("samples")) == len(samples) and expected_offsets is not None and
        [s["offset"] for s in samples] == expected_offsets and all(s["stable"] for s in samples) and
        reads == 2 * len(samples) and reads <= 1024 and elapsed is not None and 0 <= elapsed < 20000 and
        number(summary.get("cb_mask")) == 1 and number(summary.get("maps")) in range(1, 5) and
        number(summary.get("maps")) == number(summary.get("unmaps")) and
        geometry["logged_widths_match_decode"])
    rc = number(summary.get("rc"))
    sid0 = [r["index"] for r in routes if r["matches_sid0"]]
    sid2 = [r["index"] for r in routes if r["matches_sid2"]]
    return dict(smmu_capture_passed=complete, smmu_read_only_observed=readonly,
        smmu_read_only_discovery_passed=complete and readonly, discover=summaries, source_rules=rules,
        status_name=STATUS[rc] if rc is not None and 0 <= rc < len(STATUS) else None,
        raw=samples, parse_errors=errors, geometry=geometry, routing=routes, contexts=contexts,
        globals={hex(off): value(off) for off in (0, 0x48, 0x50, 0x54, 0x58)},
        routing_summary=dict(sid0_matching_indices=sid0, sid2_matching_indices=sid2,
            sid0_ambiguous=len(sid0) > 1, sid2_ambiguous=len(sid2) > 1,
            all_stream_rows_decoded=bool(routes) and all(r["decoded"] for r in routes), ownership_proven=False),
        draft=draft_rows, tlb=tlb_rows, stores=stores,
        interpretation="Only valid=3 and equal a/b decode. Invalid/missing values are null; raw storage retained. "
                       "BYPASS/FAULT or disabled CB do not establish ownership. Only cb_mask-selected CBs observed. "
                       "Frozen combined-end smmu=unread is a constant label, not discovery evidence.")


def pas_and_g1(text):
    pas = load("_g3_pas_inspect", ROOT / "scripts/analyze-hakios-gpu-zap.py").inspect(text)
    before, after = pas["smc_before"], pas["smc_after"]
    calls = (0x42000201, 0x42000202, 0x42000205, 0x42000206)
    exact_calls = bool(len(before) == len(after) == 4 and
        all(number(b.get("index")) == number(a.get("index")) == i and number(b.get("x0")) == calls[i] and
            number(b.get("x2")) == 13 and zeros(a, ("callback", "x0", "x1"))
            for i, (b, a) in enumerate(zip(before, after))))
    pas["pas_authenticated_and_shutdown"] = pas["pas_authenticated_and_shutdown"] and exact_calls
    combined = one(pas["combined_end"])
    g1 = bool(zeros(combined, ("error", "retain_power", "uncertain")) and
        all(number(combined.get(k)) == 1 for k in ("consumer_safe", "original_verified", "restore_ack", "vbif_valid")))
    returns = []
    for record in after:
        x0, x1, x6 = (number(record.get(k)) for k in ("x0", "x1", "x6"))
        interrupted = x0 == 1
        classification = ("SCM_INTERRUPTED" if interrupted else "SCM_COMPLETED" if x0 == 0 else
                          "SCM_NONZERO_TRANSPORT_RETURN" if x0 is not None else "UNPARSED")
        returns.append(dict(line=record["line"], index=number(record.get("index")), x0=x0, x1=x1, x6=x6,
            classification=classification, continuation_required=interrupted,
            final_secure_result_valid=x0 == 0, secure_result=x1 if x0 == 0 else None,
            secure_rejection_observed=(x1 != 0 if x0 == 0 and x1 is not None else None)))
    return dict(pas=pas, g1_cleanup_passed=g1, pas_exact_stock_direct_calls=exact_calls,
        scm_returns=returns, scm_interrupted_observed=any(r["continuation_required"] for r in returns),
        native_continuation_log=rows(text, "LITHIUM GPU pas resume "))


def self_test():
    # Small parser tests only; these fixtures are never hardware evidence.
    offsets = [0x20, 0x24, 0x28, 0x3c, 0, 0x48, 0x50, 0x54, 0x58, 0x800, 0xc00,
               0x1000, 0x1400, 0x1800] + [0x8000 + off for off in CB_REGS]
    values = {0x20: (1 << 30) | (1 << 27) | 1, 0x24: 0x20000001,
              0x28: 0x1444, 0x3c: 0x20, 0x800: 0x80000000, 0xc00: 0}
    prefix = (f"LITHIUM SMMU discover used=1 entry_rc=0 rc=0 complete=1 reads={2 * len(offsets)} "
              f"samples={len(offsets)} elapsed_us=100 cb_mask=0x1 cpu_pa=44 ias=44 oas=44 ubs=44 cap_pa=44 maps=3 unmaps=3 writes=0 installed=0 hold=0\n"
              "LITHIUM SMMU cpu_mmfr0 valid=1 raw=0x4\n"
              "LITHIUM SMMU draft prepare_used=0 prepare_rc=0 prepared=0 program_used=0 program_rc=0 attempts=0\n"
              "LITHIUM SMMU tlb polls=0 valid=0 status=0x0\n")
    fixture = prefix + "".join(f"LITHIUM SMMU raw off=0x{o:x} valid=0x3 a=0x{values.get(o, 0):x} b=0x{values.get(o, 0):x}\n" for o in offsets)
    result = inspect(fixture)
    checks = {
        "valid_complete": result["smmu_read_only_discovery_passed"],
        "sid0_translation_cb0": result["routing"][0]["type"] == "TRANS" and result["routing_summary"]["sid0_matching_indices"] == [0],
        "stable_zero_is_value": result["contexts"][0]["sctlr"] == 0,
        "missing_is_null": inspect(fixture.replace("LITHIUM SMMU raw off=0x8000 valid=0x3 a=0x0 b=0x0\n", ""))["contexts"][0]["sctlr"] is None,
        "partial_zero_is_null": inspect(fixture.replace("off=0x8000 valid=0x3", "off=0x8000 valid=0x1"))["contexts"][0]["sctlr"] is None,
        "unstable_rejected": not inspect(fixture.replace("off=0x8000 valid=0x3 a=0x0 b=0x0", "off=0x8000 valid=0x3 a=0x0 b=0x1"))["smmu_capture_passed"],
        "duplicate_rejected": not inspect(fixture + "LITHIUM SMMU raw off=0x20 valid=3 a=0 b=0\n")["smmu_capture_passed"],
        "store_rejected": not inspect(fixture + "LITHIUM SMMU store off=0 width=4 value=1\n")["smmu_read_only_observed"],
        "geometry_rejected": not inspect(fixture.replace("0x20000001", "0x30000001"))["smmu_capture_passed"],
        "time_rejected": not inspect(fixture.replace("elapsed_us=100", "elapsed_us=20000"))["smmu_capture_passed"],
        "mapping_leak_rejected": not inspect(fixture.replace("unmaps=3", "unmaps=2"))["smmu_capture_passed"],
        "invalid_high_word_prevents_64bit": inspect(fixture.replace("off=0x8024 valid=0x3", "off=0x8024 valid=0x0"))["contexts"][0]["ttbr0"] is None,
        "bypass_is_observation": inspect(fixture.replace("off=0xc00 valid=0x3 a=0x0 b=0x0", "off=0xc00 valid=0x3 a=0x10000 b=0x10000"))["routing"][0]["type"] == "BYPASS",
        "empty_not_success": not inspect("")["smmu_read_only_discovery_passed"],
    }
    new_rules = dict(LEGACY_RULES, profile="test-only-new-id7-policy", id7_major_required=False,
                     scm_interrupted_resume=True)
    zero_id7_fixture = fixture.replace("off=0x3c valid=0x3 a=0x20 b=0x20", "off=0x3c valid=0x3 a=0x0 b=0x0")
    checks["old_rule_rejects_zero_id7"] = not inspect(zero_id7_fixture)["smmu_capture_passed"]
    checks["explicit_new_rule_accepts_observation_zero_id7"] = inspect(zero_id7_fixture, new_rules)["smmu_capture_passed"]
    rejected = (prefix.replace("entry_rc=0 rc=0 complete=1", "entry_rc=10 rc=10 complete=0")
               .replace(f"reads={2 * len(offsets)} samples={len(offsets)}", "reads=8 samples=4") +
               "".join(f"LITHIUM SMMU raw off=0x{o:x} valid=3 a=0x{values.get(o, 0):x} b=0x{values.get(o, 0):x}\n" for o in offsets[:4]))
    checks["old_recorded_capability10_never_reclassified_pass"] = not inspect(rejected, new_rules)["smmu_capture_passed"]
    interruption = pas_and_g1("LITHIUM GPU pas smc after index=0 callback=0 x0=0x1 x1=0x82 x2=0xd x6=0x61900003\n")
    checks["scm_interrupted_not_secure_rejection"] = (interruption["scm_interrupted_observed"] and
        interruption["scm_returns"][0]["secure_rejection_observed"] is None and
        interruption["scm_returns"][0]["secure_result"] is None and
        not interruption["pas"]["pas_authenticated_and_shutdown"])
    return dict(passed=all(checks.values()), cases=len(checks), checks=checks, hardware_evidence=False, device_operations=0)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def runtime(run):
    package = load("_g3_runtime_package", ROOT / "scripts/package-hakios-gpu-smmu-live.py")
    state = read_json(run / "result.json")
    match = re.fullmatch(r"gpu-g3-read-(01|02)", state.get("attempt_id", ""))
    if not match:
        raise ValueError("saved run has no known G3 attempt")
    package.configure(match[1])
    admitted = package.verify_package()
    if not (state.get("command_accepted") is True and state.get("attempt_id") == package.ATTEMPT and
            state.get("phase") == package.PHASE and state.get("image_sha256") == admitted["image_sha256"] and
            state.get("boot_args") == admitted["cmdline"]):
        raise ValueError("saved run identity differs from G3-read-01")
    console = (run / "console.log").read_bytes()
    text = console.decode(errors="replace")
    report = read_json(package.OUT / "kernel-result.json")
    rules = package.rules_for_kernel(report)
    result = dict(**inspect(text, rules), **pas_and_g1(text))
    receiver = read_json(run / "usb-status.json")
    auditor = load("_g3_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
    audit = auditor.audit((run / "console.frames").read_bytes(), console, run / "capture-host.py")
    returned = read_json(run / "return-verification.json") if (run / "return-verification.json").exists() else None
    stops = rows(text, "LITHIUM CPU TEMP phase=stop ")
    losses = [int(v) for v in re.findall(r"writer_contention_dropped_bytes=(\d+)", text)]
    final = receiver.get("status") not in ("waiting", "capturing") and returned is not None
    lifecycle = bool(final and returned.get("passed") is True and audit["emitted_kernel_stream_through_timer_verified"] and
                     len(stops) == 4 and losses and not any(losses))
    result.update(schema="lithium-g3-smmu-read-runtime-v2", attempt=package.ATTEMPT,
        analyzed_at=datetime.now(timezone.utc).isoformat(), accepted_at=state["accepted_at"],
        image_sha256=state["image_sha256"], boot_args=state["boot_args"], final=final,
        lifecycle_passed=lifecycle, usb=audit, receiver_status=receiver, return_verification=returned,
        stop_temperature_records=stops, source_log_drops=losses,
        passed=bool(result["smmu_read_only_discovery_passed"] and result["pas"]["pas_authenticated_and_shutdown"] and
                    result["g1_cleanup_passed"] and lifecycle),
        smmu_translation_installed=False, gpu_chip_id_verified=False, cp_verified=False, metal_acceleration_verified=False)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02"), default="01")
    parser.add_argument("--run", type=Path, help="saved run directory; never contacts device")
    parser.add_argument("--console", type=Path, help="parse saved console only; no lifecycle success claim")
    parser.add_argument("--kernel-report", type=Path, help="frozen packaged report for a parse-only console")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--output", type=Path, help="optional JSON output, restricted to owned G3 evidence")
    args = parser.parse_args()
    if args.self_test:
        result = self_test()
    elif args.console:
        text = args.console.read_bytes().decode(errors="replace")
        package = load("_g3_console_rules", ROOT / "scripts/package-hakios-gpu-smmu-live.py")
        package.configure(args.attempt)
        report = read_json(args.kernel_report or package.OUT / "kernel-result.json")
        rules = package.rules_for_kernel(report)
        result = dict(**inspect(text, rules), **pas_and_g1(text), final=False, passed=False,
                      lifecycle_passed=None, scope="saved console parsing only")
    else:
        result = runtime(args.run or OWN / "runs" / ("gpu-g3-read-" + args.attempt))
    output = args.output or (None if args.self_test or args.console else
                             OWN / ("runtime-" + result["attempt"][-2:] + "-source-bound.json"))
    if output:
        if not output.resolve().is_relative_to(OWN.resolve()):
            raise ValueError("output must remain inside exclusive G3 evidence")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result.get("passed") or args.console else 1


if __name__ == "__main__":
    raise SystemExit(main())
