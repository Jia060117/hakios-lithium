"""Derive evidence and compile probes from exactly four pinned source files.

Run from any directory: python ports/hakios-lithium/gpu/abi/prepare.py
Writes only this directory and evidence/gpu-metal-20261123/abi.
No device calls, source-tree imports, network access or repository traversal.
"""
from pathlib import Path
import hashlib
import json
import re

ABI = Path(__file__).resolve().parent
ROOT = ABI.parents[3]
EVIDENCE = ROOT / "evidence/gpu-metal-20261123/abi"
SOURCES = {
    "kernel": ("upstream/hakios-xnu-kernel/drivers/gpu/hakios.gpu.cpp",
               "2d3ba9f37a45980438299137bb24f89fe81ff3376850337a3b5df51ff4ef8eb8"),
    "metal_header": ("upstream/hakios-xnu-kernel/hakios-metal/include/hakios-metal/gpu.hpp",
                     "ea45cbe172ea65e53f2f4d1dac67681f358338ad4349e09b171eb903002706c9"),
    "metal_gpu": ("upstream/hakios-xnu-kernel/hakios-metal/src/gpu.cpp",
                  "d16c01c4c94c4475dc96888661d5f6051f78f972e4552de70f648eff08417188"),
    "iokit": ("upstream/hakios-xnu-kernel/hakios-metal/src/iokit_transport.cpp",
              "48067c25eff3c46d4c9c75872fabd8498c4f53b9e3e259fe39b5343d841745c6"),
}

# Expected layout is deliberately independent of the C/C++ compiler probes.
# Fields: name, source type spelling, offset; no packing and eight-byte u64 alignment.
RECORDS = [
    ("Timespec", "msm_timespec", "MsmTimespec", 16, 8,
     [("seconds","int64_t",0),("nanoseconds","int64_t",8)]),
    ("Param", "msm_param", "MsmParam", 24, 8,
     [("pipe","uint32_t",0),("param","uint32_t",4),("value","uint64_t",8),("len","uint32_t",16),("pad","uint32_t",20)]),
    ("GemNew", "msm_gem_new", "MsmGemNew", 16, 8,
     [("size","uint64_t",0),("flags","uint32_t",8),("handle","uint32_t",12)]),
    ("GemInfo", "msm_gem_info", "MsmGemInfo", 24, 8,
     [("handle","uint32_t",0),("info","uint32_t",4),("value","uint64_t",8),("len","uint32_t",16),("pad","uint32_t",20)]),
    ("CpuPrep", "msm_gem_cpu_prep", "MsmGemCpuPrep", 24, 8,
     [("handle","uint32_t",0),("op","uint32_t",4),("timeout","Timespec",8)]),
    ("CpuFini", None, "MsmGemCpuFini", 4, 4, [("handle","uint32_t",0)]),
    ("SubmitCmd", "msm_gem_submit_cmd", "MsmSubmitCmd", 32, 8,
     [("type","uint32_t",0),("submit_idx","uint32_t",4),("submit_offset","uint32_t",8),
      ("size","uint32_t",12),("pad","uint32_t",16),("nr_relocs","uint32_t",20),("relocs","uint64_t",24)]),
    ("SubmitBo", "msm_gem_submit_bo", "MsmSubmitBo", 16, 8,
     [("flags","uint32_t",0),("handle","uint32_t",4),("presumed","uint64_t",8)]),
    ("Submit", "msm_gem_submit", "MsmGemSubmit", 72, 8,
     [("flags","uint32_t",0),("fence","uint32_t",4),("nr_bos","uint32_t",8),("nr_cmds","uint32_t",12),
      ("bos","uint64_t",16),("cmds","uint64_t",24),("fence_fd","int32_t",32),("queueid","uint32_t",36),
      ("in_syncobjs","uint64_t",40),("out_syncobjs","uint64_t",48),("nr_in_syncobjs","uint32_t",56),
      ("nr_out_syncobjs","uint32_t",60),("syncobj_stride","uint32_t",64),("pad","uint32_t",68)]),
    ("WaitFence", "msm_wait_fence", "MsmWaitFence", 32, 8,
     [("fence","uint32_t",0),("flags","uint32_t",4),("timeout","Timespec",8),("queueid","uint32_t",24)]),
    ("Madvise", "msm_gem_madvise", None, 12, 4,
     [("handle","uint32_t",0),("madv","uint32_t",4),("retained","uint32_t",8)]),
    ("Queue", "msm_submitqueue", "MsmSubmitQueue", 12, 4,
     [("flags","uint32_t",0),("prio","uint32_t",4),("id","uint32_t",8)]),
    ("QueueClose", None, None, 4, 4, []),
    ("QueueQuery", "msm_submitqueue_query", None, 24, 8,
     [("data","uint64_t",0),("id","uint32_t",8),("param","uint32_t",12),("len","uint32_t",16),("pad","uint32_t",20)]),
    ("GemClose", "drm_gem_close", "DrmGemClose", 8, 4, [("handle","uint32_t",0),("pad","uint32_t",4)]),
    ("UserMemory", "hakios_user_memory", None, 24, 8,
     [("address","uint64_t",0),("size","uint64_t",8),("handle","uint32_t",16),("flags","uint32_t",20)]),
    ("Surface", "hakios_surface", "HakiosSurface", 40, 8,
     [("surface","uint32_t",0),("handle","uint32_t",4),("size","uint64_t",8),("offset","uint32_t",16),
      ("rowBytes","uint32_t",20),("width","uint32_t",24),("height","uint32_t",28),
      ("pixelFormat","uint32_t",32),("planes","uint32_t",36)]),
    ("Show", "hakios_show", None, 24, 8,
     [("pixels","uint64_t",0),("width","uint32_t",8),("height","uint32_t",12),("rowBytes","uint32_t",16),("pad","uint32_t",20)]),
]

