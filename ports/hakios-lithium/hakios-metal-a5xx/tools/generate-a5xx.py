#!/usr/bin/env python3
"""Generate the host-only A5xx constants subset from the pinned Mesa XML.

No network, build-system, source-reference, or device writes. Run --inventory-only
before Mesa arrives. Defaults are workspace-relative, independent of cwd. The
generated API uses XML types (including enum parameter types); it deliberately
does not provide A6XX aliases or A635 chip facts.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import xml.parsers.expat
from pathlib import Path

sys.dont_write_bytecode = True
PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parents[2]
EVIDENCE = ROOT / "evidence/gpu-metal-20261123/constants"
MESA = ROOT / "evidence/gpu-metal-20261123/reference/mesa-25.0.7"
HAKIOS = ROOT / "upstream/hakios-xnu-kernel/hakios-metal"
RELEASE_SHA256 = "592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6"
TOKEN = re.compile(r"\b[A-Za-z_]\w*\b")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def strip_comments(text):
    """Preserve offsets/line numbers while excluding comments and string contents."""
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: re.sub(r"[^\n]", " ", m.group()), text, flags=re.S)


def inventory(source):
    hp = source / "include/hakios-metal/a6xx.hpp"
    original = hp.read_text(encoding="utf-8")
    code = strip_comments(original)
    declarations = {}
    pattern = re.compile(
        r"(?m)^\s*(?:inline\s+)?constexpr\s+([\w:]+)\s+(\w+)\s*(?=[=(])"
        r"|^\s*(enum|struct)\s+(\w+)"
        r"|^\s*([A-Za-z_]\w*)\s*=\s*([^;\n]+),\s*$")
    for m in pattern.finditer(code):
        typ, name = (m[1], m[2]) if m[2] else ((m[3], m[4]) if m[4] else ("enumerator", m[5]))
        start = m.start()
        actual = start + len(m[0]) - len(m[0].lstrip())
        line = code.count("\n", 0, actual) + 1
        tail = code[m.end():]
        kind = "helper" if tail.startswith("(") else "constant"
        if typ in ("enum", "struct"):
            kind = typ
        end = code.find(";", m.end()) + 1
        if typ == "enumerator":
            end = m.end()
        if kind in ("helper", "enum", "struct"):
            brace = code.find("{", m.end())
            if brace >= 0:
                level, end = 1, brace + 1
                while level and end < len(code):
                    level += (code[end] == "{") - (code[end] == "}")
                    end += 1
        declarations[name] = {
            "name": name, "kind": kind, "cpp_type": typ,
            "declaration": {"file": hp.relative_to(source).as_posix(), "line": line},
            "definition": original[actual:end], "_code": code[actual:end],
            "uses": [], "required_by": [],
        }
    files = [hp] + sorted((source / "src").glob("*.cpp"))
    for path in files[1:]:
        for line, content in enumerate(strip_comments(path.read_text(encoding="utf-8")).splitlines(), 1):
            for name in sorted(set(TOKEN.findall(content)) & declarations.keys()):
                declarations[name]["uses"].append({"file": path.relative_to(source).as_posix(), "line": line})
    selected = {n for n, d in declarations.items() if d["uses"]}
    pending = list(selected)
    while pending:
        parent = pending.pop()
        for dep in set(TOKEN.findall(declarations[parent]["_code"])) & declarations.keys() - {parent}:
            declarations[dep]["required_by"].append(parent)
            if dep not in selected:
                selected.add(dep)
                pending.append(dep)
    symbols = []
    for name in sorted(selected):
        d = declarations[name]
        d.pop("_code")
        d["required_by"] = sorted(set(d["required_by"]))
        symbols.append(d)
    # pm4::kMemcpy and other opcode aliases are outside a6xx.hpp and must not
    # disappear from the generation audit just because that header omits them.
    pm4 = source / "include/hakios-metal/pm4.hpp"
    aliases = []
    for line, text in enumerate(pm4.read_text(encoding="utf-8").splitlines(), 1):
        m = re.match(r"\s*(k\w+)\s*=\s*(0x[\da-fA-F]+),\s*//\s*(CP_\w+)", text)
        if m:
            uses = []
            for path in files[1:]:
                for use_line, content in enumerate(strip_comments(path.read_text(encoding="utf-8")).splitlines(), 1):
                    if m[1] in TOKEN.findall(content):
                        uses.append({"file": path.relative_to(source).as_posix(), "line": use_line})
            aliases.append({"name": "pm4::" + m[1], "xml_name": m[3],
                            "value": int(m[2], 0), "file": "include/hakios-metal/pm4.hpp", "line": line,
                            "uses": uses})
    return {"scope": "constants sub-item only; no M0/device pass",
            "method": "comment/string-excluded lexical uses in src/*.cpp plus header dependency closure",
            "source_root": str(source), "files": {p.relative_to(source).as_posix(): sha(p) for p in files + [pm4]},
            "direct_symbols": sum(bool(d["uses"]) for d in symbols), "symbols": symbols, "pm4_aliases": aliases}


class Node:
    def __init__(self, tag, attrs, file, line):
        self.tag, self.attrs, self.file, self.line = tag, dict(attrs), file, line
        self.children = []

    def source(self):
        return {"file": self.file, "line": self.line, "tag": self.tag, "attrs": self.attrs,
                "path": getattr(self, "xml_path", [])}


def parse_xml(path, mesa):
    parser = xml.parsers.expat.ParserCreate()
    stack, roots = [], []

    def start(tag, attrs):
        node = Node(tag, attrs, path.relative_to(mesa).as_posix(), parser.CurrentLineNumber)
        node.xml_path = [{"tag": n.tag, "name": n.attrs.get("name")} for n in stack + [node]
                         if n.tag in ("domain", "array", "reg32", "reg64", "bitset")]
        (stack[-1].children if stack else roots).append(node)
        stack.append(node)

    parser.StartElementHandler = start
    parser.EndElementHandler = lambda tag: stack.pop()
    with path.open("rb") as f:
        parser.ParseFile(f)
    return roots[0]


def applies(text, generation):
    """Exact chip variant union/ranges, not Mesa gen_header's first-variant shortcut."""
    if not text:
        return True
    for part in text.split(","):
        part = part.strip()
        if "-" in part:
            lo, hi = part.split("-", 1)
            low = int(lo[1]) if lo else 2
            high = int(hi[1]) if hi else 7
            if low <= generation <= high:
                return True
        elif re.fullmatch(r"A[2-7]XX", part) and int(part[1]) == generation:
            return True
    return False


def children(node, tag):
    return [x for x in node.children if x.tag == tag]


