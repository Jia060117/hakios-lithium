"""MIX-specific read-only display whitelist and raw capture decoder.

Bases/pipe offsets are from the matching stock FDT. Register offsets and
format bits are checked against the pinned mdss_mdp_hwio.h/pipe.c sources.
"""
import struct

PROPERTY = "mix1,display-probe-v1"
MARKER = b"MIX1DISPLAYPROBE"
RECORD_SIZE = 4096


def register_list(nodes, source_header):
    node, = [props for props in nodes.values() if props.get("compatible") == b"qcom,mdss_mdp\0"]
    cells = lambda b: struct.unpack(">" + "I" * (len(b) // 4), b)
    base, extent, *_ = cells(node["reg"])
    assert base == 0x900000 and extent == 0x90000
    names = ["SRC_SIZE", "SRC_IMG_SIZE", "SRC_XY", "OUT_SIZE", "OUT_XY", "SRC0_ADDR",
             "SRC_YSTRIDE0", "SRC_FORMAT", "SRC_UNPACK_PATTERN", "SRC_OP_MODE"]
    import re
    offsets = {}
    for name in names:
        match = re.search(r"#define\s+MDSS_MDP_REG_SSPP_" + name + r"\s+(0x[0-9a-fA-F]+)\b", source_header)
        assert match, name
        offsets[name] = int(match.group(1), 16)
    result = [dict(group="MDSS", name="HW_VERSION", address=base)]
    for group in ("vig", "rgb", "dma"):
        for i, offset in enumerate(cells(node[f"qcom,mdss-pipe-{group}-off"])):
            for name in names:
                result.append(dict(group=group.upper() + str(i), name=name, address=base + offset + offsets[name]))
    for i, offset in enumerate(cells(node["qcom,mdss-ctl-off"])):
        for lm in range(4):
            result.append(dict(group="CTL" + str(i), name="LAYER" + str(lm), address=base + offset + lm * 4))
        result.append(dict(group="CTL" + str(i), name="TOP", address=base + offset + 0x14))
    assert len(result) <= (3072 - 520) // 8
    assert len({x["address"] for x in result}) == len(result)
    assert all(base <= x["address"] < base + extent and x["address"] % 4 == 0 for x in result)
    return result


def decode(record, registers):
    assert len(record) == RECORD_SIZE and record[:16] == MARKER
    count = struct.unpack_from("<I", record, 512)[0]
    assert count == len(registers)
    samples, groups = [], {}
    for i, item in enumerate(registers):
        address, value = struct.unpack_from("<II", record, 520 + i * 8)
        assert address == item["address"]
        samples.append(dict(group=item["group"], name=item["name"], address=hex(address), value=hex(value)))
        groups.setdefault(item["group"], {})[item["name"]] = value
    candidates = []
    for group, regs in groups.items():
        if "SRC0_ADDR" not in regs:
            continue
        width, height = regs["SRC_SIZE"] & 0xffff, regs["SRC_SIZE"] >> 16
        stride = regs["SRC_YSTRIDE0"] & 0xffff
        address = regs["SRC0_ADDR"]
        if not address or not width or not height:
            continue
        fmt = regs["SRC_FORMAT"]
        bpp = ((fmt >> 9) & 3) + 1
        candidates.append(dict(pipe=group, address=hex(address), width=width, height=height,
                               image_width=regs["SRC_IMG_SIZE"] & 0xffff, image_height=regs["SRC_IMG_SIZE"] >> 16,
                               stride_bytes=stride, bytes_per_pixel=bpp,
                               source_format=hex(fmt), unpack_pattern=hex(regs["SRC_UNPACK_PATTERN"]),
                               op_mode=hex(regs["SRC_OP_MODE"]),
                               address_within_splash=0x83401000 <= address < 0x85800000,
                               visible_bytes_fit_splash=0x83401000 <= address and address + height * stride <= 0x85800000,
                               layout_linear=((fmt >> 30) & 3) == 0,
                               caveat="Nonzero pipe state alone does not prove it is routed to the active panel."))
    count, complete = struct.unpack_from("<II", record, 3072)
    assert 0 <= count <= 16
    reservations = [dict(address=hex(a), size_bytes=s) for a, s in
                    (struct.unpack_from(">QQ", record, 3080 + i * 16) for i in range(count))]
    return dict(register_count=len(samples), samples=samples, candidates=candidates,
                raw_fdt_memreserve=dict(complete=complete == 1, entries=reservations),
                no_display_register_writes=True)
