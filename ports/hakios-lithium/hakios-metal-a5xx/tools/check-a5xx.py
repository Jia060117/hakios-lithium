#!/usr/bin/env python3
"""Independently validate every A5xx generated constant/type against Mesa XML.

Does not import generate-a5xx.py. XML location, ownership, name, generation,
numeric value, field type and C++ declaration must agree. --compile compiles
all values/types and runs field/array checks, plus the hand-derived constants
tests; --sanitize adds ASan/UBSan. Writes only the constants evidence directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.parsers.expat
from pathlib import Path

sys.dont_write_bytecode = True
PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parents[2]
EVIDENCE = ROOT / "evidence/gpu-metal-20261123/constants"
MESA = ROOT / "evidence/gpu-metal-20261123/reference/mesa-25.0.7"
RELEASE_SHA256 = "592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_xml(path):
    """A second Expat reader: retain parent relationships and implicit enum values."""
    parser = xml.parsers.expat.ParserCreate()
    nodes, stack = {}, []

    def start(tag, attrs):
        node = {"tag": tag, "attrs": dict(attrs), "line": parser.CurrentLineNumber,
                "parents": list(stack)}
        if tag == "value":
            value = int(attrs["value"], 0) if "value" in attrs else 0
            node["value"] = value
        nodes.setdefault(node["line"], []).append(node)
        stack.append(node)

    parser.StartElementHandler = start
    parser.EndElementHandler = lambda tag: stack.pop()
    with path.open("rb") as stream:
        parser.ParseFile(stream)
    return nodes


def chip_applies(text):
    if not text:
        return True
    for term in text.split(","):
        bounds = term.strip().split("-")
        number = lambda s: int(re.fullmatch(r"A([2-7])XX", s)[1])
        if len(bounds) == 1 and number(bounds[0]) == 5:
            return True
        if len(bounds) == 2 and (number(bounds[0]) if bounds[0] else 2) <= 5 <= (number(bounds[1]) if bounds[1] else 7):
            return True
    return False


def resolve_xml(source, xml):
    candidates = xml[source["file"]].get(source["line"], [])
    if source["tag"] == "implicit_field":
        candidates = [n for n in candidates if n["tag"] in ("reg32", "reg64")
                      and all(n["attrs"].get(k) == v for k, v in source["attrs"].items())]
    else:
        candidates = [n for n in candidates if n["tag"] == source["tag"] and n["attrs"] == source["attrs"]]
    if len(candidates) != 1:
        raise ValueError(f"source location is not exactly one XML element: {source}")
    return candidates[0]


def fullname(node):
    """Reconstruct Mesa-style names from actual domain/stripe/array XML ancestry."""
    if node["tag"] == "bitset":
        return node["attrs"]["name"]
    ancestors = node["parents"] + [node]
    domain = next((n["attrs"] for n in reversed(ancestors) if n["tag"] == "domain"), None)
    if domain is None:
        return node["attrs"].get("name")
    prefix = None
    if domain.get("prefix"):
        prefix = domain.get("variants", "").split(",")[0].split("-")[0] or None
    for n in ancestors:
        if n["tag"] == "stripe":
            prefix = n["attrs"]["variants"].split(",")[0].split("-")[0]
    parts = ([prefix] if prefix else []) + [domain["name"]]
    parts += [n["attrs"]["name"] for n in ancestors if n["tag"] == "array" and n["attrs"].get("name")]
    if node["tag"] == "array" and not any(n["attrs"].get("name") for n in ancestors if n["tag"] == "array"):
        parts.append("")
    if node["tag"] != "array":
        parts.append(node["attrs"]["name"])
    return "_".join(parts)


def position(node, owner=None):
    a = node["attrs"]
    reg = owner if owner is not None else next(
        (n for n in reversed(node["parents"] + [node]) if n["tag"] in ("reg32", "reg64")), None)
    bits = 64 if reg is not None and reg["tag"] == "reg64" else 32
    lo = int(a.get("pos", a.get("low", "0")), 0)
    hi = int(a.get("pos", a.get("high", str(bits - 1))), 0)
    return lo, hi, int(a.get("shr", "0"), 0), int(a.get("radix", "0"), 0), bits


def check_manifest(manifest, xml, header_text):
    errors = []
    declarations = {}
    signatures = {m[2]: (m[1], m[3]) for m in re.finditer(
        r"(?m)^(?:inline|constexpr) (\w+) (\w+)\((\w+) value\) \{", header_text)}
    for m in re.finditer(r"(?m)^inline constexpr (\w+) (\w+) = (0x[\da-fA-F]+)(?:u|ull);", header_text):
        if m[2] in declarations:
            errors.append(f"duplicate declaration {m[2]}")
        declarations[m[2]] = (m[1], int(m[3], 16))
    enum_values = {}
    for enum in re.finditer(r"(?ms)^enum (\w+) : uint32_t \{(.*?)^\};", header_text):
        for value in re.finditer(r"(?m)^\s+(\w+) = (0x[\da-fA-F]+)u,", enum[2]):
            if value[1] in declarations or value[1] in enum_values:
                errors.append(f"duplicate enumerator {value[1]}")
            enum_values[value[1]] = (enum[1], int(value[2], 16))
    found = {**declarations, **enum_values}
    if set(found) != set(manifest["items"]):
        errors.append(f"header/manifest name set mismatch missing={sorted(set(manifest['items'])-set(found))[:8]} "
                      f"extra={sorted(set(found)-set(manifest['items']))[:8]}")
    for name, item in manifest["items"].items():
        try:
            src = resolve_xml(item["source"], xml)
            owner = resolve_xml(item["owner_source"], xml) if item.get("owner_source") else None
            # Check variants at both ends for a shared inline bitset field.
            for n in src["parents"] + [src] + ((owner["parents"] + [owner]) if owner else []):
                if not chip_applies(n["attrs"].get("variants")):
                    raise ValueError("A5XX excluded by XML variants")
            kind = item["kind"]
            expected_type = "uint32_t"
            if kind == "enum_value":
                expected = src["value"]
                parent = next(n for n in reversed(src["parents"]) if n["tag"] == "enum")
                expected_type = parent["attrs"]["name"]
                expected_name = src["attrs"]["name"]
            elif kind == "register":
                expected = sum(int(n["attrs"].get("offset", "0"), 0) for n in src["parents"] + [src]
                               if n["tag"] in ("array", "reg32", "reg64"))
                expected_name = "REG_" + fullname(src)
                if item.get("bits") != (64 if src["tag"] == "reg64" else 32) or item.get("xml_type") != src["attrs"].get("type"):
                    raise ValueError("register bit width/type differs from XML")
            elif kind == "alignment":
                expected = int(src["attrs"]["align"], 0)
                expected_name = "REG_" + fullname(src) + "__ALIGN"
            elif kind.startswith("array"):
                ancestors = src["parents"] + [src]
                linear = [n for n in ancestors if n["tag"] == "array" and int(n["attrs"]["length"], 0) != 1]
                if src["tag"] in ("reg32", "reg64") and "stride" in src["attrs"]:
                    linear.append(src)
                base = sum(int(n["attrs"].get("offset", "0"), 0) for n in ancestors if n["tag"] in ("array", "reg32", "reg64"))
                expected = {"array__base": base, "array__stride": int(linear[-1]["attrs"]["stride"], 0) if linear else 0,
                            "array__length": int(linear[-1]["attrs"]["length"], 0) if linear else 1}[kind]
                expected_name = "REG_" + fullname(src) + {"array__base": "__BASE", "array__stride": "__STRIDE",
                                                        "array__length": "__LENGTH"}[kind]
            else:
                lo, hi, shr, radix, bits = position(src, owner)
                expected_type = "uint64_t" if bits == 64 else "uint32_t"
                expected = {"flag": ((1 << (hi - lo + 1)) - 1) << lo,
                            "field__mask": ((1 << (hi - lo + 1)) - 1) << lo,
                            "field__shift": lo, "field__scale": shr, "field__width": hi - lo + 1,
                            "field__radix": radix, "field__mul": 1 << radix}[kind]
                if owner:
                    stem = fullname(owner)
                else:
                    stem = next(n["attrs"]["name"] for n in reversed(src["parents"]) if n["tag"] == "bitset")
                # A shared bitset is only valid for registers whose XML type names it.
                if owner and owner["tag"] in ("reg32", "reg64") and src["tag"] == "bitfield" and src["parents"][-1]["tag"] == "bitset":
                    if owner["attrs"].get("type") != src["parents"][-1]["attrs"]["name"]:
                        raise ValueError("owner register does not use the source bitset")
                stem += "_" + src["attrs"]["name"] if src["attrs"].get("name") and src["tag"] == "bitfield" else ""
                expected_name = stem + ("" if kind == "flag" else "__" + kind.split("__")[1].upper())
            if expected_name != name:
                raise ValueError(f"XML name is {expected_name}")
            if (item["cpp_type"], item["value"]) != (expected_type, expected):
                raise ValueError(f"manifest value/type differ from XML: expected {expected_type} {expected:#x}")
            if found.get(name) != (expected_type, expected):
                raise ValueError(f"C++ value/type differs from XML: got {found.get(name)} expected {expected_type} {expected:#x}")
        except (ValueError, KeyError, StopIteration) as exc:
            errors.append(f"{name}: {exc}")
    for name, field in manifest["fields"].items():
        try:
            src = resolve_xml(field["source"], xml)
            owner = resolve_xml(field["owner_source"], xml)
            lo, hi, shr, radix, bits = position(src, owner)
            typ = src["attrs"].get("type", "uint")
            cpp = {"boolean": "bool", "int": "int32_t", "float": "float", "fixed": "float", "ufixed": "float",
                   "uint": "uint32_t", "hex": "uint32_t", "a3xx_regid": "uint32_t",
                   "address": "uint64_t", "waddress": "uint64_t"}.get(typ, typ)
            if (field["low"], field["high"], field["shr"], field["radix"], field["bits"], field["xml_type"], field["cpp_type"]) != (
                    lo, hi, shr, radix, bits, typ, cpp):
                raise ValueError("field layout/type metadata differs from XML")
            if not field.get("flag"):
                ret = "uint64_t" if bits == 64 else "uint32_t"
                if signatures.get(name) != (ret, cpp):
                    raise ValueError(f"encoder signature must be {ret}({cpp})")
        except (ValueError, KeyError) as exc:
            errors.append(f"{name}: {exc}")
    for name, array in manifest["arrays"].items():
        try:
            src = resolve_xml(array["source"], xml)
            lineage = src["parents"] + [src]
            base = sum(int(n["attrs"].get("offset", "0"), 0) for n in lineage
                       if n["tag"] in ("array", "reg32", "reg64"))
            indexed = [n for n in lineage if n["tag"] == "array" and int(n["attrs"]["length"], 0) != 1]
            if src["tag"] in ("reg32", "reg64") and "stride" in src["attrs"]:
                indexed.append(src)
            strides = [int(n["attrs"]["stride"], 0) for n in indexed]
            lengths = [int(n["attrs"]["length"], 0) for n in indexed]
            if name != "REG_" + fullname(src) or array["base"] != base or list(array["strides"]) != strides or list(array["lengths"]) != lengths:
                raise ValueError("array name/base/strides/counts differ from XML")
        except (ValueError, KeyError) as exc:
            errors.append(f"{name}: {exc}")
    # Inspect the actual implementation, not just a manifest availability bool.
    switch = re.search(r"constexpr bool opcodeAvailable\(uint32_t opcode\) \{(.*?)\n\}", header_text, re.S)
    actual_opcodes = set(int(s, 16) for s in re.findall(r"case (0x[\da-f]+)u:", switch[1])) if switch else set()
    expected_opcodes = set()
    for file, nodes in xml.items():
        for same_line in nodes.values():
            for node in same_line:
                if node["tag"] == "value" and node["parents"][-1]["attrs"].get("name") == "adreno_pm4_type3_packets":
                    if chip_applies(node["attrs"].get("variants")):
                        expected_opcodes.add(node["value"])
    if actual_opcodes != expected_opcodes or 0x75 in actual_opcodes:
        errors.append("opcodeAvailable disagrees with XML A5XX variants (CP_MEMCPY must be rejected)")
    return errors


def audit_source(manifest):
    """C++ checks derived from separately revalidated XML metadata."""
    out = ['#include "hakios-metal/a5xx.hpp"', '#include <type_traits>', '#include <cstdio>',
           '#include <cstdlib>', 'using namespace hakios::metal::a5xx;',
           'extern int a5xx_constants_tests();',
           'static unsigned long long checks = 0;',
           'static void check(bool ok, const char* name) { ++checks; if (!ok) { std::fprintf(stderr, "XML audit failed: %s\\n", name); std::exit(1); } }']
    for n, it in sorted(manifest["items"].items()):
        out.append(f'static_assert({n} == {it["value"]}ull, "{n} XML value");')
        out.append(f'static_assert(std::is_same_v<std::remove_const_t<decltype({n})>, {it["cpp_type"]}>, "{n} XML C++ type");')
    for n, f in sorted(manifest["fields"].items()):
        if f.get("flag"):
            continue
        ret = "uint64_t" if f["bits"] == 64 else "uint32_t"
        out.append(f'static_assert(std::is_same_v<decltype(&{n}), {ret}(*)({f["cpp_type"]})>, "{n} encoder type");')
    for n, a in sorted(manifest["arrays"].items()):
        args = ", ".join("uint32_t" for _ in a["strides"])
        out.append(f'static_assert(std::is_same_v<decltype(&{n}), uint32_t(*)({args})>, "{n} array type");')
    out.append("int main() {")
    out.append("    uint64_t seed = 0x9e3779b97f4a7c15ull;")
    for n, f in sorted(manifest["fields"].items()):
        if f.get("flag"):
            continue
        if f["cpp_type"] == "float":
            if f["xml_type"] == "float":
                samples = [("0.0f", 0), ("-0.0f", 0x80000000 if f["high"] - f["low"] == 31 else 0x8000),
                           ("1.0f", 0x3f800000 if f["high"] - f["low"] == 31 else 0x3c00),
                           ("-2.0f", 0xc0000000 if f["high"] - f["low"] == 31 else 0xc000)]
            else:
                samples = [("0.0f", 0), ("0.5f", (1 << f["radix"]) // 2),
                           ("1.0f", 1 << f["radix"]), ("1.5f", 3 * (1 << f["radix"]) // 2)]
                if f["xml_type"] == "fixed":
                    samples.append(("-0.5f", (-(1 << f["radix"]) // 2) & 0xffffffff))
            for value, encoded in samples:
                expected = ((encoded >> f["shr"]) << f["low"]) & f["mask"]
                out.append(f'    check({n}({value}) == {expected}ull, "{n} float/fixed XML encoding");')
            continue
        typ, ret = f["cpp_type"], "uint64_t" if f["bits"] == 64 else "uint32_t"
        out += ["    for (uint32_t i = 0; i < 128; ++i) {",
                "        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;",
                f"        const {typ} value = static_cast<{typ}>(i == 0 ? 0ull : i == 1 ? 1ull : i == 2 ? ~0ull : seed);",
                f"        const uint64_t expected = ((uint64_t(value) >> {f['shr']}) << {f['low']}) & {f['mask']}ull;",
                f'        check({n}(value) == {ret}(expected), "{n}");', "    }"]
    for n, a in sorted(manifest["arrays"].items()):
        for maximum in (False, True):
            indices = [length - 1 if maximum else 0 for length in a["lengths"]]
            expected = (a["base"] + sum(s * v for s, v in zip(a["strides"], indices))) & 0xffffffff
            out.append(f'    check({n}({", ".join(str(i) + "u" for i in indices)}) == {expected}u, "{n}");')
    out += ['    const int manual = a5xx_constants_tests();',
            '    std::printf("XML field/array runtime checks=%llu; manual status=%d\\n", checks, manual);',
            '    return manual;', '}', ""]
    return "\n".join(out)


def mesa_witness(args, manifest):
    """Second witness: invoke Mesa's unmodified generator, read-only inputs.

    Mesa c-defines exposes all variant opcode enums together. It is a numeric
    witness, never the A5xx availability gate; XML variants are checked above.
    """
    directory = args.mesa_root / "src/freedreno/registers"
    values = {}
    artifacts = {}
    for name in ("a5xx", "adreno_common", "adreno_pm4"):
        command = [sys.executable, "-B", str(directory / "gen_header.py"), "--rnn", str(directory),
                   "--xml", str(directory / f"adreno/{name}.xml"), "c-defines"]
        output = subprocess.run(command, capture_output=True, text=True)
        if output.returncode:
            raise ValueError(f"unmodified Mesa generator failed: {output.stderr[:500]}")
        dest = args.evidence_dir / "mesa-generated" / f"{name}.xml.h"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(output.stdout, encoding="utf-8")
        artifacts[name] = {"sha256": digest(dest), "command": command, "stderr": output.stderr}
        for m in re.finditer(r"(?m)^#define\s+(\w+)\s+(0x[\da-fA-F]+|\d+)\s*$", output.stdout):
            values.setdefault(m[1], set()).add(int(m[2], 0))
        for m in re.finditer(r"(?m)^\s+(\w+)\s*=\s*(0x[\da-fA-F]+|\d+),?\s*$", output.stdout):
            values.setdefault(m[1], set()).add(int(m[2], 0))
    checked = 0
    xml_derived = {}
    for name, item in manifest["items"].items():
        if name not in values:
            xml_derived[item["kind"]] = xml_derived.get(item["kind"], 0) + 1
            continue
        if item["value"] not in values[name]:
            raise ValueError(f"unmodified Mesa generator numerical mismatch {name}")
        checked += 1
    return {"values_also_in_mesa_c_defines": checked, "extra_metadata_checked_directly_from_xml": xml_derived,
            "artifacts": artifacts, "generator_sha256": digest(directory / "gen_header.py"),
            "note": "Mesa c-defines combines variant enums; availability is independently constrained by XML variants."}


def run_compile(args, manifest):
    folder = args.evidence_dir
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / "xml-constants-audit.cpp"
    source.write_text(audit_source(manifest), encoding="utf-8")
    exe = folder / ("a5xx-constants-sanitized" if args.sanitize else "a5xx-constants-test")
    command = [args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O1", "-g",
               "-I", str(args.header.parents[1]), str(source), str(PORT / "tests/a5xx_constants_test.cpp"), "-o", str(exe)]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    result = subprocess.run(command, text=True, capture_output=True)
    (folder / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise ValueError(f"C++ compilation failed; see {folder / 'compile.log'}")
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    result = subprocess.run([str(exe.resolve())], text=True, capture_output=True, env=env)
    (folder / "runtime.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise ValueError(f"C++ runtime failed; see {folder / 'runtime.log'}")
    return {"command": command, "sanitizers": ["address", "undefined"] if args.sanitize else [],
            "exit_code": result.returncode, "stdout": result.stdout,
            "binary_sha256": digest(exe), "audit_cpp_sha256": digest(source)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesa-root", type=Path, default=MESA)
    parser.add_argument("--evidence-dir", type=Path, default=EVIDENCE)
    parser.add_argument("--manifest", type=Path, default=EVIDENCE / "constants-manifest.json")
    parser.add_argument("--header", type=Path, default=PORT / "include/hakios-metal/a5xx.hpp")
    parser.add_argument("--compile", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--mesa-witness", action="store_true")
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    result = {"scope": "host-only constants sub-item; M0 not passed", "device_operations": 0,
              "errors": [], "passed": False}
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if manifest.get("release_sha256") != RELEASE_SHA256 or manifest.get("mesa_release") != "25.0.7" or manifest.get("generation") != 5:
            raise ValueError("manifest does not identify the authorized Mesa 25.0.7 archive")
        xml = {}
        for file, expected in manifest["xml_files"].items():
            path = args.mesa_root / file
            if digest(path) != expected:
                raise ValueError(f"XML SHA256 drift: {file}")
            xml[file] = read_xml(path)
        text = args.header.read_text(encoding="utf-8")
        errors = check_manifest(manifest, xml, text)
        result["errors"] = errors
        result["counts"] = {"constants": len(manifest["items"]), "fields": len(manifest["fields"]),
                            "arrays": len(manifest["arrays"]), "xml_files": len(xml)}
        if errors:
            raise ValueError(f"{len(errors)} XML/header validation errors: {errors[:4]}")
        inventory_path = args.manifest.parent / "hakios-usage-inventory.json"
        diff_path = args.manifest.parent / "a6xx-to-a5xx-differences.json"
        if inventory_path.exists() and diff_path.exists():
            inv = json.loads(inventory_path.read_text(encoding="utf-8"))
            diff = json.loads(diff_path.read_text(encoding="utf-8"))
            inv_names = [d["name"] for d in inv["symbols"]]
            diff_names = [d["symbol"] for d in diff["rows"]]
            if len(diff_names) != len(set(diff_names)) or sorted(inv_names) != sorted(diff_names):
                raise ValueError("differences do not cover every inventory symbol exactly once")
            for row in diff["rows"]:
                if not row.get("action") or (not row["a5xx"] and not row.get("sources")):
                    raise ValueError(f"difference has neither counterpart nor sourced alternative: {row['symbol']}")
            protected_root = Path(inv["source_root"])
            # A Windows inventory may be verified from the WSL host toolchain.
            if os.name != "nt" and re.match(r"^[A-Za-z]:[\\/]", str(protected_root)):
                raw = str(protected_root).replace("\\", "/")
                protected_root = Path("/mnt/" + raw[0].lower() + raw[2:])
            for file, expected in inv["files"].items():
                if digest(protected_root / file) != expected:
                    raise ValueError(f"read-only HakiOS input changed since inventory: {file}")
            for file, expected in diff["reference_hashes"].items():
                if digest(args.mesa_root / file) != expected:
                    raise ValueError(f"comparison source changed: {file}")
            result["inventory_coverage"] = {"symbols": len(inv_names), "rows": len(diff_names),
                                            "read_only_hakios_files": len(inv["files"]),
                                            "comparison_reference_files": len(diff["reference_hashes"])}
        if args.self_test:
            mutations = {
                "wrong_value": text.replace("REG_A5XX_SP_CS_CTRL_REG0 = 0x0000e5f0u", "REG_A5XX_SP_CS_CTRL_REG0 = 0x0000a9b0u"),
                "wrong_constant_type": text.replace("uint32_t REG_A5XX_SP_CS_CTRL_REG0 =", "uint64_t REG_A5XX_SP_CS_CTRL_REG0 ="),
                "wrong_encoder_type": text.replace("A5XX_SP_CS_CTRL_REG0_THREADSIZE(a3xx_threadsize value)", "A5XX_SP_CS_CTRL_REG0_THREADSIZE(uint32_t value)"),
                "missing_constant": re.sub(r"(?m)^inline constexpr uint32_t REG_A5XX_SP_CS_CTRL_REG0 =.*\n", "", text),
                "foreign_extra_constant": text.replace("namespace hakios::metal::a5xx {", "namespace hakios::metal::a5xx {\ninline constexpr uint32_t A6XX_FORBIDDEN = 0x00000075u;"),
                "memcpy_generation_bypass": text.replace("switch (opcode) {", "switch (opcode) {\n    case 0x75u:"),
            }
            result["negative_checks"] = {}
            for label, mutated in mutations.items():
                if text == mutated or not check_manifest(manifest, xml, mutated):
                    raise ValueError(f"negative test failed to reject {label}")
                result["negative_checks"][label] = "rejected"
            import copy
            bad = copy.deepcopy(manifest)
            bad["fields"]["A5XX_TEX_CONST_2_PITCH"]["shr"] = 6
            if not check_manifest(bad, xml, text):
                raise ValueError("checker failed to reject incorrect XML field scale")
            result["negative_checks"]["wrong_xml_field_scale"] = "rejected"
            bad = copy.deepcopy(manifest)
            bad["items"]["REG_A5XX_SP_CS_CTRL_REG0"]["source"]["line"] = 1
            if not check_manifest(bad, xml, text):
                raise ValueError("checker failed to reject incorrect XML source line")
            result["negative_checks"]["wrong_xml_source_line"] = "rejected"
        if args.mesa_witness:
            result["mesa_witness"] = mesa_witness(args, manifest)
        if args.compile or args.sanitize:
            result["cpp"] = run_compile(args, manifest)
        result["header_sha256"] = digest(args.header)
        result["source_sha256"] = {
            "generate-a5xx.py": digest(PORT / "tools/generate-a5xx.py"),
            "check-a5xx.py": digest(Path(__file__)),
            "a5xx_constants_test.cpp": digest(PORT / "tests/a5xx_constants_test.cpp"),
        }
        result["passed"] = True
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        result["errors"].append(str(exc))
    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    (args.evidence_dir / "validation.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