def load_model(mesa, generation=5):
    regdir = mesa / "src/freedreno/registers/adreno"
    roots = [parse_xml(regdir / n, mesa) for n in
             ("adreno_common.xml", "adreno_pm4.xml", f"a{generation}xx.xml")]
    enums, items, fields, arrays, files, excluded = {}, {}, {}, {}, {}, []
    bitsets = {}
    opcode_rules = {}

    def insert(name, entry):
        if name in items:
            if any(items[name].get(k) != entry.get(k) for k in ("value", "cpp_type", "kind")):
                raise ValueError(f"conflicting applicable definition {name}: {items[name]} / {entry}")
            return
        items[name] = {"name": name, **entry}

    def scan_enums(node, eligible=True):
        eligible = eligible and applies(node.attrs.get("variants"), generation)
        if node.tag == "domain":
            name = node.attrs["name"]
            if re.match(r"A[2-7]XX", name) and not name.startswith(f"A{generation}XX"):
                eligible = False
            if name in opcode_rules:
                eligible = eligible and opcode_rules[name]["allowed"]
        if node.tag == "enum":
            vals = []
            for v in children(node, "value"):
                # Mesa 25.0.7 gen_header.py's absent value attribute defaults
                # to zero, not C/C++ auto-increment (INDEX_SIZE_INVALID).
                value = int(v.attrs.get("value", "0"), 0)
                allowed = eligible and applies(v.attrs.get("variants"), generation)
                if node.attrs["name"] == "adreno_pm4_type3_packets":
                    opcode_rules[v.attrs["name"]] = {
                        "name": v.attrs["name"], "value": value, "allowed": allowed,
                        "variants": v.attrs.get("variants", "all"), "source": v.source()}
                if allowed:
                    vals.append(v.attrs["name"])
                    insert(v.attrs["name"], {"kind": "enum_value", "cpp_type": node.attrs["name"],
                                            "value": value, "source": v.source(),
                                            "value_origin": "explicit XML" if "value" in v.attrs else "Mesa 25.0.7 absent-value default zero",
                                            "enum_source": node.source()})
            if vals:
                enums[node.attrs["name"]] = {"values": vals, "source": node.source()}
        if node.tag == "bitset":
            bitsets[node.attrs["name"]] = node
        for c in node.children:
            scan_enums(c, eligible)

    for root in roots:
        scan_enums(root)
        files[root.file] = sha(mesa / root.file)

    def emit_field(owner, f, bits, reg):
        a = f.attrs
        low = int(a.get("pos", a.get("low", "0")), 0)
        high = int(a.get("pos", a.get("high", str(bits - 1))), 0)
        shr = int(a.get("shr", "0"), 0)
        radix = int(a.get("radix", "0"), 0)
        typ = a.get("type", "uint")
        if not 0 <= low <= high < bits or not 0 <= shr < 64:
            raise ValueError(f"invalid field {owner}: {f.source()}")
        if typ not in enums and typ not in ("uint", "hex", "int", "boolean", "a3xx_regid",
                                             "address", "waddress", "fixed", "ufixed", "float"):
            raise ValueError(f"unknown XML type {typ} for {owner}")
        name = owner + ("_" + a["name"] if a.get("name") else "")
        cpp_type = (typ if typ in enums else {"boolean": "bool", "int": "int32_t",
                    "address": "uint64_t", "waddress": "uint64_t",
                    "float": "float", "fixed": "float", "ufixed": "float"}.get(typ, "uint32_t"))
        mask = ((1 << (high - low + 1)) - 1) << low
        value_type = "uint64_t" if bits == 64 else "uint32_t"
        spec = {"name": name, "xml_type": typ, "cpp_type": cpp_type,
                "low": low, "high": high, "shr": shr, "radix": radix, "bits": bits,
                "mask": mask, "source": f.source(), "owner_source": reg.source()}
        if name in fields and fields[name] != spec:
            raise ValueError(f"field name collision {name}")
        fields[name] = spec
        if typ == "boolean" or (a.get("type") is None and low == high):
            insert(name, {"kind": "flag", "cpp_type": value_type, "value": mask,
                          "source": f.source(), "owner_source": reg.source()})
            spec["flag"] = True
        for suffix, value in (("__MASK", mask), ("__SHIFT", low), ("__SCALE", shr),
                              ("__WIDTH", high - low + 1)):
            insert(name + suffix, {"kind": "field" + suffix.lower(), "cpp_type": value_type,
                                  "value": value, "source": f.source(), "owner_source": reg.source()})
        if typ in ("fixed", "ufixed"):
            insert(name + "__RADIX", {"kind": "field__radix", "cpp_type": "uint32_t",
                                     "value": radix, "source": f.source(), "owner_source": reg.source()})
            insert(name + "__MUL", {"kind": "field__mul", "cpp_type": "uint32_t",
                                   "value": 1 << radix, "source": f.source(), "owner_source": reg.source()})

    def walk(node, domain=None, prefix=None, base=0, strides=(), lengths=(), path=(), eligible=True):
        a = node.attrs
        eligible = eligible and applies(a.get("variants"), generation)
        if node.tag == "domain":
            domain = a["name"]
            if re.match(r"A[2-7]XX", domain) and not domain.startswith(f"A{generation}XX"):
                eligible = False
            if domain in opcode_rules:
                eligible = eligible and opcode_rules[domain]["allowed"]
            prefix = None
            if a.get("prefix"):
                prefix = (a.get("variants", "").split(",")[0].split("-")[0]) or None
        elif node.tag == "stripe" and a.get("variants"):
            prefix = a["variants"].split(",")[0].split("-")[0]
        if not eligible:
            excluded.append(node.source())
            return
        owner = ((prefix + "_") if prefix else "") + (domain or "")
        if node.tag == "bitset":
            if a.get("inline") != "yes":
                for f in children(node, "bitfield"):
                    emit_field(a["name"], f, 32, node)
            return
        if node.tag == "array":
            if any(k in a for k in ("offsets", "doffsets", "index")):
                raise ValueError("non-linear array requires explicit support: " + str(node.source()))
            base += int(a["offset"], 0)
            length = int(a["length"], 0)
            if length != 1:
                strides += (int(a["stride"], 0),)
                lengths += (length,)
            path += (a.get("name", ""),)
            name = "REG_" + owner + "_" + "_".join(x for x in path if x)
            arrays[name] = {"name": name, "kind": "array", "base": base,
                            "strides": strides, "lengths": lengths, "source": node.source()}
        if node.tag in ("reg32", "reg64"):
            bits = 32 if node.tag == "reg32" else 64
            offset = base + int(a["offset"], 0)
            local = "_".join(x for x in path + (a["name"],) if x)
            full = owner + "_" + local
            name = "REG_" + full
            rs, rl = strides, lengths
            if "stride" in a:
                rs += (int(a["stride"], 0),)
                rl += (int(a["length"], 0),)
            if rs:
                arrays[name] = {"name": name, "kind": "register_array", "base": offset,
                                "strides": rs, "lengths": rl, "source": node.source()}
            else:
                insert(name, {"kind": "register", "cpp_type": "uint32_t", "value": offset,
                              "bits": bits, "xml_type": a.get("type"), "source": node.source()})
            if "align" in a:
                insert(name + "__ALIGN", {"kind": "alignment", "cpp_type": "uint32_t",
                                         "value": int(a["align"], 0), "source": node.source()})
            if a.get("type") in bitsets:
                b = bitsets[a["type"]]
                fs = children(b, "bitfield") if b.attrs.get("inline") == "yes" else []
                fs += children(node, "bitfield")
            elif children(node, "bitfield"):
                fs = children(node, "bitfield")
            elif a.get("type"):
                fs = [Node("implicit_field", {k: v for k, v in a.items()
                                             if k in ("type", "shr", "radix")}, node.file, node.line)]
            else:
                fs = []
            for f in fs:
                if applies(f.attrs.get("variants"), generation):
                    emit_field(full, f, bits, node)
            return
        for c in node.children:
            if c.tag not in ("enum", "doc", "value", "bitfield"):
                walk(c, domain, prefix, base, strides, lengths, path, eligible)

    for root in roots:
        walk(root)
    for n, a in arrays.items():
        # Preserve Mesa's function-style array API; provide first/stride/count
        # separately, rather than pretending an indexed register is a scalar.
        for suffix, val in (("__BASE", a["base"]), ("__STRIDE", a["strides"][-1] if a["strides"] else 0),
                            ("__LENGTH", a["lengths"][-1] if a["lengths"] else 1)):
            insert(n + suffix, {"kind": "array" + suffix.lower(), "cpp_type": "uint32_t",
                               "value": val, "source": a["source"]})
    return {"generation": generation, "enums": enums, "items": items, "fields": fields,
            "arrays": arrays, "xml_files": files, "opcode_rules": opcode_rules}