def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    loaded = {}
    manifest = []
    for key, (relative, expected) in SOURCES.items():
        raw = (ROOT / relative).read_bytes()
        if sha(raw) != expected:
            raise SystemExit(f"Pinned source changed: {relative}; refusing stale line evidence")
        text = raw.decode("utf-8")
        loaded[key] = text
        manifest.append(dict(id=key, path=relative, sha256=expected, bytes=len(raw), lines=len(text.splitlines())))
    dump(EVIDENCE / "source-manifest.json", {
        "scope": "exactly four pinned files; no backend implementation inspected",
        "sources": manifest, "upstream_write_count": 0, "device_calls": 0
    })

    def ref(key, first, last=None):
        lines = loaded[key].splitlines()
        last = first if last is None else last
        assert 0 < first <= last <= len(lines)
        return dict(source=key, path=SOURCES[key][0], first_line=first, last_line=last,
                    excerpt_sha256=sha(("\n".join(lines[first-1:last]) + "\n").encode()))

    snippets = []
    def evidence(text, *ranges):
        refs = [ref(*r) for r in ranges]
        snippets.append(dict(claim=text, refs=refs))
        return dict(text=text, evidence=refs)

    generated = ["// Generated verbatim struct definitions from pinned source; do not edit.",
                 "#pragma once", "#include <stdint.h>"]
    checks = ["// Generated independent expected sizes, alignment and every field offset."]
    checks.append('static_assert(sizeof(void*) == 8, "64-bit ABI required");')
    checks.append('static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "little endian required");')
    total_checks = 2
    spec_records = []
    for key, namespace, index in [("kernel","pinned_kernel",1),("metal_header","pinned_metal",2)]:
        generated.append(f"namespace {namespace} {{")
        for row in RECORDS:
            name = row[index]
            if not name:
                continue
            pattern = re.compile(r"\bstruct\s+" + re.escape(name) + r"\s*\{[^}]*\};", re.S)
            match = pattern.search(loaded[key])
            if not match:
                raise SystemExit(f"Missing record {key}:{name}")
            generated.append(match.group())
        generated.append("}")
    for short, kernel, metal, size, alignment, fields in RECORDS:
        typename = "HakiosGpu" + short
        refs = []
        variants = [typename]
        for key, prefix, name in [("kernel","pinned_kernel",kernel),("metal_header","pinned_metal",metal)]:
            if name:
                m = re.search(r"\bstruct\s+"+name+r"\s*\{[^}]*\};", loaded[key], re.S)
                refs.append(ref(key, loaded[key][:m.start()].count("\n")+1,
                                loaded[key][:m.end()].count("\n")+1))
                variants.append(prefix+"::"+name)
        if short == "QueueClose":
            refs = [ref("metal_gpu",133,138), ref("kernel",1238,1241)]
        for variant in variants:
            checks += [f'static_assert(sizeof({variant}) == {size}, "{variant} size");',
                       f'static_assert(alignof({variant}) == {alignment}, "{variant} alignment");']
            total_checks += 2
            for field, _, offset in fields:
                checks.append(f'static_assert(offsetof({variant}, {field}) == {offset}, "{variant}.{field}");')
                total_checks += 1
            for field, fieldtype, _ in fields:
                if fieldtype != "Timespec":
                    checks.append(f'static_assert(__is_same(decltype({variant}::{field}), {fieldtype}), "{variant}.{field} type");')
                    total_checks += 1
        spec_records.append(dict(name=typename, size=size, alignment=alignment,
                                 fields=[dict(name=n,type=t,offset=o) for n,t,o in fields], evidence=refs))
    generated.append("")
    (ABI / "source-layouts.hpp").write_text("\n".join(generated), encoding="utf-8")
    checks += [f"#define HAKIOS_ABI_RECORDS {len(RECORDS)}u",
               f"#define HAKIOS_ABI_FIELDS {sum(len(r[5]) for r in RECORDS)}u",
               f"#define HAKIOS_ABI_LAYOUT_CHECKS {total_checks}u", ""]
    (ABI / "layout-probe.inc").write_text("\n".join(checks), encoding="utf-8")

    # Mechanical equality of selector and constant numbers, independent of layout.
    selector_pairs = [
        ("GEM_CLOSE","kDrmGemClose","kGemClose","GemClose"),
        ("GET_PARAM","kMsmGetParam","kGetParam","Param"),
        ("SET_PARAM","kMsmSetParam",None,"Param"),
        ("GEM_NEW","kMsmGemNew","kGemNew","GemNew"),
        ("GEM_INFO","kMsmGemInfo","kGemInfo","GemInfo"),
        ("CPU_PREP","kMsmGemCpuPrep","kGemCpuPrep","CpuPrep"),
        ("CPU_FINI","kMsmGemCpuFini","kGemCpuFini","CpuFini"),
        ("SUBMIT","kMsmGemSubmit","kGemSubmit","Submit"),
        ("WAIT_FENCE","kMsmWaitFence","kWaitFence","WaitFence"),
        ("MADVISE","kMsmGemMadvise",None,"Madvise"),
        ("QUEUE_NEW","kMsmSubmitQueueNew","kSubmitQueueNew","Queue"),
        ("QUEUE_CLOSE","kMsmSubmitQueueClose","kSubmitQueueClose","QueueClose"),
        ("QUEUE_QUERY","kMsmSubmitQueueQuery",None,"QueueQuery"),
        ("USER_MEMORY","kHakiosUserMemory",None,"UserMemory"),
        ("SURFACE","kHakiosSurface","kHakiosSurface","Surface"),
        ("SHOW","kHakiosShow",None,"Show"),
    ]
    local_header = (ABI / "hakios_gpu_abi.h").read_text()
    def number(text, name):
        m = re.search(r"\b"+name+r"\s*=\s*(0x[0-9a-fA-F]+|\d+)", text)
        if not m:
            raise SystemExit("Missing numeric declaration: "+name)
        return int(m[1],0)
    selectors = []
    for local,kernel,metal,record in selector_pairs:
        n = number(loaded["kernel"],kernel)
        assert n == number(local_header, "HAKIOS_GPU_"+local)
        if metal:
            assert n == number(loaded["metal_header"],metal)
        selectors.append(dict(name=local,number=n,hex=hex(n),record="HakiosGpu"+record,
                              metal_wrapper=bool(metal), evidence=[ref("kernel",72,89),ref("metal_header",22,34)]))
    constant_pairs = {
        "PIPE_3D":"kPipe3D", "PARAM_GPU_ID":"kParamGpuId", "PARAM_GMEM_SIZE":"kParamGmemSize",
        "PARAM_CHIP_ID":"kParamChipId", "PARAM_MAX_FREQ":"kParamMaxFreq", "PARAM_TIMESTAMP":"kParamTimestamp",
        "PARAM_GMEM_BASE":"kParamGmemBase", "PARAM_PRIORITIES":"kParamPriorities",
        "PARAM_PP_PGTABLE":"kParamPpPgtable", "PARAM_FAULTS":"kParamFaults",
        "PARAM_SUSPENDS":"kParamSuspends", "PARAM_HIGHEST_BANK_BIT":"kParamHighestBankBit",
        "PARAM_RAYTRACING":"kParamRaytracing", "PARAM_UBWC_SWIZZLE":"kParamUbwcSwizzle",
        "PARAM_MACROTILE_MODE":"kParamMacrotileMode", "INFO_OFFSET":"kInfoGetOffset",
        "INFO_IOVA":"kInfoGetIova", "BO_READ_ONLY":"kBoGpuReadOnly", "BO_WRITE_COMBINED":"kBoWriteCombined",
        "SUBMIT_BO_READ":"kSubmitBoRead", "SUBMIT_BO_WRITE":"kSubmitBoWrite",
        "SUBMIT_COMMAND_BUFFER":"kSubmitCmdBuf",
    }
    constants = []
    for local, metal in constant_pairs.items():
        v = number(loaded["metal_header"], metal)
        assert number(local_header,"HAKIOS_GPU_"+local) == v
        constants.append(dict(name=local, value=v, evidence=[ref("metal_header",77,100)]))
    for local, kernel in [
        ("INFO_SET_NAME","MSM_INFO_SET_NAME"),("INFO_FLAGS","MSM_INFO_GET_FLAGS"),
        ("PREP_NO_SYNC","MSM_PREP_NOSYNC"),("SUBMIT_FENCE_FD_OUT","MSM_SUBMIT_FENCE_FD_OUT")
    ]:
        m = re.search(r"#define\s+"+kernel+r"\s+(0x[0-9a-fA-F]+|\d+)",loaded["kernel"])
        assert m and number(local_header,"HAKIOS_GPU_"+local)==int(m[1],0)
        constants.append(dict(name=local,value=int(m[1],0),evidence=[ref("kernel",163,172)]))

    contract = {
        "service": evidence(
            'IOService class/name HakiosGPU; kIOUserClientClassKey value HakiosGPUUserClient; '
            'IOServiceMatching("HakiosGPU"), IOServiceOpen type 0. No chip/family/capability properties '
            'are published by these four files. kIOUserClientClassKey string definition is outside scope.',
            ("kernel",303,323),("iokit",67,80)),
        "readiness": evidence("User client start requires hakios_gpu_ready(). Methods return NotReady after ready is lost, "
            "except SHOW; first reject null input, input >72 bytes, or missing fBuffers.",
            ("kernel",465,472),("kernel",1220,1232)),
        "framing": evidence("Selectors are request numbers, not Linux ioctl encoded words. Native little-endian 64-bit "
            "records; pointers are uint64 user virtual addresses, never portable file offsets. Kernel zero-fills union "
            "then copies 0..72 input bytes, with no per-selector exact-size check; successful output copies "
            "min(input size, output capacity), including padding. Metal passes same in/out pointer and length but does "
            "not validate returned length. This checker requires canonical full sizes; it is intentionally stricter.",
            ("kernel",71,161),("kernel",1220,1232),("kernel",1319,1325),("iokit",22,39)),
        "params": evidence("MsmParam pipe=1 from Metal; kernel only consumes param/value and delegates to external "
            "hakios_gpu_parameter, so actual supported IDs/values cannot be established here. gpu.hpp lists IDs "
            "1..10,0x10..0x13; its comment says GPU_ID value 0 on its upstream target, not selector 0. "
            "chipId() truncates uint64 to low uint32; faults() and chipId() return 0 on query failure. "
            "A635-family 0x06030500 in comments is not A530 device evidence.",
            ("kernel",48,64),("kernel",1235,1241),("metal_header",73,100),("metal_gpu",22,41)),
        "gem_new": evidence("Metal rejects size 0, rounds to 16 KiB, creates -> queries IOVA -> maps; info/map failure "
            "closes handle (map failure ENOMEM). Kernel rounds with round_page_64/PAGE_SIZE (numeric kernel PAGE_SIZE "
            "definition outside these files), accepts rounded 1..256 MiB, allocates handle 1..8192 and maps GPU pages "
            "in 4096-byte steps; GPU_READONLY bit passed to backend. Fresh memory is zeroed per source comment; kept "
            "same-client size/flags buffers can be returned uncleared unless gpu_keep=2. Flags do not select WC in gemNew.",
            ("metal_gpu",9,9),("metal_gpu",43,74),("kernel",605,665),("kernel",720,799)),
        "gem_info": evidence("Invalid handle BadArgument; info0 OFFSET=handle<<32 (not mapped by this offset); "
            "info1 IOVA=GPU address; info5 FLAGS=saved flags; info2 SET_NAME success/no storage; others Unsupported. "
            "len/pad not consumed by this implementation.",
            ("kernel",947,968)),
        "cpu_prep_fini": evidence("PREP ignores handle and waits for global hakios_gpu_last(), not BO-local fence. "
            "op&4 NOSYNC gives patience0; otherwise absolute continuous-clock deadline, clamped 0..5000ms with overflow "
            "handling. Poll delay 200us. Unfinished PREP returns Busy. FINI is success/no-op; Metal ignores its error.",
            ("kernel",174,180),("kernel",195,222),("kernel",259,279),("kernel",1238,1260),("metal_gpu",191,199)),
        "submit": evidence("nr_bos 1..8192, nr_cmds 1..128; copyin arrays of 16/32-byte records (VMError on copy failure). "
            "Non-type1 commands skipped. Type1 requires live command BO index, nr_relocs=0, offset/size divisible by4, "
            "uint64(offset)+size <= BO bytes. Zero command size and all-skipped types are not explicitly rejected. "
            "BO presumed/flags and request pipe/queue/syncobj fields are not used for scheduling or relocation here; "
            "invalid non-command BOs are skipped in cache/fence loops. Calls EXTERNAL hakios_gpu_submit(addresses,words,count); "
            "nonzero fence Success, zero Timeout. FENCE_FD_OUT sets fence_fd=-1, no fd exported.",
            ("kernel",970,994),("kernel",1009,1063),("kernel",1080,1099),("kernel",1141,1150)),
        "metal_submit": evidence("Metal rejects zero words, misaligned offset or out-of-BO end; command BO first/READ; "
            "other uses deduplicated by handle and READ/WRITE ORed. One CMD_BUF, byte size words*4, flags=pipe3D, "
            "fence_fd=-1, queueid supplied; bos/cmds are temporary uint64 process VAs valid for synchronous call. "
            "Fence output changes only on success. words*4 is uint32 in cmd.size; caller/newBo size constraints matter.",
            ("metal_gpu",140,181)),
        "wait_fence": evidence("WAIT rejects signed-wrap comparison (int32)(fence-last)>0, otherwise waits requested "
            "fence to absolute continuous deadline (max5000ms); unfinished Timeout. flags and queueid ignored. "
            "Metal clamps negative duration to0 and adds nowNs; signed addition overflow is not handled in deadline().",
            ("kernel",259,279),("kernel",1264,1282),("metal_gpu",11,19),("metal_gpu",183,189)),
        "queues_close": evidence("QUEUE_NEW returns ++fNextQueue; flags/prio unused, no demonstrated independent queues. "
            "QUEUE_CLOSE, SET_PARAM, CPU_FINI success/no-op. QUERY attempts copyout uint64 zero if data!=0 && len>=8, "
            "ignoring copyout return and query id/param. MADVISE retained=1. GEM_CLOSE invalid handle BadArgument, "
            "otherwise removes handle and defers busy memory reclamation, may retain memory indefinitely after hang. "
            "Metal unmaps then closes on owned BO release; disown unmaps without closing duplicate handle.",
            ("kernel",480,535),("kernel",1238,1241),("kernel",1283,1304),("metal_gpu",94,138)),
        "surface_user_show": evidence("SURFACE ID -> cached IOSurface memory, metadata/40-byte record, same live handle "
            "on repeat; handle=0xffffffff describes only and returns handle0. NotFound absent, Unsupported client-backed "
            "surface. USER_MEMORY address/size whole kernel pages, nonzero<=256MiB; prepares/wires pages, readonly "
            "fallback sets GPU_READONLY=0x2, VMError if neither prepare works. SHOW is CPU pixel copy to panel, not GPU submit: "
            "32-bit blue-first, width/height1..4096, rowBytes>=4*width, total<=256MiB.",
            ("kernel",809,870),("kernel",879,945),("kernel",1153,1179)),
        "mapping_cache": evidence("Map memory type=GEM handle; clientMemoryForType retains memory, marks seen=true, "
            "options=0. Metal IOConnectMapMemory64 uses kIOMapAnywhere, ignores supplied expected size and returned "
            "map size; map error nullptr, unmap result ignored. GEM allocations default descriptor WC, gpu_cached=1 "
            "ordinary cached; kBoWriteCombined numeric flag is declared but not consulted to choose cache mode. "
            "Submit flushes mapped cached GEMs, imported user pages by physical segments every submission, surfaces "
            "unless unchanged seed/unseen/<4ms; mapped surfaces every submission. Always calls to_memory(NULL,0) "
            "barrier. CPU_PREP waits; FINI performs no cache maintenance. to_memory/flush implementations outside scope, "
            "so no proven A530 coherency or invalidation guarantee.",
            ("kernel",332,333),("kernel",773,783),("kernel",997,1045),("kernel",1182,1199),
            ("metal_header",93,100),("iokit",41,57)),
        "returns": evidence("IOKit symbolic Success->0, NoMemory->ENOMEM, BadArgument->EINVAL, Timeout->ETIMEDOUT, "
            "Busy->EBUSY, NotFound->ENOENT, Unsupported->ENOSYS; all others (including NotReady/VMError) log and EIO. "
            "Surface API comment says ENOTSUP but actual transport returns ENOSYS. Actual numeric IOReturn definitions "
            "are outside permitted four sources; test shim numbers are surrogate tokens, not ABI evidence. "
            "Host errno numbers must not be serialized as Darwin errno numbers.",
            ("iokit",22,39),("metal_header",170,175),("kernel",920,920),("kernel",1228,1229)),
    }
    limitations = [
        "No A530 register, power, firmware, SMMU, command-stream, fence completion or cache hardware validation.",
        "No backend implementation read: external declarations/call sites establish dependency, not A530 support.",
        "Host execute()/a530_submit() returns unsupported for valid shapes and never issues or fabricates a fence.",
        "HakiosMetalCA caller and Metal capability implementation are outside four-file scope; no end-to-end compatibility claim.",
        "No runtime service discovery, device access, upstream edits, build/index/AGENTS changes or shared file writes.",
        "Native arm64 layout compile is host cross-compilation; it is not an arm64 execution or hardware test.",
    ]
    spec = dict(schema="hakios.gpu.abi.snapshot.v1", date="2026-11-23",
                requested_stack="A530 -> hakios-metal A5xx -> HakiosMetalCA",
                status="host_abi_validated_only", a530_submit="unsupported",
                sources=manifest, byte_order="little", pointer_bits=64,
                selectors=selectors, constants=constants, records=spec_records, contract=contract,
                limitations=limitations, results="evidence/gpu-metal-20261123/abi/results.json")
    dump(ABI / "spec.json",spec)
    dump(EVIDENCE / "line-evidence.json", snippets)
    # Compact verbatim, bounded extracts; not copies of the full repository.
    extract = []
    seen = set()
    for item in snippets:
        for r in item["refs"]:
            key = (r["source"],r["first_line"],r["last_line"])
            if key in seen: continue
            seen.add(key)
            extract.append(f'\n{r["path"]}:{r["first_line"]}-{r["last_line"]}\n')
            lines = loaded[r["source"]].splitlines()
            extract.extend(f"{i}: {lines[i-1]}\n" for i in range(r["first_line"],r["last_line"]+1))
    (EVIDENCE / "source-excerpts.txt").write_text("".join(extract),encoding="utf-8")
    print(f"PASS prepare: 4 pinned sources; {len(selectors)} selectors; {len(RECORDS)} records; "
          f"{total_checks} layout assertions; ABI header numbers match source")

if __name__ == "__main__":
    main()
