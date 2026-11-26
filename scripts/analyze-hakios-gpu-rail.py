#!/usr/bin/env python3
"""Analyze the completed saved GFX rail cycle and USB stream. No device I/O."""
from datetime import datetime, timezone
from pathlib import Path
import importlib.util
import json
import re

ROOT = Path(__file__).resolve().parents[1]
RUN = ROOT / "evidence/gpu-metal-20261123/package/runs/gpu-g1-rail-01"
OUT = ROOT / "reports/hakios-ios16-gpu-g1-rail-01"


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def main():
    receiver = load(RUN / "usb-status.json")
    if receiver["status"] in ("waiting", "capturing"):
        raise RuntimeError("capture still active; preserve current run")
    result = load(RUN / "result.json")
    returned = load(RUN / "return-verification.json")
    console = (RUN / "console.log").read_bytes()
    raw = (RUN / "console.frames").read_bytes()
    spec = importlib.util.spec_from_file_location("rail_saved_usb_audit", ROOT / "scripts/hakios_usb_audit.py")
    audit_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit_module)
    audit = audit_module.audit(raw, console, RUN / "capture-host.py")
    lines = console.decode(errors="replace").splitlines()
    sections = {}
    traces = []
    temperatures = []
    for n, line in enumerate(lines, 1):
        for kind in ("begin", "end", "status", "restore"):
            if line.startswith("LITHIUM GPU rail " + kind + " "):
                sections.setdefault(kind, []).append(dict(line=n, **fields(line)))
        if line.startswith("LITHIUM GPU rail part="):
            traces.append(dict(line=n, **fields(line)))
        if line.startswith("LITHIUM CPU TEMP "):
            temperatures.append(fields(line))
    assert all(len(sections.get(k, [])) == 1 for k in ("begin", "end", "status", "restore"))
    end = sections["end"][0]
    status = sections["status"][0]
    restore = sections["restore"][0]
    checks = {
        "zero_errors": all(end[k] == "0" for k in ("error", "power_error", "cleanup_error")),
        "enable_ready_restore": all(end[k] == "1" for k in ("enable_ack", "ready", "restore_ack", "original_verified")),
        "no_retained_or_uncertain_state": all(end[k] == "0" for k in ("retained", "uncertain", "lease_held")),
        "released": end["released"] == "1",
        "exact_two_pmic_write_attempts": end["pmic_write_attempts"] == "2",
        "gpu_block_writes_zero": end["gpu_writes"] == "0",
        "gfx_ready_transition": int(status["before_gfx"], 0) == 0 and int(status["after_gfx"], 0) == 0x81,
        "mx_ready_unchanged": all(int(status[k], 0) == 0x81 for k in ("mx0", "mx1", "mx_after")),
        "source_bound_ready_mask": int(status["status_mask"], 0) == 0x81 and int(status["ready_samples"]) >= 2,
        "restore_command_completed": restore["attempted"] == restore["acknowledged"] == restore["original_verified"] == "1"
            and restore["transport_error"] == "0" and int(restore["cmd_status"], 0) == 1,
        "all_trace_data_valid": bool(traces) and all(t["valid"] == "1" and t["error"] == "0" for t in traces),
        "trace_enable_target_exact": [(int(t["sid"]), int(t["addr"], 0), int(t["raw"], 0))
            for t in traces if t["write"] == "1"] == [(3, 0x1746, 0x80)],
        "stop_temperature_records": sum(t.get("phase") == "stop" for t in temperatures) == 4,
        "source_loss_zero": "LITHIUM LOG writer_contention_dropped_bytes=0" in lines,
        "usb_timer_stream": audit["emitted_kernel_stream_through_timer_verified"],
        "same_phone_fastboot": returned.get("passed") is True,
    }
    passed = all(checks.values())
    report = dict(
        schema="lithium-gpu-g1-rail-runtime-v1",
        analyzed_at=datetime.now(timezone.utc).isoformat(),
        passed=passed, checks=checks, accepted_at=result["accepted_at"],
        image_sha256=result["image_sha256"], boot_args=result["boot_args"],
        begin=sections["begin"][0], end=end, status=status, restore=restore,
        traces=traces, usb=audit, receiver=receiver, return_verification=returned,
        cpu_peak_native_mC={a: max(int(t["native_mC"]) for t in temperatures
            if t.get("affinity") == a and t.get("valid") == "1")
            for a in sorted({t["affinity"] for t in temperatures})},
        cpu_temperature_is_not_gpu_temperature=True,
        analog_voltage_measured=False, gpu_full_power_on_passed=False,
        chip_id_read=False, cp_running=False, metal_acceleration_verified=False,
    )
    OUT.with_suffix(".json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (RUN / "usb-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    OUT.with_suffix(".md").write_text(f"""# GPU G1 GFX 受控供电循环 01 · 2026-11-25

**本轮{'通过' if passed else '未完整通过'}：GFX 使能、ready 观察、恢复关闭。** 未打开 GPU consumer，尚未读取芯片 ID。

- 单次 RAM 临时启动：`{result['accepted_at']}`，CPU614.4 MHz配置模型，XNU3核。
- 镜像：`{result['image_sha256']}`。
- SID3 / PPID317 / APID52，唯一 PMIC 寄存器 `0x1746`：写 `0x80`，随后恢复原值 `0x00`，共2次。
- GFX STATUS `0x1708` 从 `0x00` 到 `0x81`，至少两次 ready；MX 两次前置和后置均 `0x81`。
- 供电数字配置逐项双遍回读，恢复后原值一致；voltage/MX/GDSC/GPU clock 写入均0。
- 核心和清理 error0，elapsed `{end['elapsed_us']}` µs，`{end['accesses']}` read/write32 accesses，120 observer reads，2 PMIC writes。
- 这是 PMIC 数字 ready 证据，不是模拟电压测量，也不是整个 GPU 上电或 CP 验收。

## 生命周期与证据

正常180秒timer、四路stop温度、源日志丢失0，USB drain和独立逐字节重放：`{audit['emitted_kernel_stream_through_timer_verified']}`。
保存 `{audit['frames']}`帧 / `{audit['raw_bytes']}` raw bytes / `{audit['console_bytes']}` console bytes。
接收器终态 `{receiver['status']}` / `{receiver.get('failure', '')}` 原样保留。
一次Fastboot devices/product核验：`{returned['passed']}`，时间 `{returned.get('finished_at')}`。
未单独捕获固件内部 Prepare/Commit；不会用USB断开单独代替自动返回证据。

## 实现与复核

- Aristotle：`gpu/g1-status-binding.h`、`g1-status-core.h`；420场景 / 2,971,158 checks的专项和freestanding AArch64对象。STATUS来源绑定到固定QTI authored common-driver源码镜像及本机TYPE1c/SUB09，镜像并非官方发布来源，该来源限制保留。
- Gauss：rail packager/runner草稿；中途服务配额中断。主代理接手路径兼容修正、实际打包/读回和执行，没有等待或自动重试。
- 主代理：`gpu/g1-rail-adapter.inc`、默认关闭`gpu_rail_cycle`准入、19参数专项、真实adapter mock接缝专项、完整HakiOS链接、冻结与全部设备操作；独立复核真实轨迹、120 observer读、两次写入和USB。

完整内核SHA256：`d2ab3bc31a010fb597aeeb9174cd9d6afac05b95f44579b257513a8bd9f6f5f9`。
源码可回退补丁与构建证据：`evidence/gpu-metal-20261125/rail01/`。

## 仍需推进

MX/RPM ACK、GPU bus/clock、CX/GX、chipID、PAS13、SMMU、CP/fence以及Metal计算/绘制/界面合成都未获本次硬件证据。
下一步先连接RPM通道与GPU专用总线投票，保留CPU614/R20/low01及旧冻结。
本轮不使用物理UFS、分区操作、刷写、ADB或QEMU。

```text
{result['boot_args']}
```
""", encoding="utf-8")
    print(json.dumps({"passed": passed, "checks": checks, "end": end}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