PREAMBLE = """// Generated by tools/generate-a5xx.py from official Mesa 25.0.7 XML (MIT).
// HOST ONLY: constants sub-item, not M0 acceptance or hardware validation.
// Register addresses are DWORD indices, not byte offsets.
// No A6XX aliases, A635 chip facts, firmware, or device access.
// Every numeric definition has XML provenance in constants-manifest.json.
// Encoders preserve XML types and shifts; callers must validate logical
// dimensions, range, and alignment before constructing a command stream.
#pragma once
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace hakios::metal::a5xx {
namespace detail {
inline uint32_t floatBits(float value) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
// IEEE binary16, round-to-nearest-even, preserving infinities and quiet NaNs.
inline uint16_t floatHalf(float value) {
    const uint32_t b = floatBits(value), sign = (b >> 16) & 0x8000u;
    const uint32_t exp = (b >> 23) & 255u, mant = b & 0x7fffffu;
    if (exp == 255) return uint16_t(sign | 0x7c00u | (mant ? 0x200u : 0u));
    if (exp > 142) return uint16_t(sign | 0x7c00u);
    if (exp < 102) return uint16_t(sign);
    const uint32_t shift = exp < 113 ? 126 - exp : 13;
    const uint32_t raw = exp < 113 ? (mant | 0x800000u) : mant;
    const uint32_t round = (uint32_t(1) << (shift - 1)) - 1;
    const uint32_t rounded = (raw + round + ((raw >> shift) & 1u)) >> shift;
    return uint16_t(sign | (exp < 113 ? rounded : (((exp - 112) << 10) + rounded)));
}
inline uint32_t fixedBits(float value, uint32_t radix, bool signedValue) {
    const double scaled = std::ldexp(double(value), int(radix));
    const bool valid = std::isfinite(scaled) &&
        (signedValue ? scaled >= -2147483648.0 && scaled < 2147483648.0
                     : scaled >= 0.0 && scaled < 4294967296.0);
    assert(valid);
    if (!valid) return 0;  // keep NDEBUG builds free of float-to-integer UB
    return signedValue ? uint32_t(int32_t(scaled)) : uint32_t(scaled);
}
}  // namespace detail
"""


def hexval(value, typ):
    return f"0x{value:0{16 if typ == 'uint64_t' else 8}x}{'ull' if typ == 'uint64_t' else 'u'}"


def render_header(model):
    out = [PREAMBLE]
    enum_values = set()
    for name, enum in sorted(model["enums"].items()):
        out.append(f"enum {name} : uint32_t {{")
        for v in enum["values"]:
            item = model["items"][v]
            src = item["source"]
            out.append(f"    {v} = {hexval(item['value'], 'uint32_t')}, // {src['file']}:{src['line']}")
            enum_values.add(v)
        out.append("};\n")
    for name, item in sorted(model["items"].items()):
        if name in enum_values:
            continue
        src = item["source"]
        out.append(f"inline constexpr {item['cpp_type']} {name} = {hexval(item['value'], item['cpp_type'])};"
                   f" // {src['file']}:{src['line']} [{item['kind']}]")
    out.append("")
    for name, a in sorted(model["arrays"].items()):
        args = ", ".join(f"uint32_t i{i}" for i in range(len(a["strides"])))
        val = hexval(a["base"], "uint32_t")
        for i, stride in enumerate(a["strides"]):
            val += f" + {hexval(stride, 'uint32_t')} * i{i}"
        out.append(f"constexpr uint32_t {name}({args}) {{ return {val}; }}")
    for name, f in sorted(model["fields"].items()):
        if f.get("flag"):
            continue
        rettype = "uint64_t" if f["bits"] == 64 else "uint32_t"
        typ = f["xml_type"]
        if typ == "float":
            val = "detail::floatBits(value)" if f["high"] - f["low"] == 31 else "detail::floatHalf(value)"
        elif typ in ("fixed", "ufixed"):
            val = f"detail::fixedBits(value, {f['radix']}u, {'true' if typ == 'fixed' else 'false'})"
        else:
            val = "value"
        qualifier = "inline" if f["cpp_type"] == "float" else "constexpr"
        out.append(f"{qualifier} {rettype} {name}({f['cpp_type']} value) {{")
        out.append(f"    return {rettype}((uint64_t({val}) >> {name}__SCALE) << {name}__SHIFT) & {name}__MASK;")
        out.append("}")
    allowed = sorted({r["value"] for r in model["opcode_rules"].values() if r["allowed"]})
    out += ["", "// Generation gate from adreno_pm4_type3_packets value variants.",
            "// Numeric recognition only: payload size and field validation remain mandatory.",
            "constexpr bool opcodeAvailable(uint32_t opcode) {", "    switch (opcode) {"]
    out += [f"    case 0x{n:02x}u:" for n in allowed]
    out += ["        return true;", "    default: return false;", "    }", "}",
            "}  // namespace hakios::metal::a5xx", ""]
    return "\n".join(out)


def file_ref(mesa, relative, needle):
    path = mesa / relative
    for line, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if needle in text:
            return {"file": relative, "line": line, "text": text.strip()}
    raise ValueError(f"required source anchor missing: {relative}: {needle}")


def format_rows(mesa, generation):
    path = ("src/freedreno/fdl/fd6_format_table.c" if generation == 6 else
            "src/gallium/drivers/freedreno/a5xx/fd5_format.c")
    rows = {}
    for line, text in enumerate((mesa / path).read_text(encoding="utf-8").splitlines(), 1):
        m = re.match(r"\s*(FMT|VTC|_TC|_T_|VT_|V__|VT|_T|V_)\(([^)]+)\),", text)
        if not m:
            continue
        macro, args = m[1], [x.strip() for x in m[2].split(",")]
        if generation == 6:
            if macro == "FMT":
                pipe, vtx, tex, rb, swap = args
            else:
                pipe, fmt, swap = args
                vtx = fmt if macro in ("VTC", "VT_", "V__") else "NONE"
                tex = fmt if macro in ("VTC", "_TC", "_T_", "VT_") else "NONE"
                rb = fmt if macro in ("VTC", "_TC") else "NONE"
            roles = {"vertex": "FMT6_" + vtx, "texture": "FMT6_" + tex, "color": "FMT6_" + rb}
        else:
            pipe, fmt, rb, swap = args
            roles = {"vertex": "VFMT5_" + (fmt if macro in ("VT", "V_") else "NONE"),
                     "texture": "TFMT5_" + (fmt if macro in ("VT", "_T") else "NONE"), "color": "RB5_" + rb}
        rows[pipe] = {"roles": roles, "swap": swap, "source": {"file": path, "line": line}}
    return rows


