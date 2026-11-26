#!/usr/bin/env python3
"""Read one saved console log; never query or control a device.

A passing numeric profile is not visual acceptance or package provenance.
Exit codes: 0 numeric profile observed, 1 contradiction, 2 incomplete evidence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re


NUMBER = r"(?:[-+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?|[-+]?(?:nan|inf(?:inity)?))"
MANDATORY = ("cpu", "kernel_dt", "geometry", "backboardd", "mobilegestalt",
             "uiscreen", "surface", "home_window_geometry")


def _near(value, expected, tolerance=0.002):
    try:
        number = float(value)
        return math.isfinite(number) and abs(number - expected) <= tolerance
    except (ValueError, TypeError):
        return False


def _rect(value, expected):
    parts = value.split(",")
    if len(parts) != 4:
        return None
    try:
        numbers = [float(part) for part in parts]
    except ValueError:
        return None
    return all(math.isfinite(a) and abs(a - b) <= 0.002 for a, b in zip(numbers, expected))


def _integer(value):
    return int(value, 16 if value.lower().startswith("0x") else 10)


def _records(lines, token, pattern):
    regex = re.compile(pattern, re.IGNORECASE)
    return [(index + 1, match.groupdict() if (match := regex.search(line)) else None)
            for index, line in enumerate(lines) if token in line]


def _check(records, validator, count=None, identities=None, identity="sample"):
    observed = [{"line": line, "fields": fields} for line, fields in records]
    result = {"status": "unverified", "lines": [n for n, _ in records],
              "observed": observed}
    if not records:
        result["reason"] = "no matching observation"
        return result
    valid = [fields for _, fields in records if fields is not None]
    try:
        outcomes = [validator(fields) for fields in valid]
    except (ValueError, TypeError, KeyError, OverflowError):
        outcomes = [None]
    if any(outcome is False for outcome in outcomes):
        result.update(status="fail", reason="observed values contradict the expected profile")
    elif count is not None and len(records) > count:
        result.update(status="fail", reason="duplicate observations; do not combine boot captures")
    elif len(valid) != len(records):
        result["reason"] = "matching marker has malformed or truncated fields"
    elif any(outcome is None for outcome in outcomes):
        result["reason"] = "source accessors unavailable or numeric fields truncated"
    elif count is not None and len(records) < count:
        result["reason"] = "required observation count is incomplete"
    elif identities is not None and sorted(fields[identity] for fields in valid) != sorted(identities):
        result["reason"] = "required sample identities are missing or repeated"
    else:
        result.update(status="pass", reason="required numeric observations agree")
    return result


def _combine(*parts):
    status = "fail" if any(p["status"] == "fail" for p in parts) else (
        "pass" if all(p["status"] == "pass" for p in parts) else "unverified")
    return {"status": status, "lines": sorted(n for p in parts for n in p["lines"]),
            "parts": list(parts)}


def analyze(text):
    """Return measured-log checks, keeping missing evidence and photos explicit."""
    lines = text.splitlines()
    checks = {}
    banners = [(i + 1, {"banner_sha256": hashlib.sha256(line.strip().encode()).hexdigest()}) for i, line in enumerate(lines)
               if "Darwin Kernel Version" in line]
    boundary = _check(banners, lambda d: True)
    if len({fields["banner_sha256"] for _, fields in banners}) > 1:
        boundary.update(status="fail", reason="different kernel banners; cannot combine captures")
    elif len(banners) > 1:
        boundary.update(status="unverified", reason="the same banner can repeat within one boot; capture boundary not proved")
    checks["capture_boundary"] = boundary
    rows = _records(lines, "LITHIUM RUN A ",
        r"LITHIUM RUN A logical=(?P<logical>[0-9]+) mpidr=(?P<mpidr>0x[0-9a-f]+) "
        r"affinity=(?P<affinity>0x[0-9a-f]+) max_logical=(?P<max>[0-9]+) "
        r"bootcpu=(?P<boot>[0-9]+) cpus=(?P<cpus>[0-9]+) "
        r"expected_affinity=(?P<expected>0x[0-9a-f]+) match=(?P<match>[01])(?:\s|$)")
    checks["cpu"] = _check(rows, lambda d: all(_integer(d[k]) == v for k, v in
        (("logical", 0), ("max", 0), ("boot", 1), ("cpus", 1),
         ("affinity", 1), ("expected", 1), ("match", 1))) and
        (_integer(d["mpidr"]) & 0xff00ffffff) == 1, count=1)
    dt = _records(lines, "LITHIUM DISPLAY DT ",
        r"LITHIUM DISPLAY DT phase=(?P<phase>before|after) chosen_scale=(?P<chosen>[0-9]+) "
        r"artwork_scale=(?P<artwork>[0-9]+) subtype=(?P<subtype>[0-9]+) valid=(?P<valid>[01]/[01]/[01])(?:\s|$)")
    checks["kernel_dt"] = _check(dt, lambda d: None if d["valid"] != "1/1/1" else
        (d["chosen"], d["artwork"], d["subtype"]) == ("2", "2", "1334"),
        count=2, identities=["before", "after"], identity="phase")
    geometry = _records(lines, "LITHIUM DISPLAY GEOMETRY ",
        r"LITHIUM DISPLAY GEOMETRY render=(?P<w>[0-9]+)x(?P<h>[0-9]+) "
        r"panel=(?P<pw>[0-9]+)x(?P<ph>[0-9]+) panel_stride=(?P<row>[0-9]+) "
        r"panel_bpp=(?P<bpp>[0-9]+) auto_profile=(?P<auto>[01]) .*?"
        r"effective_scale=(?P<scale>[0-9]+) effective_subtype=(?P<subtype>[0-9]+)(?:\s|$)")
    checks["geometry"] = _check(geometry, lambda d: all(d[k] == str(v) for k, v in
        (("w", 750), ("h", 1334), ("pw", 1080), ("ph", 2040), ("row", 3240),
         ("bpp", 24), ("auto", 1), ("scale", 2), ("subtype", 1334))), count=1)
    bb_scale = _records(lines, "StartWindowServer: MG scale:",
        rf"StartWindowServer: MG scale:(?P<scale>{NUMBER}) display preferredScale:(?P<preferred>{NUMBER})(?:\s|$)")
    bb_size = _records(lines, "StartWindowServer: initial main screen size:",
        rf"initial main screen size:\{{(?P<w>{NUMBER}),\s*(?P<h>{NUMBER})\}} scale:(?P<scale>{NUMBER})(?:\s|$)")
    checks["backboardd"] = _combine(
        _check(bb_scale, lambda d: _near(d["scale"], 2, 1e-6) and _near(d["preferred"], 2, 1e-6), count=1),
        _check(bb_size, lambda d: _near(d["w"], 750) and _near(d["h"], 1334) and _near(d["scale"], 2, 1e-6), count=1))
    mg = _records(lines, "LITHIUM DISPLAY MG key=main-screen-",
        rf"LITHIUM DISPLAY MG key=main-screen-(?P<key>width|height|scale) available=(?P<available>[01]) "
        rf"complete=(?P<complete>[01]) value=<CFNumber .*?\{{value\s*=\s*(?P<value>{NUMBER}), type = kCFNumber[A-Za-z0-9]+\}}")
    # The diagnostic also prints main-screen-class, whose unavailability is allowed.
    mg = [(n, d) for n, d in mg if "key=main-screen-class " not in lines[n - 1]]
    checks["mobilegestalt"] = _check(mg, lambda d: None if d["available"] != "1" or d["complete"] != "1" else
        _near(d["value"], {"width": 750, "height": 1334, "scale": 2}[d["key"]], 1e-6),
        count=3, identities=["width", "height", "scale"], identity="key")
    screens = _records(lines, "LITHIUM DISPLAY UISCREEN ",
        r"LITHIUM DISPLAY UISCREEN sample=(?P<sample>[0-9]+) bounds=(?P<bounds>\S+) "
        r"scale=(?P<scale>\S+) native_bounds=(?P<native>\S+) native_scale=(?P<ns>\S+) valid=(?P<valid>[01]/[01]/[01]/[01])(?:\s|$)")
    checks["uiscreen"] = _check(screens, lambda d: None if d["valid"] != "1/1/1/1" else
        _rect(d["bounds"], (0, 0, 375, 667)) and _rect(d["native"], (0, 0, 750, 1334)) and
        _near(d["scale"], 2, 1e-6) and _near(d["ns"], 2, 1e-6), count=3, identities=["0", "1", "2"])
    surfaces = _records(lines, "MiniIOSurface: backboardd[",
        r"MiniIOSurface: backboardd\[[0-9]+\] created surface [0-9]+: "
        r"(?P<w>[0-9]+)x(?P<h>[0-9]+) '(?P<format>[^']+)', (?P<row>[0-9]+) bytes per row")
    # Do not misidentify unrelated small surfaces as the primary framebuffer.
    surfaces = [(n, d) for n, d in surfaces if d is None or (d["w"], d["h"]) == ("750", "1334")]
    checks["surface"] = _check(surfaces, lambda d: d["format"] == "BGRA" and
        int(d["row"]) >= 750 * 4 and int(d["row"]) % 4 == 0)
    home = _records(lines, "LITHIUM DISPLAY WINDOW ",
        r"LITHIUM DISPLAY WINDOW sample=(?P<sample>[0-9]+) index=[0-9]+ class=SBHomeScreenWindow "
        r"frame=(?P<frame>\S+) bounds=(?P<bounds>\S+)")
    home = [(n, d) for n, d in home if "class=SBHomeScreenWindow " in lines[n - 1]]
    checks["home_window_geometry"] = _check(home, lambda d: _rect(d["frame"], (0, 0, 375, 667)) and
        _rect(d["bounds"], (0, 0, 375, 667)), count=3, identities=["0", "1", "2"])
    loader = _records(lines, "LITHIUM DISPLAY LOADER phase=after ",
        r"LITHIUM DISPLAY LOADER phase=after chosen_scale=(?P<chosen>[0-9]+) artwork_scale=(?P<artwork>[0-9]+) "
        r"subtype=(?P<subtype>[0-9]+) auto_profile=(?P<auto>[01]) render=(?P<w>[0-9]+)x(?P<h>[0-9]+)(?:\s|$)")
    checks["loader"] = _check(loader, lambda d: (d["chosen"], d["artwork"], d["subtype"], d["auto"], d["w"], d["h"]) ==
        ("2", "2", "1334", "1", "750", "1334"), count=1)
    profiles = _records(lines, "LITHIUM DISPLAY PROFILE ",
        r"LITHIUM DISPLAY PROFILE sample=(?P<sample>[0-9]+) expected=(?P<expected>\S+) "
        r"actual=(?P<w>[^x ]+)x(?P<h>[^@ ]+)@(?P<scale>\S+) "
        r"native=(?P<nw>[^x ]+)x(?P<nh>[^@ ]+)@(?P<ns>\S+) match=(?P<match>[01])(?:\s|$)")
    checks["profile_markers"] = _check(profiles, lambda d: d["expected"] == "375x667@2" and d["match"] == "1" and
        all(_near(d[k], v, 1e-6 if k in ("scale", "ns") else 0.002)
            for k, v in (("w", 375), ("h", 667), ("scale", 2), ("nw", 750), ("nh", 1334), ("ns", 2))),
        count=3, identities=["0", "1", "2"])
    mandatory = [checks[name]["status"] for name in MANDATORY]
    status = "fail" if any(item["status"] == "fail" for item in checks.values()) else (
        "pass" if all(s == "pass" for s in mandatory) else "unverified")
    return {"schema": 1, "scope": "offline observations from one saved console; no device operations",
            "log_profile_status": status, "checks": checks,
            "screen_dimensions_observations": [{"line": i + 1, "text": line} for i, line in enumerate(lines)
                if "LITHIUM DISPLAY MG key=ScreenDimensions " in line],
            "visual_acceptance": "unverified", "goal_complete": False,
            "package_identity": "not established by console text",
            "note": "Even pass requires separate Dock/labels/physical-boundary photographs and frozen-package provenance."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and (args.output.resolve() == args.log.resolve() or args.output.exists()):
        parser.error("output must be a new file, distinct from the input log")
    try:
        raw = args.log.read_bytes()
        result = analyze(raw.decode("utf-8", errors="replace"))
        result.update(input_path=str(args.log.resolve()), input_sha256=hashlib.sha256(raw).hexdigest())
        rendered = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(rendered)
        print(rendered, end="")
    except OSError as error:
        parser.error(str(error))
    return {"pass": 0, "fail": 1, "unverified": 2}[result["log_profile_status"]]


if __name__ == "__main__":
    raise SystemExit(main())
