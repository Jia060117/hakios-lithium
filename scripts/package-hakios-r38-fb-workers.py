#!/usr/bin/env python3
"""Freeze the R38 fb_threads=2 comparison, using frozen bytes only.

Run with the existing WSL Python: its gzip encoder must reproduce the baseline
exactly. All writes stay in evidence/cpu-r38-20261121/fb-workers. No compiler,
device tool, FAT formatter, source sync, or subprocess is called.

--verify-only reopens the frozen files and repeats the actual layered readback.
"""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OWN = ROOT / "evidence/cpu-r38-20261121/fb-workers"
OUT = OWN / "phase4-r38-ufs-fb2-01"
BASE = ROOT / "artifacts/hakios/lithium-ios16/phase4-r38-ufs-perf-low01"
IMAGE = "lithium-hakios-ios16-r38-ufs-fb2-01.img"
BASE_IMAGE = "lithium-hakios-ios16-r38-ufs-perf-low01.img"
BASE_SHA = "bb4e8e23e0cc7d9af8198e6974c92cd51cbfdbc2c070b181c59027a608acb5b6"
CONTRACT_SHA = "0e93f2829ad51afdcd4916a82cc0b0d01daab6b949056714d02654638756c0c5"
KERNEL_SHA = "eb5b407451e41be6b2b14ff4c6b7d24d034b1ba30470e396de0d989c35a83a4a"
SOURCE_ROOT = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
FB_REL = Path("iokit/Kernel/MiniIOMobileFramebuffer.cpp")
SNAPSHOT = ROOT / "reports/local/20261121-r38-ufs-perf-low01-approved/snapshot-134032/console.log"
ENTRY_FILES = {"kernel": "hakios-kernel", "ramdisk": "root.apfs",
               "resident": "flashboot.apfs"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def meta(data):
    return {"bytes": len(data), "sha256": digest(data)}


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def write(path, data):
    path = path.resolve()
    require(path.is_relative_to(OWN.resolve()), "write escaped owned evidence")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
    require(path.read_bytes() == data, "written bytes differ: " + str(path))


def write_json(path, value):
    write(path, (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode())


def changed(a, b):
    require(len(a) == len(b), "byte-difference inputs have unequal sizes")
    return [i for i, (x, y) in enumerate(zip(a, b)) if x != y]


def layers(codec, live, blob):
    raw, dtbs, fat, header = codec.decode_android(blob)
    require(raw[live.XNUP_AT:live.XNUP_AT + 4] == b"XNUP", "XNUP offset differs")
    size = struct.unpack_from("<I", raw, live.XNUP_AT + 4)[0] + 8
    require(0 < size < live.PAYLOAD_LIMIT and live.XNUP_AT + size <= len(raw),
            "XNUP extent out of bounds")
    payload = raw[live.XNUP_AT:live.XNUP_AT + size]
    entries = codec.parse_payload(payload)
    require(set(entries) == live.ENTRY_NAMES, "XNUP entry set differs")
    require(codec.encode_payload(list(entries.items())) == payload,
            "actual codec does not reproduce XNUP bytes")
    padding = raw[live.XNUP_AT + size:]
    require(padding == live.NOP * ((-size) % 16 // 4), "Image padding differs")
    ks = struct.unpack_from("<I", header, 8)[0]
    packed = blob[4096:4096 + ks]
    compressed = packed[:-len(dtbs)] if dtbs else packed
    return dict(raw=raw, dtbs=dtbs, fat=fat, header=header, payload=payload,
                entries=entries, shim=raw[:live.FD_AT],
                fd=raw[live.FD_AT:live.XNUP_AT], padding=padding,
                gzip=compressed, loader=live.fat_loader(fat))


def baseline(codec, boot, live):
    contract_bytes = (BASE / "contract.json").read_bytes()
    require(digest(contract_bytes) == CONTRACT_SHA, "baseline contract changed")
    contract = json.loads(contract_bytes)
    require(contract["passed"] and contract["package_host_ready"] and
            contract["image"]["sha256"] == BASE_SHA, "baseline is not accepted low01")
    files = {}
    verified = {}
    for name, expected in contract["components"].items():
        require(Path(name).name == name, "baseline component path escaped")
        data = (BASE / name).read_bytes()
        require(meta(data) == expected, "baseline component changed: " + name)
        files[name] = data
        verified[name] = dict(path=str(BASE / name), **meta(data), matches_contract=True)
    blob = files[BASE_IMAGE]
    require(digest(blob) == BASE_SHA and meta(blob) == {
        k: contract["image"][k] for k in ("bytes", "sha256")}, "baseline image differs")
    old = layers(codec, live, blob)
    boot.check_boot(blob, old["raw"], old["dtbs"], old["fat"], old["header"])
    require(boot.payload_entries(old["payload"]) == old["entries"],
            "second actual XNUP parser disagrees")
    for entry, filename in ENTRY_FILES.items():
        require(old["entries"][entry] == files[filename],
                "baseline embedded entry differs: " + entry)
    for layer, filename in (("fd", "lithium.fd"), ("fat", "loader.fat"),
                            ("loader", "loader.efi"), ("payload", "payload.bin"),
                            ("raw", "Image")):
        require(old[layer] == files[filename], "baseline embedded layer differs: " + layer)
    args = live.text_entry(old["entries"]["cmdline"])
    require(args == contract["cmdline"] and files["boot-args.txt"] == (args + "\n").encode(),
            "baseline command line disagreement")
    tokens = args.split()
    required = {
        "bootcpu=1", "cpus=3", "ufs=2", "root_apfs=ufs_userdata",
        "var_hfs=ufs_darwin_data", "ufs_write=1", "lithiumusb=2",
        "gl=0", "metal=0", "fbwidth=1080", "fbheight=2040", "fb_threads=1",
        "fb_filter=0", "loader_return_after=10800", "touch=1", "touch_log=1",
        "touch_poll=1", "touch_smp=1", "applog_ui_quiet=1", "ui_lite=1",
        "cpu_perf_fixed=1", "cpu_perf_" + "ufs=1",
    }
    require(required.issubset(tokens) and len(tokens) == len(set(tokens)) and
            not any(t.startswith("return_fastboot") for t in tokens),
            "baseline geometry/touch/UFS/CPU/return arguments differ")
    require(contract["programmed_cpu_model_hz"] == 307200000 and
            contract["return_fastboot"] is False and
            contract["ufs_automatic_return_supported"] is False and
            contract["voltage_write"] is False, "baseline model/capability differs")
    require(digest(old["entries"]["kernel"]) == KERNEL_SHA, "baseline kernel pin differs")
    live.verify_kernel(old["entries"]["kernel"], "ufs")
    live.verify_root(old["entries"]["ramdisk"])
    # This also proves that compression/header/padding changes come only from the
    # changed input byte, not a new codec version, timestamps, OS flag or FAT.
    require(gzip.compress(old["raw"], compresslevel=9, mtime=0) == old["gzip"],
            "gzip cannot reproduce baseline: use the existing WSL Python/zlib")
    require(live.make_android(old["raw"], old["dtbs"], old["fat"], old["header"]) == blob,
            "actual Android encoder does not reproduce baseline")
    return contract, old, verified


def comparison(codec, boot, live, old, new_blob):
    new = layers(codec, live, new_blob)
    boot.check_boot(new_blob, new["raw"], old["dtbs"], old["fat"], old["header"])
    require(boot.payload_entries(new["payload"]) == new["entries"],
            "second actual XNUP parser disagrees on output")
    before = live.text_entry(old["entries"]["cmdline"])
    after = live.text_entry(new["entries"]["cmdline"])
    old_tokens, new_tokens = before.split(), after.split()
    require(old_tokens.count("fb_threads=1") == 1 and len(old_tokens) == len(new_tokens),
            "worker token count differs")
    token_diff = [dict(index=i, before=a, after=b)
                  for i, (a, b) in enumerate(zip(old_tokens, new_tokens)) if a != b]
    require(token_diff == [dict(index=old_tokens.index("fb_threads=1"),
                                before="fb_threads=1", after="fb_threads=2")],
            "comparison changed more than one token")
    expected_args = before.replace("fb_threads=1", "fb_threads=2", 1)
    require(after == expected_args, "command spacing or other command bytes changed")
    entry_diff = [k for k in old["entries"] if old["entries"][k] != new["entries"][k]]
    require(entry_diff == ["cmdline"], "non-command XNUP entry changed")
    payload_diff = changed(old["payload"], new["payload"])
    raw_diff = changed(old["raw"], new["raw"])
    require(len(payload_diff) == 1 and
            old["payload"][payload_diff[0]] == ord("1") and
            new["payload"][payload_diff[0]] == ord("2") and
            raw_diff == [live.XNUP_AT + payload_diff[0]], "expected one changed raw byte")
    require(list(old["entries"]) == list(new["entries"]), "XNUP ordering changed")
    for key in ("shim", "fd", "fat", "loader", "dtbs", "padding"):
        require(new[key] == old[key], "immutable layer changed: " + key)
    shim, literal = live.replace_copy_length(old["shim"], len(old["payload"]),
                                             len(new["payload"]))
    require(shim == old["shim"] == new["shim"], "same-sized payload changed shim")
    header_diff = changed(old["header"], new["header"])
    require(all(8 <= at < 12 or 576 <= at < 596 for at in header_diff),
            "unexpected Android header edit")
    require(gzip.compress(new["raw"], compresslevel=9, mtime=0) == new["gzip"],
            "new gzip stream differs from deterministic encoder")
    require(live.make_android(new["raw"], old["dtbs"], old["fat"], old["header"]) == new_blob,
            "new outer bytes differ from actual encoder")
    preserved = {key: dict(**meta(old[key]), byte_identical=True)
                 for key in ("shim", "fd", "fat", "loader", "dtbs", "padding")}
    entry_meta = {key: dict(before=meta(value), after=meta(new["entries"][key]),
                            byte_identical=value == new["entries"][key])
                  for key, value in old["entries"].items()}
    evidence = dict(
        passed=True, exact_one_token=token_diff, payload_entries=entry_meta,
        payload_changed_byte_offsets=payload_diff, Image_changed_byte_offsets=raw_diff,
        old_byte_hex="31", new_byte_hex="32", immutable_layers=preserved,
        payload_bytes=len(new["payload"]), payload_size_unchanged=True,
        shim_copy_length_literal_offset=literal,
        shim_copy_bytes=struct.unpack_from("<Q", shim, literal)[0],
        shim_copy_length_changed=False, header_changed_byte_offsets=header_diff,
        android_kernel_size_before=struct.unpack_from("<I", old["header"], 8)[0],
        android_kernel_size_after=struct.unpack_from("<I", new["header"], 8)[0],
        android_id_sha1_before=old["header"][576:596].hex(),
        android_id_sha1_after=new["header"][576:596].hex(),
        gzip_before=meta(old["gzip"]), gzip_after=meta(new["gzip"]),
        baseline_gzip_exact_replay=True, baseline_android_exact_replay=True,
        final_android_exact_replay=True,
        codec="package-hakios-ios16-touch-r21.py: decode_android/parse_payload/encode_payload",
        second_parser="package-hakios-ios16.py: payload_entries",
        android_readback="package-hakios-ios16.py: check_boot",
        fat_readback="package-hakios-touch-live.py: fat_loader; frozen FAT not rewritten",
        runtime_verified=False,
    )
    return new, evidence


def source_analysis(kernel):
    prepared = SOURCE_ROOT / "xnu/files" / FB_REL
    actual = SOURCE_ROOT / "xnu-src/xnu-8796.121.2" / FB_REL
    profile = SOURCE_ROOT / "devices/lithium/diagnostics/framebuffer-profile.inc"
    require(prepared.read_bytes() == actual.read_bytes(), "prepared and actual framebuffer differ")
    require(digest((SOURCE_ROOT / "hakios-kernel").read_bytes()) == digest(kernel) == KERNEL_SHA,
            "existing build workspace kernel differs from frozen baseline")
    text = actual.read_text()
    needles = {
        "workers_tunable": 'gThreads, "fb_threads", 3',
        "priority_tunable": 'gThreadPriority, "fb_pri", 92',
        "helper_creation": "i + 1 < fThreads",
        "helper_rows_start": "rows * (index + 1) / fb->fThreads",
        "helper_rows_end": "rows * (index + 2) / fb->fThreads",
        "fallback": "fThreads <= 1 || fDiff || fHelpersStarted + 1 < fThreads",
        "first_eight_profile": "bool lithium_profile = fSwapID <= 8;",
        "presenter_rows_end": "(job.ry1 - job.ry0) / fThreads",
        "join": "while (fHelpersBusy != 0)",
        "five_second_stats": "fStatDrawing += mach_absolute_time() - began;",
    }
    anchors = {}
    for name, needle in needles.items():
        require(needle in text, "actual source lacks expected worker/profiling phase: " + name)
        anchors[name] = dict(line=text[:text.index(needle)].count("\n") + 1, text=needle)
    require(not re.search(r"\b(?:thread_bind|thread_affinity_set)\s*\(", text),
            "framebuffer affinity assumptions changed")
    parsed = load("r38_fb_timing", "hakios_display_timing.py")
    snapshot = SNAPSHOT.read_bytes()
    timing = parsed.summarize(snapshot.decode("utf-8", errors="replace"))
    analysis = dict(
        source_scope="Actual prepared and xnu-src patched files; existing workspace kernel hash equals frozen kernel. No new build.",
        source_sha256=digest(actual.read_bytes()), anchors=anchors,
        fb_threads_2=dict(total_drawers=2, presenter=1, helpers=1,
                          full_panel_presenter_rows=[0, 1020], full_panel_helper_rows=[1020, 2040],
                          interval_convention="[start,end)", pixels_each=1080 * 1020,
                          panel_bytes_each=3240 * 1020,
                          split="floor(rows/2) first; remaining rows second; int64 multiplication",
                          disjoint_full_coverage=True),
        math_risk=[
            "Rows are split equally, not weighted by measured per-core throughput.",
            "The framebuffer threads have priority 92 by default but no explicit CPU affinity here; actual placement/migration is unmeasured.",
            "XNU phys1 is faster than phys100/101 in earlier RAM CPU sanity workloads. Those integer/memcpy ratios do not establish framebuffer conversion throughput.",
            "Ideal rows-only time is max(R/(2*v_presenter), R/(2*v_helper)) plus dispatch/join overhead; if the helper is less than half as fast as the baseline drawer this can regress.",
            "The presenter publishes one RowJob, wakes the helper, draws its first half, then waits uninterruptibly for the helper count to reach zero. The slower share sets completion.",
            "Helper creation/startup failure or fDiff takes the single-thread fallback. fb_threads=2 is requested capacity, not runtime proof of two active drawers.",
            "Default fb_filter=0 and BGR24 packing remain unchanged; this partitions panel conversion/copy, not all software composition or input handling.",
            "The existing global console-ownership store occurs in drawRows; this host review is not a concurrency or long-run safety proof.",
        ],
        profiling=dict(
            first_eight="LITHIUM DISPLAY SPLIT and LITHIUM DISPLAY CPU are already compiled in under LITHIUM_ARMV8_0 for swap ID <=8, inside the single-thread fallback only.",
            profile_bootarg="No separate enable bootarg in framebuffer-profile.inc or the patched call sites.",
            phases="band0 setup; band1 row drawing; bands2.. up to eight row bands; wall and current-thread recount user+system CPU.",
            cpu_limits="wall minus current-thread CPU includes scheduling/blocking/interrupts/overhead; not a scheduler-only measurement. Reject cpu_le_wall=0 or nonmonotonic samples.",
            comparison_limit="When both drawers are active the parallel branch bypasses these CPU/split spans. Their absence cannot prove successful worker execution; no per-worker CPU/core attribution is available.",
            existing_optional_args=["touch_lat (swap ready/shown/done wall timeline)", "fb_pri",
                                    "fbtrace", "fb_beat", "fb_measure", "fb_beam", "fb_intf"],
            optional_arg_limits="None added. Do not opt into foreign-SoC interface MMIO merely for profiling. fb_measure/fb_intf require board-specific validation.",
            steady_stats="5-second driver drawing/GPU/beat/queue counters remain; drawing is wall elapsed including helper completion/scheduling. Per-share wall statistics require fBeamIntf !=0.",
            added_args=[],
        ),
        saved_baseline_early_spans=dict(source=str(SNAPSHOT), **meta(snapshot),
                                        **timing, scope="Saved early baseline prefix only; no live USB or device query."),
        runtime_verified=False, guaranteed_speedup=False, measured_worker_speedup=None,
    )
    sources = [
        ("actual-MiniIOMobileFramebuffer.cpp", actual),
        ("prepared-MiniIOMobileFramebuffer.cpp", prepared),
        ("framebuffer-profile.inc", profile),
        ("port-framebuffer-profile.inc", ROOT / "ports/hakios-lithium/diagnostics/framebuffer-profile.inc"),
    ]
    require(profile.read_bytes() == sources[-1][1].read_bytes(), "profile include differs from port")
    return analysis, sources


def verify_existing(codec, boot, live, old):
    freeze = json.loads((OWN / "freeze-manifest.json").read_text())
    for relative, expected in freeze["files"].items():
        path = (OWN / relative).resolve()
        require(path.is_relative_to(OWN.resolve()), "freeze path escaped")
        require(meta(path.read_bytes()) == expected, "frozen file differs: " + relative)
    require(digest(Path(__file__).read_bytes()) == freeze["script_sha256"],
            "packager source differs from frozen manifest")
    sources = json.loads((OWN / "source-manifest.json").read_text())
    for item in sources["files"]:
        if item["category"] == "executed_helper":
            require(meta(Path(item["path"]).read_bytes()) == item["metadata"],
                    "executed helper changed since freeze: " + item["path"])
    contract = json.loads((OUT / "contract.json").read_text())
    for name, expected in contract["components"].items():
        require(meta((OUT / name).read_bytes()) == expected, "component differs: " + name)
    new, proof = comparison(codec, boot, live, old, (OUT / IMAGE).read_bytes())
    require(proof == json.loads((OWN / "readback.json").read_text()), "layer proof differs")
    for entry, filename in ENTRY_FILES.items():
        require((OUT / filename).read_bytes() == new["entries"][entry], "saved entry differs")
    for layer, filename in (("fd", "lithium.fd"), ("fat", "loader.fat"),
                            ("loader", "loader.efi"), ("payload", "payload.bin"),
                            ("raw", "Image"), ("header", "android-header.bin"),
                            ("dtbs", "dtbs.bin"), ("shim", "bootshim.bin")):
        require((OUT / filename).read_bytes() == new[layer], "saved layer differs: " + layer)
    args = live.text_entry(new["entries"]["cmdline"])
    require(contract["cmdline"] == args and
            (OUT / "boot-args.txt").read_bytes() == (args + "\n").encode(), "saved args differ")
    return contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true")
    options = parser.parse_args()
    live = load("r38_fb_live", "package-hakios-touch-live.py")
    codec, boot = live.bootstrap()
    base_contract, old, verified = baseline(codec, boot, live)
    if options.verify_only:
        result = verify_existing(codec, boot, live, old)
        print(json.dumps(dict(passed=True, mode="verify-only", image=str(OUT / IMAGE),
                              **result["image"], device_operations=0), indent=2))
        return
    require(not OWN.exists(), "owned output already exists; use --verify-only to preserve it")
    # No shared control file is written; hash these to record preservation.
    protected = {str(p): meta(p.read_bytes())
                 for p in (ROOT / ".git/index", ROOT / "project.json")}
    before = live.text_entry(old["entries"]["cmdline"])
    args = before.replace("fb_threads=1", "fb_threads=2", 1)
    entries = dict(old["entries"], cmdline=args.encode("ascii") + b"\0")
    payload = codec.encode_payload(list(entries.items()))
    shim, _ = live.replace_copy_length(old["shim"], len(old["payload"]), len(payload))
    raw = shim + old["fd"] + payload + old["padding"]
    require(len(raw) <= live.IMAGE_LIMIT, "comparison Image exceeds reservation")
    image = live.make_android(raw, old["dtbs"], old["fat"], old["header"])
    new, proof = comparison(codec, boot, live, old, image)
    budget = live.command_budget(args, live.text_entry(entries["kernel.ver"]), "ufs")
    require(budget == base_contract["command_budget"], "same-length command budget changed")
    analysis, sources = source_analysis(entries["kernel"])
    components = {
        **{filename: entries[entry] for entry, filename in ENTRY_FILES.items()},
        "loader.efi": new["loader"], "lithium.fd": new["fd"],
        "loader.fat": new["fat"], "payload.bin": new["payload"], "Image": new["raw"],
        IMAGE: image, "boot-args.txt": (args + "\n").encode(),
        "android-header.bin": new["header"], "dtbs.bin": new["dtbs"], "bootshim.bin": new["shim"],
    }
    for name, data in components.items():
        write(OUT / name, data)
    write_json(OWN / "baseline-verify.json", dict(
        passed=True, image_sha256=BASE_SHA, contract_sha256=CONTRACT_SHA,
        components=verified, actual_embedded_layers_match_saved_components=True,
        android_header_sha1_checked=True, baseline_codec_replay_byte_identical=True))
    write_json(OWN / "readback.json", proof)
    write_json(OWN / "source-analysis.json", analysis)
    source_items = []
    helper_paths = {Path(__file__).resolve()}
    for module in list(sys.modules.values()) + [live, codec, boot]:
        path = getattr(module, "__file__", None)
        if path and Path(path).resolve().parent == ROOT / "scripts":
            helper_paths.add(Path(path).resolve())
    for path in sorted(helper_paths):
        sources.append(("helpers/" + path.name, path))
    for name, path in sources:
        data = path.read_bytes()
        write(OWN / "sources" / name, data)
        source_items.append(dict(path=str(path), snapshot="sources/" + name,
                                 category="executed_helper" if name.startswith("helpers/") else "analyzed_source",
                                 metadata=meta(data)))
    write_json(OWN / "source-manifest.json", dict(
        files=source_items, baseline_contract=dict(path=str(BASE / "contract.json"),
                                                 sha256=CONTRACT_SHA),
        existing_workspace_kernel_sha256=KERNEL_SHA,
        compiler_invocations=0, subprocess_invocations=0, source_modifications=0))
    contract = dict(
        schema="hakios-r38-ufs-fb2-01", created_at_utc=datetime.now(timezone.utc).isoformat(),
        passed=True, package_host_ready=True, frozen=True,
        runtime_verified=False, main_independent_validation="pending",
        baseline=dict(path=str(BASE), image_sha256=BASE_SHA, contract_sha256=CONTRACT_SHA,
                      all_components_verified=True),
        image=dict(path=IMAGE, **meta(image)),
        components={name: meta(data) for name, data in components.items()},
        payload_entries={key: meta(value) for key, value in entries.items()},
        cmdline=args, command_budget=budget, only_token_change=proof["exact_one_token"],
        display_baseline="R20-display", geometry=dict(width=1080, height=2040, stride=3240),
        fb_threads_requested=2, xnu_cpu_count=3, touch_smp=True,
        programmed_cpu_model_hz=307200000, measured_cpu_mhz=None,
        inherited_s11_digital_uv=905000, voltage_write=False, higher_stock_step=False,
        normal_boot_may_write_userdata=True, return_fastboot=False,
        automatic_fastboot_enabled=False, ufs_automatic_return_supported=False,
        kernel_timer_seconds=10800, device_operations=0, build_invocations=0,
        partition_operations=0, qemu_used=False, usb_process_changed=False,
        boot_counts=dict(hakios=78, ios16=47),
        output_location_reason="New artifact kept below owned evidence path; shared artifacts/index not modified.",
        readback=proof, script_sha256=digest(Path(__file__).read_bytes()),
    )
    write_json(OUT / "contract.json", contract)
    for path, expected in protected.items():
        require(meta(Path(path).read_bytes()) == expected, "shared control changed: " + path)
    write_json(OWN / "protected-files.json", dict(before=protected, after=protected, changed=False))
    report = f"""# Frozen R38 UFS fb_threads=2 comparison

Host-ready image: `{OUT / IMAGE}`

SHA256: `{digest(image)}`

Baseline image: `{BASE_SHA}`. Pinned baseline contract: `{CONTRACT_SHA}`.
Every baseline component hash and its actual embedded layer were verified.
Only payload command token index {proof['exact_one_token'][0]['index']} changes
`fb_threads=1` to `fb_threads=2`; no whitespace or other argument changes.

```text
{args}
```

Actual codec replay and independent payload parser/readback passed.
XNUP payload changes at byte {proof['payload_changed_byte_offsets'][0]};
decompressed Image changes only at byte {proof['Image_changed_byte_offsets'][0]}:
ASCII `1` (0x31) to `2` (0x32). Payload size is unchanged ({len(payload)} bytes).
BootShim is byte-identical, including copy-length literal at
{proof['shim_copy_length_literal_offset']} ({proof['shim_copy_bytes']} copied bytes).
Kernel, APFS root, resident APFS, Mu FD, loader, entire FAT, DTBs, kernel.ver,
and Image alignment padding are identical. Only deterministic gzip, Android
kernel-size field when needed, its SHA1 ID, and necessary outer padding are
regenerated. The baseline itself re-encodes byte for byte under this codec.
Header byte differences: {proof['header_changed_byte_offsets']}.
See `readback.json` for all old/new hashes and exact offsets.

R20 1080x2040/stride3240, cpus=3, touch_smp=1, software drawing, fb_filter=0,
307.2 MHz configuration model, inherited S11 digital 905mV and no voltage
write are preserved. MHz and temperature are unmeasured. Runtime unverified;
no guaranteed speedup. UFS automatic Fastboot remains unsupported/disabled,
the inherited loader timer is 10800 seconds, normal boot may write Data.
No phone/query/USB changes, compiler, FAT rebuild, shared report/project/index
edit, or vendor commit. Accepted boot counts remain 78/47.

The actual patched source and prepared source have equal SHA256
`{analysis['source_sha256']}`; the existing workspace kernel equals the frozen
kernel `{KERNEL_SHA}`. Source copies and helper source hashes are in
`source-manifest.json`. No reconstruction or compilation was required.

For fb_threads=2, the presenter gets rows [0,1020) and one helper [1020,2040)
for a full panel: 1,101,600 pixels and 3,304,800 destination bytes per drawer.
Clipped/odd row counts use a floor split and remainder, with int64 products.
There is no speed weighting or explicit framebuffer-thread CPU affinity.
Both use the existing priority (default92); the presenter joins the helper.
Different scheduling on phys1 vs slower perf cores can negate the benefit or
regress elapsed time. Earlier RAM integer/memcpy ratios are not render rates.
Formula: rows-only elapsed ~= max(R/(2*v_presenter),R/(2*v_helper)) plus
dispatch/join overhead. Helper startup/failure or fDiff can retain serial work.

Existing first-eight-swap DISPLAY SPLIT/CPU samples separate setup, rows and
512-row bands, using wall plus current-thread recount CPU. These are inside
the serial fallback; the active parallel branch has no corresponding worker
CPU attribution. The saved baseline early prefix has
{analysis['saved_baseline_early_spans']['draw_total_count']} accepted draw
spans; see `source-analysis.json` for exact values and rejected samples.
These early data cannot establish steady-state CPU fraction. Five-second
driver/GPU/beat/queue wall counters remain available. Per-share wall stats
require fBeamIntf !=0. Existing touch_lat provides swap wall timelines;
no profiling/affinity/MMIO arguments were added to this comparison.

`freeze-manifest.json` records every delivered file. Main can independently
validate with:

```text
wsl.exe --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-r38-fb-workers.py --verify-only
```

Main independent validation is pending; packaging readback is complete.
"""
    write(OWN / "report.md", report.encode())
    files = {path.relative_to(OWN).as_posix(): meta(path.read_bytes())
             for path in sorted(OWN.rglob("*")) if path.is_file()}
    freeze = dict(schema="hakios-r38-fb-workers-freeze-v1", files=files,
                  script_sha256=contract["script_sha256"], image_sha256=digest(image),
                  baseline_sha256=BASE_SHA, device_operations=0, build_invocations=0)
    write_json(OWN / "freeze-manifest.json", freeze)
    # Reopen the delivered image, not just the in-memory package, for final proof.
    verify_existing(codec, boot, live, old)
    print(json.dumps(dict(passed=True, image=str(OUT / IMAGE), sha256=digest(image),
                          bytes=len(image), cmdline=args, frozen_files=len(files),
                          runtime_verified=False, device_operations=0), indent=2))


if __name__ == "__main__":
    main()