def symbol_view(name, model):
    if name in model["items"]:
        return model["items"][name]
    if name in model["fields"]:
        return {"kind": "encoder", **model["fields"][name]}
    if name in model["enums"]:
        return {"name": name, "kind": "enum_type", **model["enums"][name]}
    if name in model["arrays"]:
        return model["arrays"][name]
    # HakiOS flattens indexed Mesa registers to element zero.
    if name.endswith("__STRIDE") and name[:-8] in model["arrays"]:
        a = model["arrays"][name[:-8]]
        return {"name": name, "kind": "array_stride", "value": a["strides"][-1], "source": a["source"]}
    return None


def semantic_key(entry):
    """Join XML node identities, not C++ spelling or equal numeric values."""
    if not entry or entry.get("kind") in ("enum_type", "enum_value"):
        return None
    source = entry.get("owner_source", entry["source"])
    if source["tag"] not in ("reg32", "reg64", "array"):
        return None
    # Only register-space identities; packet payloads are mapped by explicit
    # generation-specific rules below (LOAD_STATE changes word placement).
    if not re.match(r"src/freedreno/registers/adreno/a[56]xx.xml$", source["file"]):
        return None
    path = source.get("path", [])
    domains = [n["name"] for n in path if n["tag"] == "domain"]
    if not domains:
        return None
    role = {"A5XX": "register-file", "A6XX": "register-file",
            "A5XX_TEX_CONST": "sampled-texture-descriptor", "A6XX_TEX_CONST": "sampled-texture-descriptor",
            "A5XX_TEX_SAMP": "sampler-descriptor", "A6XX_TEX_SAMP": "sampler-descriptor"}.get(domains[-1])
    if role is None:
        return None
    register = tuple((n["tag"], n["name"]) for n in path if n["tag"] in ("array", "reg32", "reg64"))
    field = entry["source"]["attrs"].get("name") if entry["source"]["tag"] == "bitfield" else None
    return role, register, field, entry["kind"]


