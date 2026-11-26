#!/usr/bin/env python3
"""Focused actual-package/build/test readback. No device commands."""
import argparse
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import struct
import sys
import uuid

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
EVIDENCE=ROOT/"evidence/cpu-r39-20261121"


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    value=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(ok,text):
    if not ok: raise ValueError(text)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt",required=True)
    args=parser.parse_args()
    attempt="r37-frequency-"+args.attempt
    package=ROOT/"artifacts/hakios/lithium-ios16"/("phase4-r37-ram-frequency-"+args.attempt)
    manifest=read(package/"contract.json")
    long=manifest.get("cpu_rate_soak_long",False)
    require(type(long) is bool,"invalid long manifest flag")
    extended=long and manifest.get("stress_seconds")==1200
    runner=module("r39_admission","reports/local/run-cpu-r39-long-measure.py" if long else
                  "reports/local/run-cpu-r37-ram-measure.py")
    if extended:
        runner.configure(extended=True)
    admitted=runner.validate_package(package,package/"contract.json","frequency",attempt)
    contract=admitted["contract"]
    expected_seconds=runner.STRESS_SECONDS if long else 90
    require(contract["stress_requested"] and contract["stress_seconds"]==expected_seconds and
            contract["regulator_writes"]==0,"wrong rate screening scope")
    support_revision=contract.get("support_revision","01")
    require(support_revision in ("01","02","03","04"),"unknown support revision")
    support=read(EVIDENCE/("support"+support_revision+"/ram-support.json"))
    revision=contract.get("kernel_revision","rate01")
    build=read(EVIDENCE/("main/kernel-result-r39-"+revision+".json"))
    tested={}
    receipts=("thermal/test-results.json","soak/test-results.json") if support_revision!="01" else (
        "uefi/results.json","thermal/test-results.json","soak/test-results.json","supply/test-results.json")
    for relative in receipts:
        result=read(EVIDENCE/relative)
        require(result["passed"],"focused test failed: "+relative)
        production=[]
        for name,digest in result["source_sha256"].items():
            require(sha(ROOT/name)==digest,"tested source changed: "+name)
            if name.startswith("ports/hakios-lithium/"):
                port_relative=name.removeprefix("ports/hakios-lithium/")
                require(build["inputs"]["port_files"][port_relative]==digest and
                        support["inputs"]["port_files"][port_relative]==digest,
                        "build did not consume tested source: "+name)
                production.append(name)
        tested[relative]=production
    if support_revision!="01":
        high=EVIDENCE/"high-path/next-profile"
        result=read(high/"test-results.json")
        manifest=read(high/"patch-manifest.json")
        require(result["passed"] and result["old_core_scenario_traces_identical"]==128 and
                all(row["returncode"]==0 for row in result["runs"]),
                "EARLY profile focused tests failed")
        overrides={}
        if support_revision in ("03","04"):
            extension=read(high/"stage2/extension-test-results.json")
            require(extension["passed"] and len(extension["rates"])==9 and
                    all(row["returncode"]==0 for row in extension["runs"]),
                    "stock direct extension tests failed")
            overrides={row["path"]:row for row in
                       read(high/"stage2/extension-manifest.json")["patch_files"]}
            for name,digest in extension["source_sha256"].items():
                require(sha(high/name)==digest,"direct extension evidence changed")
        replacements={}
        exact_word_test=None
        if support_revision=="04":
            exact_word_test=read(EVIDENCE/"clkctl08-tests/test-results.json")
            require(exact_word_test["passed"] and
                    all(row["returncode"]==0 for row in exact_word_test["runs"]),
                    "empirical exact-word focused tests failed")
            for name,digest in exact_word_test["source_sha256"].items():
                require(sha(ROOT/name)==digest,"exact-word tested source changed")
        for row in manifest["patch_files"]:
            name=row["path"]
            if support_revision=="04" and name=="ports/hakios-lithium/diagnostics/cpu-perf-fixed-r38.h":
                digest=exact_word_test["source_sha256"][name]
                replacements[name]=digest
                continue
            digest=overrides.get(name,row)["proposed_sha256"]
            proposed_path=high/("stage2/proposed" if name in overrides else "proposed")/name
            proposed=proposed_path.read_bytes()
            live=(ROOT/name).read_bytes()
            # Windows git apply may materialize CRLF. Admit ONLY the two
            # exact encodings of the frozen tested bytes, not whitespace or
            # semantic edits; build binding below uses the ACTUAL byte hash.
            require(sha(proposed_path)==digest and
                    b"\r" not in proposed and
                    live in (proposed,proposed.replace(b"\n",b"\r\n")),
                    "live EARLY profile differs from tested proposal")
            replacements[name]=sha(ROOT/name)
        for name,digest in result["source_sha256"].items():
            require(sha(high/name)==digest,"EARLY test evidence changed")
        for row in read(high/"additional-inputs.json")["inputs"]:
            name=row["path"]
            if name=="scripts/package-hakios-cpu-r39.py":
                # Captured as a migration reference, not compiled/included by
                # the C tests. Its new choices are verified through the actual
                # image admission and payload readback, not its former hash.
                tested["migrated_packager_sha256"]=sha(ROOT/name)
                continue
            require(sha(ROOT/name)==replacements.get(name,row["sha256"]),
                    "EARLY test dependency changed: "+name)
        for name,digest in replacements.items():
            relative=name.removeprefix("ports/hakios-lithium/")
            require(build["inputs"]["port_files"][relative]==digest and
                    support["inputs"]["port_files"][relative]==digest,
                    "EARLY build did not consume tested source")
        tested["EARLY_direct_profile"]=replacements
    parallel_source="ports/hakios-lithium/diagnostics/cpu-rate-soak-parallel-r39.inc"
    if extended:
        require(revision=="rate10" and support_revision=="04" and
                contract.get("cpu_rate_soak_parallel") is True,"wrong extended kernel or support")
        result=read(ROOT/"evidence/cpu-r39-20261122/extended-tests/test-results.json")
        require(result["passed"] and parallel_source in result["source_sha256"],
                "extended worker tests failed or did not exercise production source")
        for name,digest in result["source_sha256"].items():
            require(sha(ROOT/name)==digest,"extended tested source changed: "+name)
            if name.startswith("ports/hakios-lithium/"):
                relative=name.removeprefix("ports/hakios-lithium/")
                require(build["inputs"]["port_files"][relative]==digest,
                        "kernel extended source differs")
        tested["parallel_extended"]=list(result["source_sha256"])
    elif contract.get("cpu_rate_soak_parallel"):
        result=read(EVIDENCE/("parallel-tests/long06/test-results.json" if long else
                              "soak/parallel-test-results.json"))
        require(result["passed"],"parallel worker tests failed")
        for name,digest in result["source_sha256"].items():
            if long and name==parallel_source:
                digest=result["tested_patched_source_sha256"]
                historical_worker=ROOT/"evidence/cpu-r39-20261121/parallel-tests/long06/proposed-parallel.inc"
                require(sha(historical_worker)==digest,"frozen long worker differs from tested source")
            else:
                require(sha(ROOT/name)==digest,"parallel tested source changed: "+name)
            if name.startswith("ports/hakios-lithium/"):
                relative=name.removeprefix("ports/hakios-lithium/")
                require(build["inputs"]["port_files"][relative]==digest,
                        "kernel parallel source differs")
        tested["parallel"]=list(result["source_sha256"])
    require(build["linked_return_calls"][0][1]=="_lithium_cpu_thermal_stop",
            "normal-return thermal cleanup not linked")
    codec=module("r39_fv_codec","scripts/build-hakios-r38-ufs-support.py")
    fd=(package/"lithium.fd").read_bytes()
    nested=codec.unique_file(codec.fv_files(fd),codec.NESTED_FV_GUID,0x0b)
    guided=codec.sections(nested["body"])
    require(len(guided)==1 and guided[0]["type"]==2,"unexpected guided sections")
    section=guided[0]["data"]
    require(str(uuid.UUID(bytes_le=section[4:20])).upper()==codec.LZMA_GUID,
            "unexpected compression GUID")
    offset,attributes=struct.unpack_from("<HH",section,20)
    require(24<=offset<len(section) and attributes==1,"bad compression extent")
    decoder=lzma.LZMADecompressor(format=lzma.FORMAT_ALONE,memlimit=128*1024**2)
    decoded=decoder.decompress(section[offset:],max_length=16*1024**2+1)
    require(decoder.eof and not decoder.unused_data and len(decoded)<=16*1024**2,
            "bad nested FV decode")
    sections=codec.sections(decoded)
    volumes=[x for x in sections if x["type"]==0x17]
    require(len(volumes)==1,"missing nested volume")
    modules=codec.fv_files(volumes[0]["body"])
    require(not any(x["guid"]==codec.UFS_GUID for x in modules),
            "physical UFS module present")
    fastboot=codec.unique_file(modules,"DC229FA1-8A73-4F05-AC63-BBD7CD6E8369",7)
    images=[x for x in codec.sections(fastboot["body"]) if x["type"]==0x10]
    require(len(images)==1 and images[0]["body"]==Path(support["fastboot_dxe"]).read_bytes(),
            "Fastboot DXE payload differs")
    index=sha(ROOT/".git/index")
    require(index=="fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2",
            "original index changed")
    result=dict(passed=True,device_operations=0,attempt=attempt,
                image_sha256=admitted["image_sha256"],cmdline=admitted["cmdline"],
                target_hz=contract["perf_fixed_target_hz"],tested_build_sources=tested,
                final_FD_physical_UFS_absent=True,final_FD_Fastboot_DXE_exact=True,
                timer_thermal_stop_linked=True,index_sha256=index,runtime_verified=False)
    (EVIDENCE/"main"/("package-check-"+args.attempt+".json")).write_text(
        json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in result.items() if k not in ("tested_build_sources","cmdline")},
                     indent=2))


if __name__=="__main__":
    main()
