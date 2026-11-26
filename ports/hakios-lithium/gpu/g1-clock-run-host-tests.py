#!/usr/bin/env python3
"""Host-only G1 clock build/test and manifest. Never calls a device utility.

wsl -e python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/g1-clock-run-host-tests.py
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
PORT = ROOT / "ports/hakios-lithium/gpu"
OUT = ROOT / "evidence/gpu-metal-20261123/clock"
PRIOR = ROOT / "evidence/gpu-g0-20261123/power-zap"


def record(p, **kw):
    return {"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size,
            "sha256": hashlib.sha256(p.read_bytes()).hexdigest(), **kw}


def main():
    if os.name != "posix":
        raise SystemExit("Use WSL Python3")
    OUT.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(argv, **env):
        argv = list(map(str, argv))
        p = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True,
                           env={**os.environ, **env})
        commands.append(dict(argv=argv, exit_code=p.returncode,
                             stdout=p.stdout.strip(), stderr=p.stderr.strip()))
        print(f"[{p.returncode}] {' '.join(argv)}", flush=True)
        if p.stdout.strip():
            print(p.stdout.strip(), flush=True)
        if p.stderr.strip():
            print(p.stderr.strip(), flush=True)
        if p.returncode:
            raise RuntimeError("host check failed")

    # Verify existing pinned snapshot identities; no upstream checkout edits.
    prior = json.loads((PRIOR / "source-manifest.json").read_text())
    anchors = [
        ("micode", "drivers/clk/msm/clock-mmss-8996.c", [
            [90, 119, "alpha PLL P masks; graphics VCO2 400MHz..1GHz"],
            [233, 274, "MMPLL2 parent XO and configurable post-divider"],
            [371, 420, "old fixed table is NOT the Pro mux-divider path; Pro parents MMPLL9/2/8 selectors2/3/4"],
            [1367, 1384, "rbbmtimer XO 19.2MHz table"],
            [1669, 1681, "shared mmss_mmagic_ahb dependency"],
            [2390, 2449, "AHB dependency/no halt on disable, GFX BCR declaration, timer branch"],
            [3339, 3387, "MMPLL2/8 graphics VCO, rate0, no_prepared_reconfig, programmable ops"],
            [3522, 3533, "Pro MMPLL9 floor1248MHz: cannot use as an invented133MHz root parent"],
            [3537, 3575, "GPU misc RPM request is conditioned on is_v3_gpu; no copied Pro RPM assumption"],
            [3750, 3781, "Pro uses v3 fixes plus Pro speedbin0 fix"],
            [3948, 3968, "Pro GPU uses clk_ops_mux_div_clk"]]),
        ("micode", "drivers/clk/msm/gdsc.c", [
            [28, 43, "PWR_ON/control/reset/clamp fields; wait values"],
            [80, 112, "bounded status polling"],
            [141, 266, "root prerequisite, conditional reset, clamp, collapse, retention and delays"],
            [269, 360, "reverse collapse/clamp and no-status-check-on-disable"],
            [570, 600, "probe timing and retention flags"]]),
        ("micode", "arch/arm/boot/dts/qcom/msm8996pro.dtsi", [
            [821, 867, "Pro compatible; 133MHz GFX2/MX4 and GPU-MX4 for both speedbins"]]),
        ("micode", "arch/arm/boot/dts/qcom/msm8996.dtsi", [
            [811, 829, "MMSS vdd_dig is pm8994_s1_corner; XO and external GPLL dependencies"],
            [839, 846, "GFX plus independent MX/GPU-MX supply references"]]),
        ("micode", "arch/arm/boot/dts/qcom/msm-gdsc-8996.dtsi", [
            [158, 178, "CX status address/no-status-disable500us; GX clamp register"]]),
        ("micode", "arch/arm/boot/dts/qcom/msm8996-gpu.dtsi", [
            [101, 149, "seven clock dependencies; bus master26 slave512, index3 AB0 IB1600000KBps; CX/GX supplies"]]),
        ("linux", "drivers/clk/qcom/mmcc-msm8996.c", [
            [145, 174, "MMPLL2 offsets and post-divider graph"],
            [564, 615, "GFX root and timer source; Linux initialization is not assumed suitable"],
            [1676, 1743, "GPU branch addresses"],
            [3310, 3331, "GX parentCX, clamp, supply; different provider context"],
            [3616, 3629, "Linux probe edits shared MMSS; not copied by this core"]]),
    ]
    evidence = []
    for key, name, ranges in anchors:
        e = next(x for x in prior if x["key"] == key and x["path"] == name)
        p = ROOT / e["snapshot"].replace("\\", "/")
        r = record(p)
        assert r["sha256"] == e["sha256"] and r["bytes"] == e["bytes"]
        evidence.append({**r, "commit": e["commit"], "url": e["url"],
                         "anchors": [dict(start=a, end=b, purpose=t) for a, b, t in ranges]})
    extra_ranges = {
        "a5xx_reg.h": [[711, 711, "VBIF_VERSION exists; no RBBM_CHIP_ID definition in this pinned header"]],
        "adreno.c": [[669, 684, "chipid is DT property, not physical register read"]],
        "clock-alpha-pll.c": [
            [28, 48, "MODE/L/40bit alpha/USER/CONFIG/TEST layout"],
            [152, 188, "BYPASSNL -> 5us -> RESET_N -> LOCK -> OUTCTRL"],
            [308, 322, "OUTCTRL clear -> 1us -> BYPASSNL/RESET_N clear"],
            [368, 396, "32bit alpha rounding up and integer arithmetic"],
            [593, 655, "off-before-program, alpha<<8 and USER VCO/alpha bits"],
            [742, 789, "CONFIG/TEST writes conditional on explicit per-PLL values; MMPLL2 has none"]],
        "clock-generic.c": [
            [258, 313, "postdiv search: 532MHz/4 yields133MHz within graphics VCO"],
            [646, 705, "mux-divider parent selection and try_get_rate; deterministic cold MMPLL2 policy"],
            [770, 839, "parent programming before mux reparent"]],
    }
    for e in json.loads((OUT / "source-manifest.json").read_text()):
        p = ROOT / e["path"]
        r = record(p)
        assert r["sha256"] == e["sha256"] and r["bytes"] == e["bytes"]
        evidence.append({**r, "commit": e["commit"], "url": e["url"],
                         "anchors": [dict(start=a, end=b, purpose=t)
                                     for a, b, t in extra_ranges[p.name]]})
    local2 = ROOT / "evidence/touch-stage1-20261119/clocks/primary/micode/drivers/clk/msm/clock-local2.c"
    hwio = ROOT / "evidence/gpu-g0-20261123/core/micode-msm-clocks-hwio-8996.h"
    evidence.append(record(local2, anchors=[
        dict(start=50, end=77, purpose="root enable/update/status masks and source/div fields"),
        dict(start=733, end=775, purpose="retention PERIPH13/MEM14 bits and1us delay"),
        dict(start=800, end=875, purpose="branch enable and AH B5us delay; local disable semantics"),
        dict(start=1020, end=1055, purpose="BCR definition only; reset is explicit operation"),
        dict(start=1865, end=1916, purpose="postdivider4 maps to USER bits0x300"),
        dict(start=1978, end=2108, purpose="RCG source/div/update, force-root helper and bounded poll")]))
    evidence.append(record(hwio, anchors=[
        dict(start=58, end=66, purpose="MMPLL2/8 addresses"),
        dict(start=208, end=212, purpose="AHB/GFX/BCR/timer branch addresses"),
        dict(start=484, end=485, purpose="external GCC bus clocks0x309010/0x346018")]))
    stock = PRIOR / "sources/stock/lithium-stock.dts"
    evidence.append(record(stock, anchors=[
        dict(start=2737, end=2760, purpose="actual GX core_root force-enable; NO sw_reset/reset-aon; CX votable status"),
        dict(start=11308, end=11343, purpose="DT chip05030004, seven clocks and bus route"),
        dict(start=11459, end=11480, purpose="stock133MHz busindex3 min2 max4")]))
    sources = [record(p) for p in sorted(PORT.glob("g1-clock-*")) if p.is_file()]
    immutable = [record(ROOT / r["path"]) for r in evidence]
    build = Path(tempfile.mkdtemp(prefix=".host-", dir=OUT)).resolve()
    assert build.parent == OUT.resolve()
    try:
        base = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror"]
        for name, flags in [
            ("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]),
            ("optimized", ["-O2"]),
        ]:
            exe = build / name
            run([*base, *flags, PORT / "g1-clock-core.c", PORT / "g1-clock-test.c", "-o", exe])
            run([exe], ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run([*base, "--target=aarch64-none-elf", "-ffreestanding", "-O2", "-c",
             PORT / "g1-clock-core.c", "-o", build / "g1-clock-core.o"])
    finally:
        assert build.parent == OUT.resolve() and build.name.startswith(".host-")
        shutil.rmtree(build)
    assert sources == [record(p) for p in sorted(PORT.glob("g1-clock-*")) if p.is_file()]
    assert immutable == [record(ROOT / r["path"]) for r in evidence]
    # No binary output is retained under this owned evidence directory.
    assert not list(OUT.glob(".host*"))
    manifest = {
        "schema": 1,
        "created": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        "scope": "G1 Pro GPUCC/GDSC133MHz host executable preparation; disabled/unwired hardware entry",
        "device_operations": 0, "real_mmio": 0, "real_smc": 0, "spmi_module_edits": 0,
        "shared_parser_adapter_edits": 0, "upstream_checkout_edits": 0,
        "cpu_configuration_hz": 614400000,
        "public_header": "ports/hakios-lithium/gpu/g1-clock-core.h",
        "profile": {
            "cold_handoff_only": True, "selected_parent": "MMPLL2, source selector3",
            "xo_hz": 19200000, "pll_hz_configuration": 532000000,
            "L": 27, "alpha32_ceil": "0xb5555556",
            "alpha40_lo": "0x55555600", "alpha40_hi": "0xb5",
            "user_owned_mask": "0x01300f0f", "user_owned_value": "0x01200301",
            "vco": 2, "postdivider": 4, "gfx_root_config": "0x300",
            "gfx_root_hid_bypass": True, "gpu_configuration_hz": 133000000,
            "rate_measured": False,
            "analog_CONFIG_TEST_USERHI": "inherit under platform-qualified lease; snapshot/final compare; never guessed/written",
            "GX_BCR": "0x8c4020 bit0 must be deasserted; no reset pulse (stock DT has no sw_reset/reset-aon)",
            "clamp": "0x8c4300 bit0 only; refuse bit4 asserted; preserve other bits",
            "GDSC_wait_config": "REST2 FEW8 CLK_DIS2 =0x00282000; restore initial timings",
            "retention": "GFX CBCR MEM14 then PERIPH13,1us each after GX PWR_ON; restore after GX collapse",
            "timer": "XO19.2MHz /1, CFG1 at0x8c4094; branch0x8c40b0",
            "no_isense_enable": True,
        },
        "sequence": [
            "external SUPPLY receipt: GFX settled corner2, MX corner4, GPU-MX4; no SPMI writes here",
            "external BUS vote/ack: master26->slave512, index3, AB0/IB1600000KBps; no missing-vote fabrication",
            "external PLATFORM lease: XO19.2MHz, pm8994_s1_corner/MMPLL2 digital and analog context, MMSS shared AHB/fabric readiness, serialization",
            "snapshot exact22 GPUCC/GDSC/PLL fields; cold handoff checks before first write",
            "configure GDSC waits while collapsed; clear CX SW_COLLAPSE, barrier1us, poll CX_HW PWR_ON max500",
            "program cold MMPLL2 L/alpha/VCO/postdiv/main; BYPASSNL,barrier5us,RESET_N,LOCK max100,OUTCTRL",
            "force GFX root initiallyXO and verify ROOT_OFF=0; program selector3/divbypass,UPDATE max500,readback",
            "clamp clear; GX SW_COLLAPSE clear; barrier1us; PWR_ON max100; MEM/PERIPH retention; two1us powerup delays",
            "AHB enable5us/halt max200; timerXO/branch; GFX branch last; ROOT_OFF/config/PLL/branch fresh readbacks",
            "optional single source-backed hardware identity callback; DT is not a hardware read",
            "reverse: GFX branch off -> timer off/configrestore -> localAHB vote off -> GX collapse/PWR_OFF/clamp/retentionrestore with root still on -> GFXconfig/rootrestore -> PLL off/configrestore -> CX local collapse/timingrestore -> release PLATFORM/BUS/SUPPLY",
        ],
        "identity_binding": {
            "default": "unbound; returns LG1C_ID_UNBOUND after clock verification and reverse cleanup",
            "expected_dt_chip": "0x05030004",
            "evidence": "MiCode adreno.c669-684 reads qcom,chipid from DT; a5xx_reg.h has no RBBM_CHIP_ID definition",
            "physical_chip_id_address": None,
            "adapter_contract": "one minimum source-backed physical identity read only after fresh clock/GDSC checks; hardware_read and source_binding receipts mandatory",
            "DT_identity_never_substituted": True,
        },
        "specific_missing_dependencies": [
            "SUPPLY callback from independent power module: real GFX settled condition and shared MX/GPU-MX corner4 RPM acknowledgement",
            "BUS callback implementing active route26->512 index3 AB0 IB1600000KBps vote/ack and release",
            "bus/fabric clocks: gcc_mmss_bimc_gfx0x309010, gcc_bimc_gfx0x346018 plus actual parent readiness; two G0 readbacks alone are insufficient",
            "PLATFORM shared mmss_mmagic_ahb0x8c5024/ahb_clk_src0x8c5000 parent chain, XO and MMPLL2 pm8994_s1_corner vdd_dig/analog initialized context",
            "live scoped serialization/ownership lease and returned-access mapped read/write/barrier/delay callbacks; no requirement to prove all future TZ writes impossible",
            "physical A530 chip-ID register/decode provenance is missing in the supplied primary sources; identify callback deliberately unbound",
            "actual power/clock133MHz/readback/chip identity and hardware reverse cleanup have not been tested",
        ],
        "failure_contract": {
            "default_off": "enabled0 invokes no callbacks; no shared parser/adapter binding installed",
            "returned_faults": "one-shot reverse cleanup; every owned field checked; partial write admits only old/new owned state",
            "cleanup_fault": "first cleanup failure halts lower teardown and retains remaining dependency tokens; no retry/reboot",
            "CX_disable": "votable/no-status-disable; restore local vote/timing and100us delay, do not require all firmware votes gone",
            "polling_limitation": "bounded status loops and time budgets cover callbacks that return; not protection from a hung MMIO bus access",
            "identification_not_CP_init": "no GPMU/RBCCU/SPTP GPU writes, firmware loading, PAS13 or SECVID fallback",
        },
        "sources": sources, "primary_evidence": evidence,
        "commands": commands,
        "checks": [
            "ASan/UBSan and O2 tests cover every1285 callback position, returned fault, partial write, postaccess timeout and rollback",
            "stuck PWR_ON/PLL/root/update/branch statuses; reverse faults retain dependencies",
            "disabled/missing dependencies/cold handoff rejected before writes; identity blocked on fresh readback faults",
            "normal reverse restores all saved owned fields, preserves analog CONFIG/TEST and never writes BCR/GPU/SPMI",
            "AArch64 freestanding core compile passed; no actual kernel image build claimed",
            "primary snapshots and source inputs unchanged by tests; temporary executables/objects removed",
        ],
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(f"PASS G1 clock host manifest: {OUT / 'manifest.json'}", flush=True)


if __name__ == "__main__":
    main()