def make_differences(inv, new, old, mesa):
    fd5 = "src/gallium/drivers/freedreno/a5xx/"
    anchors = {
        "compute": file_ref(mesa, fd5 + "fd5_compute.c", "cs_program_emit"),
        "texture": file_ref(mesa, fd5 + "fd5_emit.c", "12 * num_textures"),
        "image": file_ref(mesa, fd5 + "fd5_image.c", "static enum a4xx_state_block imgsb"),
        "ssbo": file_ref(mesa, fd5 + "fd5_emit.c", "SB4_CS_SSBO"),
        "render": file_ref(mesa, fd5 + "fd5_program.c", "fd5_program_emit"),
        "gmem": file_ref(mesa, fd5 + "fd5_gmem.c", "fd5_emit_tile_init"),
        "blit": file_ref(mesa, fd5 + "fd5_blitter.c", "CP_BLIT"),
        "chip": file_ref(mesa, "src/freedreno/common/freedreno_devices.py", "GPUId(530)"),
        "pm4": file_ref(mesa, "src/freedreno/common/freedreno_pm4.h", "pm4_pkt7_hdr"),
    }
    index = {}
    for name in list(new["items"]) + list(new["fields"]) + list(new["arrays"]):
        e = symbol_view(name, new)
        k = semantic_key(e)
        if k:
            index.setdefault(k, []).append(name)
    # Explicit semantic replacements. These are documentation candidates only,
    # never aliases in the generated header or authorization to reuse an emitter.
    explicit = {
        "CP_LOAD_STATE6": ("CP_LOAD_STATE4", "Rewrite packet as LOAD_STATE4; STATE_TYPE moves from payload word 0 to word 1."),
        "CP_LOAD_STATE6_FRAG": ("CP_LOAD_STATE4", "A5xx has one LOAD_STATE4 opcode, not A6xx fragment/geometry variants."),
        "CP_LOAD_STATE6_GEOM": ("CP_LOAD_STATE4", "A5xx has one LOAD_STATE4 opcode, not A6xx fragment/geometry variants."),
        "CP_LOAD_STATE6_0_STATE_TYPE": ("CP_LOAD_STATE4_1_STATE_TYPE", "STATE_TYPE moves from payload word 0 bits 14..15 to word 1 bits 0..1."),
        "CP_LOAD_STATE6_0_DST_OFF": ("CP_LOAD_STATE4_0_DST_OFF", "Use LOAD_STATE4 payload and its a4xx_state_* enum types."),
        "CP_LOAD_STATE6_0_STATE_SRC": ("CP_LOAD_STATE4_0_STATE_SRC", "SS4_INDIRECT is 2; use a4xx_state_src, reject unsupported source modes."),
        "CP_LOAD_STATE6_0_STATE_BLOCK": ("CP_LOAD_STATE4_0_STATE_BLOCK", "Select SB4 stage and resource class; SSBO is a separate state block."),
        "CP_LOAD_STATE6_0_NUM_UNIT": ("CP_LOAD_STATE4_0_NUM_UNIT", "Units depend on the A5xx resource type, not a uniform A6xx descriptor size."),
        "CP_LOAD_STATE6_1_EXT_SRC_ADDR": ("CP_LOAD_STATE4_1_EXT_SRC_ADDR", "Payload word 1 combines aligned address with STATE_TYPE."),
        "CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI": ("CP_LOAD_STATE4_2_EXT_SRC_ADDR_HI", "Use the A5xx 64-bit LOAD_STATE4 layout."),
        "ST6_SHADER": ("ST4_SHADER", "A5xx shader or sampler load; stage/resource block must be SB4."),
        "ST6_CONSTANTS": ("ST4_CONSTANTS", "A5xx constant or texture load; texture unit is 12 dwords."),
        "ST6_UBO": ("ST4_UBO", "Use fd5_emit_ubos and its 2-dword units; shader lowering must match A5xx."),
        "SS6_DIRECT": ("SS4_DIRECT", "Rewrite LOAD_STATE payload, not just enum spelling."),
        "SS6_INDIRECT": ("SS4_INDIRECT", "Rewrite LOAD_STATE payload, not just enum spelling."),
        "THREAD64": ("TWO_QUADS", "Use a3xx_threadsize, A5xx bit positions and compiler metadata; no A635 occupancy model."),
        "THREAD128": ("FOUR_QUADS", "Use a3xx_threadsize and fd5_compute register allocation constraints."),
        "REG_A6XX_SP_CS_OBJ_START": ("REG_A5XX_SP_CS_OBJ_START_LO", "A5xx has separate untyped LO/HI registers; two-word write requires explicit adjacency check."),
        "REG_A6XX_SP_CS_INSTRLEN": ("REG_A5XX_HLSQ_CS_INSTRLEN", "A5xx instruction length is in HLSQ, with a separate preload length."),
        "REG_A6XX_SP_CS_TEX_CONST": ("REG_A5XX_TPL1_CS_TEX_CONST_LO", "A5xx texture pointers belong to TPL1, not SP."),
        "REG_A6XX_SP_CS_TEX_SAMP": ("REG_A5XX_TPL1_CS_TEX_SAMP_LO", "A5xx sampler pointers belong to TPL1, not SP."),
        "REG_A6XX_SP_CS_TEX_COUNT": ("REG_A5XX_TPL1_CS_TEX_COUNT", "Use fd5_emit_cs_state counts and A5xx texture/image bindings."),
        "REG_A6XX_SP_TP_BORDER_COLOR_BASE_ADDR": ("REG_A5XX_TPL1_TP_BORDER_COLOR_BASE_ADDR_LO", "Use the A5xx border-color table layout; no assumed second PS TP base."),
        "REG_A6XX_SP_CS_PVT_MEM_HW_STACK_OFFSET": ("REG_A5XX_SP_CS_PVT_MEM_PARAM", "HWSTACKOFFSET is packed into PVT_MEM_PARAM on A5xx; unit is shr=11."),
        "a6xx_threadsize": ("a3xx_threadsize", "A5xx uses TWO_QUADS/FOUR_QUADS and different control-word bit positions."),
        "a6xx_state_block": ("a4xx_state_block", "A5xx uses SB4 including dedicated CS_SSBO."),
        "a6xx_state_src": ("a4xx_state_src", "LOAD_STATE4 source mode; no A6xx bindless assumptions."),
        "a6xx_state_type": ("a4xx_state_type", "No ST4_IBO; use the explicit fd5 image/SSBO path."),
    }
    for stage in ("VS", "HS", "DS", "GS", "FS", "CS"):
        for resource in ("TEX", "SHADER"):
            explicit[f"SB6_{stage}_{resource}"] = (
                f"SB4_{stage}_{resource}", "Same stage/resource role in XML; opcode and word1 STATE_TYPE must still be rewritten.")
    enum_roles = {
        "a6xx_tex_type": ("a5xx_tex_type", ["1D", "2D", "CUBE", "3D", "BUFFER"]),
        "a6xx_tex_swiz": ("a5xx_tex_swiz", ["X", "Y", "Z", "W", "ZERO", "ONE"]),
        "a6xx_tex_filter": ("a5xx_tex_filter", ["NEAREST", "LINEAR", "ANISO"]),
        "a6xx_tex_clamp": ("a5xx_tex_clamp", ["REPEAT", "CLAMP_TO_EDGE", "MIRROR_REPEAT", "CLAMP_TO_BORDER", "MIRROR_CLAMP"]),
        "a6xx_tex_aniso": ("a5xx_tex_aniso", ["ANISO_1", "ANISO_2", "ANISO_4", "ANISO_8", "ANISO_16"]),
    }
    for old_type, (new_type, values) in enum_roles.items():
        explicit[old_type] = (new_type, "Same texture/sampler semantic role; preserve the A5xx XML enum type and field layout.")
        for value in values:
            explicit["A6XX_TEX_" + value] = ("A5XX_TEX_" + value, "Explicit texture/sampler semantic role, verified in both XML enum definitions.")
    explicit["a6xx_tile_mode"] = ("a5xx_tile_mode", "Equal layout enum values do not establish equal tiling implementation or UBWC metadata.")
    for mode in ("LINEAR", "2", "3"):
        explicit["TILE6_" + mode] = ("TILE5_" + mode, "Recompute layout with A5xx fdl5; do not reuse A6xx tile addressing or UBWC metadata.")
    by_pipe5, by_pipe6 = format_rows(mesa, 5), format_rows(mesa, 6)
    rows = []
    for used in inv["symbols"]:
        name = used["name"]
        old_entry = symbol_view(name, old)
        row = {"symbol": name, "hakios": used, "a6xx_xml": old_entry,
               "status": "missing-requires-reimplementation", "a5xx": [], "sources": []}
        # Format equivalence is a join on PIPE_FORMAT plus role in the two Mesa
        # format tables. A8, packed RGB10A2 and vertex-only formats prove why
        # FMT6_ -> TFMT5_/RB5_ text substitution is not sufficient.
        if name.startswith("FMT6_"):
            for pipe, r6 in by_pipe6.items():
                if pipe not in by_pipe5:
                    continue
                r5 = by_pipe5[pipe]
                for role, symbol in r6["roles"].items():
                    if symbol == name:
                        candidate = r5["roles"][role]
                        row["a5xx"].append({"symbol": candidate, "role": role, "pipe_format": pipe,
                                           "xml": symbol_view(candidate, new),
                                           "swap6": r6["swap"], "swap5": r5["swap"],
                                           "supported": not candidate.endswith("_NONE"),
                                           "format_table6": r6["source"], "format_table5": r5["source"]})
            row["status"] = "format-role-split" if row["a5xx"] else "format-unavailable-reject-or-convert"
            row["action"] = "Use PIPE_FORMAT + vertex/texture/color role and fd5 swizzle, not numeric format identity. Reject unsupported roles."
            if not row["a5xx"]:
                row["action"] = ("No combined format counterpart in the fd5 table. Reject this combined format in the initial port; "
                                 "plane splitting/conversion requires a separately implemented and validated A5xx texture/shader path.")
                row["sources"] = [file_ref(mesa, fd5 + "fd5_format.c", "VT(R8_UNORM"), anchors["texture"]]
                if old_entry:
                    row["sources"].append(old_entry["source"])
        elif name in explicit or name.split("__")[0] in explicit:
            base, sep, suffix = name.partition("__")
            target, note = explicit.get(name, explicit.get(base))
            if sep and name not in explicit:
                target += sep + suffix
            entry = symbol_view(target, new)
            if entry is not None:
                row["a5xx"] = [{"symbol": target, "xml": entry}]
                row["status"] = "explicit-semantic-rewrite"
            row["action"] = note
            row["sources"] = [anchors["compute"], anchors["texture"]]
        elif name in new["items"] or name in new["fields"] or name in new["enums"] or name in new["arrays"]:
            row["a5xx"] = [{"symbol": name, "xml": symbol_view(name, new)}]
            row["status"] = "shared-xml-definition"
            row["action"] = "Use exact XML definition/type; payload semantics and resource layout still follow A5xx emitters."
        elif old_entry and semantic_key(old_entry) in index:
            candidates = sorted(set(index[semantic_key(old_entry)]))
            # Array names such as FETCH/DECODE repeat across blocks. Do not
            # silently choose an ambiguous match.
            if len(candidates) == 1 and "UNKNOWN" not in name and "_UNK" not in name:
                target = candidates[0]
                row["a5xx"] = [{"symbol": target, "xml": symbol_view(target, new)}]
                row["status"] = "xml-register-field-counterpart"
                row["action"] = ("XML owner register and field identities match. Compare listed width, shift, scale, type and register address; "
                                 "rebuild the A5xx emitter from fd5 sources. This is not a drop-in alias or a behavior proof.")
                row["sources"] = [anchors["compute"] if "_CS_" in name else anchors["texture"] if "_TEX_" in name else anchors["render"]]
        if "action" not in row:
            if used["kind"] == "helper" or used["kind"] == "struct":
                row["status"] = "helper-or-layout-rewrite"
                row["action"] = "Reimplement against the listed dependency rows and the A5xx emitter; do not copy the A6xx helper body/layout."
            elif name.startswith("k"):
                row["status"] = "a635-policy-not-transferable"
                row["action"] = ("A635 software/chip policy, not an XML register constant. Re-derive for the selected A530 profile from "
                                 "freedreno_devices.py, compiler metadata and fd5 emitters; unspecified values must remain unsupported.")
            else:
                row["action"] = ("No proven one-to-one applicable XML counterpart. Reject the feature in the initial port; "
                                 "implement from the cited fd5 compute/texture/image/render path before enabling it.")
            row["sources"] = [anchors["compute"] if "CS_" in name or "SP_" in name else
                              anchors["texture"] if "TEX" in name else anchors["render"]]
        if not row["a5xx"] and ("IBO" in name or name == "ST6_IBO"):
            row["action"] = "No A6xx IBO table reuse: lower/repack image and SSBO bindings with SB4_CS_SSBO or SB4_SSBO and A5XX_SSBO_*."
            row["sources"] = [anchors["image"], anchors["ssbo"]]
        if "A6XX_CP_SET_MARKER" in name or name.startswith("RM6_") or name == "CP_SET_MARKER":
            row["a5xx"] = []
            row["status"] = "unavailable-generation"
            row["action"] = "CP_SET_MARKER is A6XX+; omit the packet and implement A5xx binning/sysmem sequencing from fd5_gmem.c."
            row["sources"] = [new["opcode_rules"]["CP_SET_MARKER"]["source"], anchors["gmem"]]
        if name in new["opcode_rules"] and not new["opcode_rules"][name]["allowed"] and name not in explicit:
            row["status"] = "unavailable-generation"
            row["sources"].append(new["opcode_rules"][name]["source"])
        if old_entry and row["a5xx"] and len(row["a5xx"]) == 1:
            target = row["a5xx"][0].get("xml")
            if target:
                row["changes"] = {k: {"a6xx": old_entry.get(k), "a5xx": target.get(k)}
                                  for k in ("value", "cpp_type", "xml_type", "bits", "low", "high", "shr", "radix", "mask",
                                            "base", "strides", "lengths")
                                  if old_entry.get(k) != target.get(k)}
        rows.append(row)
    opcode_rows = []
    used_names = {d["name"] for d in inv["symbols"]} | {d["xml_name"] for d in inv["pm4_aliases"]}
    for name in sorted(used_names & new["opcode_rules"].keys()):
        r = dict(new["opcode_rules"][name])
        r["hakios_aliases"] = [a for a in inv["pm4_aliases"] if a["xml_name"] == name]
        r["hakios_symbol_uses"] = [d["uses"] for d in inv["symbols"] if d["name"] == name]
        r["action"] = ("allowed opcode number; validate A5xx payload" if r["allowed"] else "reject")
        if name == "CP_MEMCPY":
            r["action"] = ("reject CP_MEMCPY (A6XX+). Reimplement buffer copy/fill with a validated A5xx blit path or "
                           "host-visible CPU copy. CP_MEM_TO_MEM is a different operation/payload, not a bulk memcpy alias.")
        if name.startswith("CP_LOAD_STATE6"):
            r["action"] = "replace with CP_LOAD_STATE4 and rebuild payload; STATE_TYPE is in word1"
        opcode_rows.append(r)
    counts = {}
    for row in rows:
        counts[row["status"]] = counts.get(row["status"], 0) + 1
    source_files = set(old["xml_files"])
    source_files.update(a["file"] for a in anchors.values())
    source_files.update(("src/freedreno/fdl/fd6_format_table.c", fd5 + "fd5_format.c"))
    return {"scope": "host-only constants sub-item; no full M0/device acceptance",
            "coverage": {"inventory": len(inv["symbols"]), "rows": len(rows), "statuses": counts},
            "reference_anchors": anchors, "reference_hashes": {f: sha(mesa / f) for f in sorted(source_files)},
            "rows": rows, "opcode_rules": opcode_rows}


