#!/usr/bin/env python3
"""Freeze the BIMC debug-counter RAM experiment using existing unchanged RAM codecs."""
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("private_rpm_g0_codecs", ROOT/"scripts/package-hakios-gpu-g0.py")
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)
OLD = C.PACKAGE_ROOT/"g0-01"
OUT = ROOT/"evidence/gpu-metal-20261123/package/g1-bimc-measure-01"
BUILD = ROOT/"evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-10.json"
ARGS = C.BOOT_ARGS.replace("gpu_stage=0", "gpu_stage=1") + " gpu_bimc_measure=1"
OLD_ARGS = C.BOOT_ARGS
C.BOOT_ARGS = ARGS
C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-10")
C.KERNEL_REPORT = BUILD
C.KERNEL_MARKERS += (b"gpu_bimc_measure", b"LITHIUM GPU bimc measure begin", b"LITHIUM GPU bimc measure end")
old_validate = C.validate_args
def validate(args):
    C.require(args == ARGS, "BIMC measurement arguments differ")
    old = C.BOOT_ARGS
    C.BOOT_ARGS = OLD_ARGS
    try: old_validate(args.removesuffix(" gpu_bimc_measure=1").replace("gpu_stage=1", "gpu_stage=0"))
    finally: C.BOOT_ARGS = old
C.validate_args = validate

def verify_package(package=OUT):
    C.require(Path(package).resolve() == OUT.resolve(), "BIMC package path differs")
    contract = C.read_json(OUT/"contract.json")
    validate(contract["cmdline"])
    C.require(contract["schema"] == "lithium-gpu-g1-bimc-measure-ram-v1" and
              contract["candidate"] == "GPU-G1-bimc-measure-01" and
              contract["regulator_writes"] == 0 and contract["gpu_power_writes"] == 0 and
              contract["pmic_commands"] == 0 and contract["register_read_cap"] == 4096 and
              contract["device_operations"] == 0 and contract["stage1_armed"] and contract["bimc_measure_only"] and contract["debug_store_cap"] == 16 and contract["debug_timeout_us"] == 45000 and not contract["gpu_submission_armed"],
              "BIMC package scope differs")
    components = {}
    for name, expected in contract["components"].items():
        path = OUT/name
        C.require(path.resolve().parent == OUT.resolve() and not path.is_symlink(), "invalid component path")
        data = path.read_bytes()
        C.require(C.meta(data) == expected, "saved component differs: "+name)
        components[name] = data
    image_name = contract["image"]["path"]
    C.require(C.meta(components[image_name]) == {k:contract["image"][k] for k in ("bytes","sha256")},
              "image contract differs")
    actual = C.verify_layers(components[image_name], components)
    C.require(all(actual[k] == contract[k] for k in actual), "RPM layer verification differs")
    return dict(passed=True,image=str(OUT/image_name),image_sha256=C.sha(components[image_name]),
                image_bytes=len(components[image_name]),cmdline=ARGS,component_count=len(components),
                final_fv=actual["final_fv"],device_operations=0,runtime_verified=False)

def main():
    C.require(not OUT.exists(), "preserve frozen BIMC measurement package")
    old = C.read_json(OLD/"contract.json")
    inputs = C.reused_inputs()
    kernel, build, build_data = C.kernel_input(BUILD)
    raw_old, dtbs, header, previous, old_size = inputs["r34"]
    payload = C.CODEC.encode_payload([
        ("kernel",kernel),("ramdisk",inputs["root"]),("resident",previous["resident"]),
        ("cmdline",ARGS.encode()+b"\0"),("kernel.ver",previous["kernel.ver"])])
    shim,_ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT],old_size,len(payload))
    raw = shim+inputs["fd"]+payload+C.NOP*((-len(payload)%16)//4)
    fat = (OLD/"loader.fat").read_bytes()
    image = C.LIVE.make_android(raw,dtbs,fat,header)
    name = "lithium-hakios-ios16-gpu-g1-bimc-measure-01.img"
    components = {n:(OLD/n).read_bytes() for n in ["root.apfs","flashboot.apfs","loader.efi",
        "fastboot-dxe.efi","lithium.fd","ram-support.json","ram-root.json"]}
    components.update({"hakios-kernel":kernel,"payload.bin":payload,"Image":raw,
        "loader.fat":fat,"kernel-result.json":build_data,"boot-args.txt":(ARGS+"\n").encode(),name:image})
    checked = C.verify_layers(image,components,inputs["r34"])
    C.require(build["gpu_bimc_measure"] and not build["pmic_write_entry_enabled"], "wrong kernel capture scope")
    contract = dict(old)
    contract.update(schema="lithium-gpu-g1-bimc-measure-ram-v1",candidate="GPU-G1-bimc-measure-01",
        profile="gpu-g1-bimc-measure-ram",cmdline=ARGS,boot_args=ARGS,gpu_bimc_measure=True,
        gpu_stage=1,gpu_power_diag=False,register_read_cap=4096,pmic_commands=0,gpu_power_writes=0,
        message_ram_io_cap=0,rpm_resource_requests=0,
        stage1_armed=True,bus_read_only=False,bimc_measure_only=True,debug_store_cap=16,debug_timeout_us=45000,rpm_open_only=False,gpu_submission_armed=False,hardware_verified=False,approval_required=True,
        kernel_builds=1,device_operations=0,inputs={**old["inputs"],"kernel":str(C.FROZEN_KERNEL),
        "kernel_report":str(BUILD),"kernel_sha256":build["sha256"]},
        image={"path":name,**C.meta(image)},components={n:C.meta(b) for n,b in components.items()},**checked)
    OUT.mkdir(parents=True)
    for n,b in components.items(): (OUT/n).write_bytes(b)
    (OUT/"contract.json").write_text(json.dumps(contract,indent=2)+"\n")
    saved={n:(OUT/n).read_bytes() for n in components}
    C.require(all(C.meta(saved[n])==contract["components"][n] for n in saved),"saved components differ")
    final=C.verify_layers(saved[name],saved)
    C.require(final==checked,"saved image readback differs")
    result=dict(passed=True,image=str(OUT/name),sha256=C.sha(image),bytes=len(image),
                cmdline=ARGS,device_operations=0,readback=final)
    (OUT/"package-result.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ["passed","image","sha256","bytes","device_operations"]}))

if __name__ == "__main__": main()