def write_differences(folder, report):
    write_json(folder / "a6xx-to-a5xx-differences.json", report)
    out = ["# HakiOS A6xx → A5xx constants differences", "",
           "Host-only constants sub-item. M0 is not accepted by this report.",
           "Every inventory symbol has a row; JSON contains all use sites, original declarations, exact XML types, values and field layouts.",
           "A structural counterpart requires emitter reconstruction; it is not an alias or a hardware behavior claim.", "",
           "| Used symbol | Status | A5xx counterpart(s) | Source | Action |",
           "|---|---|---|---|---|"]
    for row in report["rows"]:
        targets = sorted({a["symbol"] for a in row["a5xx"]})
        refs = []
        for a in row["a5xx"]:
            if a.get("xml"):
                s = a["xml"]["source"]
                refs.append(f"{s['file']}:{s['line']}")
        for s in row["sources"]:
            refs.append(f"{s['file']}:{s['line']}")
        used = row["hakios"]["declaration"]
        source = f"HakiOS {used['file']}:{used['line']}; " + "; ".join(sorted(set(refs)))
        out.append(f"| `{row['symbol']}` | {row['status']} | {', '.join('`'+t+'`' for t in targets) or 'missing / see action'}"
                   f" | {source} | {row['action'].replace('|', '/')} |")
    out += ["", "## Opcode generation rules", "",
            "| Opcode | Value | XML variants | A5xx | Source | Required action |",
            "|---|---:|---|---|---|---|"]
    for r in report["opcode_rules"]:
        s = r["source"]
        out.append(f"| `{r['name']}` | `0x{r['value']:02x}` | {r['variants']} | {'yes' if r['allowed'] else 'no'}"
                   f" | {s['file']}:{s['line']} | {r['action']} |")
    (folder / "a6xx-to-a5xx-differences.md").write_text("\n".join(out) + "\n", encoding="utf-8")


def write_implementation_spec(folder, mesa, report, model):
    fd5 = "src/gallium/drivers/freedreno/a5xx/"
    refs = {
        "compute_program": file_ref(mesa, fd5 + "fd5_compute.c", "cs_program_emit"),
        "preload": file_ref(mesa, fd5 + "fd5_compute.c", "if (instrlen > 32)"),
        "constlen": file_ref(mesa, fd5 + "fd5_compute.c", "unsigned constlen = v->constlen / 4"),
        "workgroup": file_ref(mesa, fd5 + "fd5_compute.c", "A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEX"),
        "indirect": file_ref(mesa, fd5 + "fd5_compute.c", "CP_EXEC_CS_INDIRECT, 4"),
        "sampler_load": file_ref(mesa, fd5 + "fd5_emit.c", "4 * tex->num_samplers"),
        "texture_load": file_ref(mesa, fd5 + "fd5_emit.c", "12 * num_textures"),
        "ssbo": file_ref(mesa, fd5 + "fd5_emit.c", "Unlike a6xx, SSBO size is in bytes"),
        "image": file_ref(mesa, fd5 + "fd5_image.c", "static enum a4xx_state_block imgsb"),
        "pitchalign": file_ref(mesa, fd5 + "fd5_texture.c", "rsc->layout.pitchalign - 6"),
        "buffer_texture": file_ref(mesa, fd5 + "fd5_texture.c", "elements & MASK(15)"),
        "border": file_ref(mesa, fd5 + "fd5_emit.c", "REG_A5XX_TPL1_TP_BORDER_COLOR_BASE_ADDR_LO"),
        "blit_mode": file_ref(mesa, fd5 + "fd5_blitter.c", "CP_SET_RENDER_MODE_0_MODE(BLIT2D)"),
        "blit_packet": file_ref(mesa, fd5 + "fd5_blitter.c", "CP_BLIT, 5"),
        "gmem": report["reference_anchors"]["gmem"],
        "render": report["reference_anchors"]["render"],
        "chip_policy": report["reference_anchors"]["chip"],
    }

    def ref(key):
        r = refs[key]
        return f"`{r['file']}:{r['line']}`"

    opcode_xml = model["opcode_rules"]["CP_MEMCPY"]["source"]
    text = f"""# A5xx constants implementation contract

This is a host-only constants sub-item for the independent A5xx port. It does not accept M0 as a whole, execute a GPU command, establish A530 chip identity, validate firmware, prove GPU power or IOMMU ownership, or extend any device authorization.

## Inputs and reproducibility

Official Mesa **25.0.7**, archive SHA256 `{RELEASE_SHA256}` (verified by the main agent against official release notes). Generation uses `adreno_common.xml`, `adreno_pm4.xml`, `a5xx.xml`. The A6xx comparison separately reads `a6xx.xml` and both Gallium format tables. The frozen HakiOS `include/hakios-metal/a6xx.hpp` and `src/*.cpp` are read-only inputs; their SHA256 values are in `hakios-usage-inventory.json`.

`generate-a5xx.py` produces the header, XML manifest, inventory and differences. It parses namespace-free Expat node identities with exact file/line metadata and applies chip variant unions/ranges. XML inline bitsets are expanded for each owning register, including any extra fields declared on that register. Array register functions retain index parameters; `__BASE`, `__STRIDE`, `__LENGTH` are metadata, not a substitute for the index API. Registers are **DWORD indices**; a future MMIO address calculation must multiply by four with checked arithmetic.

`check-a5xx.py` has a separate XML reader and does not import the generator. It reconstructs names, values, types, widths, scales, bitset ownership and source locations. `--mesa-witness` also runs Mesa's unmodified `gen_header.py` into the evidence directory. Mesa's generated C header combines multiple chip variants, so that numeric witness must never supply A5xx opcode availability. `opcodeAvailable` is only a generation check; payload size, register permissions and field legality must be checked separately.

Coverage: **{report['coverage']['rows']}** rows, one for every selected HakiOS symbol, including helper/type dependencies; `pm4.hpp` aliases are independently listed, including `pm4::kMemcpy`. The lexical inventory excludes comments and strings, preserving source line numbers; it is a conservative dependency inventory rather than a C++ execution analysis. Every row with a structural counterpart still requires an A5xx emitter. A missing row's feature must be rejected until its cited alternative has been implemented and tested.

## Compute contract

Use {ref('compute_program')} as the emission order. A5xx programs select `TWO_QUADS`/`FOUR_QUADS` (`a3xx_threadsize`), set the HLSQ thread controls and SP footprints, enable the separate HLSQ/SP CS configurations, set HLSQ constant/instruction lengths and the shader object LO/HI registers, update HLSQ state, then set WGID/local regids. XML footprints are six-bit fields at shifts 4 and 10; THREADSIZE is bit 3; branch stack is bits 25..31. Range checks must occur before packing `max_reg + 1`, `max_half_reg + 1` and compiler-derived branch stack. No A635 merged-register, LPAC, early-preamble or occupancy assumption is inherited.

Shader preload is **zero when `instrlen > 32`**, following {ref('preload')}; longer shader objects still have an address, so zero preload must not be interpreted as zero program length. Do not carry the A635 preload cap 127 or its measured FS-length workaround into A5xx. Constant length must be a multiple of four vec4s and the A5xx HLSQ length uses `v->constlen / 4` ({ref('constlen')}). The policy is separate from the XML field value.

Use `CP_LOAD_STATE4` (0x30). Word 0 is DST_OFF[0:13], STATE_SRC[16:17], STATE_BLOCK[18:21], NUM_UNIT[22:31]; word 1 contains STATE_TYPE[0:1] and EXT_SRC_ADDR[2:31], with the high address in word 2. It is not enough to change the A6xx opcode, because A6xx STATE_TYPE lives in word 0. `SS4_DIRECT=0`, `SS4_INDIRECT=2`; shader and constant loads choose the SB4 stage block. Count units depend on the resource. Validate source-address alignment before packing, and verify all count/add/multiply operations for overflow before allocating or appending packet words.

Set local dimensions only after enforcing each dimension >=1 and <=1024 (10-bit encoded dimension-minus-one); NDRANGE global dimensions use checked local*groups multiplication, offsets are explicit. Follow {ref('workgroup')}. `CP_EXEC_CS` has four payload words (reserved zero, X/Y/Z group counts); `CP_EXEC_CS_INDIRECT` on A5xx also has four (reserved zero, address LO/HI, encoded local dimensions), after the proper flush ({ref('indirect')}). Zero work and invalid metadata must be handled before constructing any stream.

Storage binding is a distinct contract. **No ST4_IBO exists**. SSBO/image state uses `SB4_CS_SSBO`/`SB4_SSBO` and the A5XX_SSBO layouts, not the A6xx 16-dword IBO descriptor or SP_CS_IBO pointer. SSBO size is **bytes**, split across the size-state words; address state is a separate two-dword load ({ref('ssbo')}). Images also have a sampled texture view and independent image state ({ref('image')}). Shader compiler/lowering, binding numbering and buffer size units must be ported together. Reject image/SSBO features until this path is consistent; a constant table alone cannot make a shader correct.

Private memory fields must be repacked from XML. MEMSIZEPERITEM uses shr=9, TOTALPVTMEMSIZE shr=12 and A5xx HWSTACKOFFSET is inside PVT_MEM_PARAM with shr=11. Do not write A6xx's dedicated HW_STACK_OFFSET register or PERWAVEMEMLAYOUT flag. The 32-byte XML alignment is metadata; it does not prove an allocation is mapped or safe. Initially reject shaders needing unimplemented spills/stack behavior.

## Texture and sampler contract

Sampled texture state has **12 dwords per descriptor**, loaded as `ST4_CONSTANTS` in the stage's SB4 texture block ({ref('texture_load')}). Samplers have **4 dwords**, loaded as `ST4_SHADER` ({ref('sampler_load')}). A6xx 16-word strides must be removed in allocations, array counts, copies, fixtures and compiler resource metadata together. Use TPL1 stage counts/pointer registers where the A5xx emitter requires them; no SP_CS_TEX_* alias is generated.

Fields are described directly by XML: width/height are 15 bits, PITCH in bytes occupies bits 7..28 (shr=0), type occupies bits 29..31; ARRAY_PITCH is in 4KiB units (shr=12), BASE_LO requires 32-byte alignment (shr=5), BASE_HI has 17 bits, DEPTH has 13 bits. These fields do not imply identical allocation layout between generations. PITCHALIGN is the log2 byte-pitch alignment minus 6 ({ref('pitchalign')}); derive it from A5xx layout, not bytes per pixel. Buffer texture element counts use low 15 bits in WIDTH and high 15 in HEIGHT ({ref('buffer_texture')}). Enforce upper bounds before encoding; flags must not silently overlap pitches or depth.

Format equivalence is a **PIPE_FORMAT + role join**, with exact fd5/fd6 table lines in each format row. `FMT6_A8_UNORM` maps to `TFMT5_8_UNORM` with the A5xx alpha swizzle for sampling, but to `RB5_A8_UNORM` for color output. Packed RGB10A2 sampling uses `TFMT5_10_10_10_2_UNORM` (0x36); the color target uses `RB5_R10G10B10A2_UNORM` (0x37). Vertex formats use VFMT5. A `*_NONE` target means that role is unavailable. Equal numeric values cannot replace swizzle, sRGB, compressed-block layout, resource type or color capability checks.

LOD_BIAS is signed fixed with radix 8; min/max LOD are unsigned fixed with radix 8. The generated float parameters match XML. Cubic filtering and A6xx reduction modes have no proven A5xx equivalent in this input: reject those requests. Do not preserve A6xx sampler words 2/3 or border-color packing by assumption; implement the A5xx buffer/table format and base update from {ref('border')}. Depth, planar YUV, UBWC, MSAA, compressed textures and array/3D variants must stay gated until their fd5 layout/view semantics and tests are ported. A field-name match is not a feature pass.

## Render, GMEM and copy contract

Use {ref('render')} for stage configurations, program object/length/preload state, varying links and vertex fetch; use {ref('gmem')} for binning/sysmem transitions, attachment layout and resolve sequencing. Indexed registers use their A5xx strides (e.g. RB_MRT stride 7, not A6xx stride 8), and VSC has 16 pipes in the A530 Mesa profile. A6xx viewport arrays, VSC draw/primitive streams, CCU offsets, unknown startup tables, GMEM flag-buffer layouts and register context assumptions cannot be copied merely because an XML field exists.

`CP_SET_MARKER` and `CP_SET_BIN_DATA5_OFFSET` are A6xx+, and must be rejected by the A5xx stream path. `CP_SET_BIN_DATA5` being numerically allowed does not validate an A6xx seven-word use site: rebuild from fd5's packet shape and VSC resources. Shared cache events still require A5xx emission order and validated payload. Unknown initialization constants remain explicitly unimplemented.

**`CP_MEMCPY` 0x75 is A6xx+**, XML source `{opcode_xml['file']}:{opcode_xml['line']}`. The existing `pm4::kMemcpy` and command.cpp doubling-fill algorithm are unusable on A5xx. `CP_MEM_TO_MEM` is a different operation with distinct units/payload and must not be renamed to bulk memcpy. A future implementation may use a validated A5xx blit or a CPU copy for host-visible resources, with checked bounds and defined overlap handling. Until then reject the operation.

For A5xx's dedicated 2D blit, fd5 uses `CP_SET_RENDER_MODE(BLIT2D)`, **five CP_BLIT payload words** (COPY operation, source TL/BR, destination TL/BR), then `CP_SET_RENDER_MODE(END2D)` ({ref('blit_mode')}, {ref('blit_packet')}). A6xx's single-word `CP_BLIT(BLIT_OP_SCALE)` emission and SP_PS_2D/RB_2D registers are not a port. Scaling, rotation and format conversion require their own supported implementation and validation.

The A530 entry's host policy ({ref('chip_policy')}) is shared memory 32KiB, 4 SP cores, threadsize base 32, fibers_per_sp=64*16, wave granularity 2, GMEM/tile alignment 64x32 and 16 VSC pipes. This is Mesa software model data, **not a chip-ID read, GPU MHz measurement, power-state proof or successful execution**. The generated header deliberately contains only XML constants, leaving these policies for the later driver/compilation work.

## Verification and handoff

Reproduce from the workspace or any cwd:

```text
python -I ports/hakios-lithium/hakios-metal-a5xx/tools/generate-a5xx.py
python3 -I ports/hakios-lithium/hakios-metal-a5xx/tools/check-a5xx.py --self-test --mesa-witness --sanitize
```

On Windows, run the second command with the available WSL clang++ toolchain and absolute `/mnt/f/IOS-MIX1/...` script path. No shared Makefile is invoked or edited. The generated audit C++ checks every value/type at compile time, field encoder signatures, representative boundary/random values, float/fixed samples and array endpoints. The hand-authored test supplies independent compute/load-state/descriptor words, opcode refusals and an exhaustive finite binary16 round trip. Mutation checks reject value/type/source/scale/name/availability corruption. `validation.json` and compile/runtime logs contain the final results.

The constants deliverables are `a5xx.hpp`, the two tools, `a5xx_constants_test.cpp` and this evidence directory. Later compute, texture and render source changes remain the main agent's work. Full M0 requires those actual port implementations and their own meaningful host tests; this report must not be labeled M0 all-pass.
"""
    (folder / "implementation-contract.md").write_text(text, encoding="utf-8")
    write_json(folder / "implementation-source-anchors.json", refs)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mesa-root", type=Path, default=MESA)
    p.add_argument("--hakios-root", type=Path, default=HAKIOS)
    p.add_argument("--evidence-dir", type=Path, default=EVIDENCE)
    p.add_argument("--header", type=Path, default=PORT / "include/hakios-metal/a5xx.hpp")
    p.add_argument("--inventory-only", action="store_true")
    args = p.parse_args()
    inv = inventory(args.hakios_root)
    write_json(args.evidence_dir / "hakios-usage-inventory.json", inv)
    print(f"inventory: {len(inv['symbols'])} symbols ({inv['direct_symbols']} direct)")
    if args.inventory_only:
        return
    model = load_model(args.mesa_root)
    header = render_header(model)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8", newline="\n")
    manifest = {"scope": "host-only constants sub-item; M0 not passed", "mesa_release": "25.0.7",
                "release_sha256": RELEASE_SHA256, "header_sha256": sha(args.header), **model}
    write_json(args.evidence_dir / "constants-manifest.json", manifest)
    old = load_model(args.mesa_root, 6)
    report = make_differences(inv, model, old, args.mesa_root)
    write_differences(args.evidence_dir, report)
    write_implementation_spec(args.evidence_dir, args.mesa_root, report, model)
    print(f"generated: {len(model['items'])} constants, {len(model['fields'])} fields, {len(model['arrays'])} arrays")


if __name__ == "__main__":
    main()
