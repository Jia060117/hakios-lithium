#!/usr/bin/env python3
"""Write the source-bound host-phase report after R33 independent readback."""
from datetime import datetime, timezone, timedelta
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'evidence/fastboot-return-impl-20261120/main'
OUT=ROOT/'artifacts/hakios/lithium-ios16/phase4-r33-return-fastboot'


def read(p):
    return json.loads(Path(p).read_text())


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    contract=read(OUT/'contract.json')
    offline=read(EVIDENCE/'final-offline-readback.json')
    pointer=read(EVIDENCE/'host-tests-final.json')
    gate=Path(pointer['path'])
    assert offline['passed'] and offline['image_sha256']==contract['image_sha256']
    assert sha(gate)==pointer['sha256'] and read(gate)['passed']
    for p,d in read(gate)['source_sha256'].items(): assert sha(p)==d,p
    checks=read(ROOT/'reports/hakios-ios16-return-fastboot-build-checks.json')
    base=read(gate.parent/'all-peripherals/previous/passed.json')
    follow=read(gate.parent/'all-peripherals/passed.json')
    now=datetime.now(timezone(timedelta(hours=8))).isoformat()
    stem=ROOT/'reports/hakios-ios16-return-fastboot-r33-prepared'
    assert not stem.with_suffix('.json').exists()
    report=dict(schema=1,revision='R33-return-fastboot',recorded_at=now,
        passed=True,phase='host_prepared_no_device_operations',
        image_path=contract['image_path'],image_sha256=contract['image_sha256'],
        image_bytes=contract['image_bytes'],boot_args=contract['cmdline'],
        host_test_gate=str(gate),host_test_gate_sha256=sha(gate),
        normal_instruction_scans=len(checks['instruction_scans']),ram_instruction_scans=7,
        full_hakios_build=True,d22ap_identity=True,launchd_jobs=413,
        host_feature_suites=6,peripheral_suites=len(base['tests'])+len(follow['new_suites']),
        package_unit_tests=49,evidence_files_rehashed=offline['evidence_files_rehashed'],
        default_off=True,normal_timer_only=True,two_usb_drains_required=True,
        usb_session_change_rejected=True,panic_and_unknown_retained=True,
        automatic_retry=False,hardware_fastboot_verified=False,physical_touch_verified=False,
        ufs_auto_return_supported=False,ram_only_initial_acceptance=True,
        no_touch_transaction_requested=True,device_queries=0,device_boots=0,device_resets=0,
        partition_operations=0,adb_commands=0,qemu_used=False,
        display_baseline='R20-display',old_frozen_baselines_unchanged=True,
        boot_totals=dict(hakios=56,ios16=25),next='Separate one-time authorized hardware Fastboot-return acceptance, then next touch revision',
        evidence_sha256={str(p.relative_to(ROOT)):sha(p) for p in [
            OUT/'contract.json',OUT/'package-checks.json',EVIDENCE/'final-offline-readback.json',
            ROOT/'reports/hakios-ios16-return-fastboot-build.json',
            ROOT/'reports/hakios-ios16-return-fastboot-build-checks.json',
            EVIDENCE/'linked-feature-review.json',EVIDENCE/'native-report-path-correction.json']})
    stem.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    text=f"""# R33：自动返回 Fastboot 主机阶段

记录时间：{now}。已实现、完成完整构建和主机测试，并冻结独立验收包；**实际自动复位与 Fastboot 去向未验证**。本轮没有查询、启动或复位手机。触摸新事务尚未开始；必须先完成自动返回的真机验收。

镜像：`{contract['image_path']}`，{contract['image_bytes']} 字节。

SHA256：`{contract['image_sha256']}`。

## 实现及边界

- `ports/hakios-lithium/return-fastboot/contract.h`：默认关闭的严格参数解析、128 字节版本化状态、同次启动 cookie、独立返回 reason、两次真实 USB 排空结果、一次性 claim。拒绝非法/重复参数、未知版本、失配、panic、checkpoint 和重复尝试。
- `kernel.inc`、`loader.inc`、补丁 `0023-lithium-return-fastboot.patch`：正常 timer 收尾后提交资格，返回 UEFI 才倒计时和调用固件。UEFI 不重新接管 USB 来伪造第二次排空。设备 bulk 完成不等同于主机日志已持久化；摘要的额外 retained RAM 内容也不冒称全部经 USB 传输。
- `firmware-protocol.h`、`LithiumFastbootDxe`：新版本化 prepare/commit 接口，旧固件缺失该接口时保留摘要，不调用忽略模式参数的普通 ResetSystem。
- `reset-core.h`：按固定主源及本地原厂 bootloader 的静态证据，核对 SPMI v2、PPID/通道和真实 EE0 owner，设置并读回 PMIC/IMEM 的 bootloader 原因，配置两 PMIC 的复位，并在可用性查询支持时单次调用 arbiter halt，最后单次 PS_HOLD。任何失败不自动重试或切换普通重启。
- `storage.inc`、`profile.h`、`init.inc`：首轮仅明确隔离 RAM、无物理 UFS provider、无存储桥/写入、单核 affinity1。新 PID1 软件验收 hold 不携带触摸探测参数、不挂载 Data，也不调用 reboot。
- 开关为 `return_fastboot=1 return_fastboot_delay=10`，默认关闭，延迟范围 0–60 秒。本包保留 180 秒正常内核观察时间，之后预期在排空成功时显示摘要 10 秒并请求一次 Fastboot 复位。

目前**只支持隔离 RAM 诊断的自动返回**。UFS 普通系统退出仍拒绝自动复位：现有 Data 同步 API、后台写入者停止及 journal 错误传播不足以证明完整收尾，不能把 ENOENT 或两个 API 返回 0 当作安全资格。这不改变长期系统放在 UFS 的路线。

## 构建与测试

- 正常完整 HakiOS/XNU8796、D22AP loader、Mu、413 项用户态任务全部构建，82 项源码审查及 {len(checks['instruction_scans'])} 项指令扫描通过；另实际构建移除物理 UFS provider 的 RAM Mu 与新加载器，7 项额外 ISA 扫描通过。
- 六个专项入口：契约、reset core、实际 kernel/loader/USB 集成、PID1 hold、实际 Dxe 适配、打包器。均运行真实生产头/include 的主机测试；与硬件验证严格区分。另 {report['peripheral_suites']} 套既有外围回归通过。
- 契约 O1/O2 各 254525 checks；reset core 各 2145 场景；kernel/loader 集成各 279 场景；PID1 各 81 场景；Dxe 覆盖 RAM/普通/关闭三种构建及故障注入；打包单元测试最终 49 项全过。完整原始命令、源哈希与退出码在 `{gate}` 及其引用中。
- 打包读回核验 Android header/SHA1、gzip/DTB、BootShim、Mu、XNUP、加载器 FAT、真实 APFS 程序/权限和 413 项任务。主代理另以独立字节解析器复核 {offline['evidence_files_rehashed']} 项证据文件、唯一对齐 64 字节的初始 handoff 记录及 arm64 类型；未以 arm64e 或历史 7195 替代。

可复现主机命令（构建任务必须在项目批准的 WSL 资源域内）：

```text
python3 -B scripts/build-hakios-return-fastboot.py normal
python3 -B scripts/build-hakios-return-fastboot.py ram-support
python3 -B scripts/test-hakios-return-fastboot.py
python3 -B scripts/finalize-hakios-return-fastboot-build.py
python3 -B scripts/package-hakios-return-fastboot.py
python3 -B scripts/check-hakios-return-fastboot-offline.py
```

上述构建/冻结入口拒绝覆盖已存在产物；这些命令说明流程，并非要求覆盖当前冻结包重新运行。实际执行的失败日志和中间候选均保留。

## 并行执行与主代理复核

| 子代理 | 交付 | 主代理复核 |
| --- | --- | --- |
| McClintock / Boyle（前置） | 原厂复位机制、协议初版 | 核对固定主源/DTB 与实际结束链；保留未验证项 |
| Dirac / Socrates | 独立协议审查与 F1–F4 修正 | 核对真实 repro、两次排空时序、直接 claim 负例、独立 ABI 与重叠拒绝；重跑通过 |
| Cicero | reset core 和故障测试 | 核对源哈希、opcode、owner 读回、单次 halt/PS_HOLD；实际编入 Dxe |
| Lorentz | 实际 kernel/loader/USB 及 PID1 测试 | 修复并复测 PID1 重复/空白参数；保持全部旧失败证据 |
| Hypatia | 实际 Dxe mock/Mu 头编译 | 修复 post-MMIO 到期仍改变输出的 Read32 契约，最终源哈希一致 |
| Aquinas | 严格 R33 打包器、原生工具回归 | 真实生产发现 LLVM alias 连接问题后补入真实 tool_closure 回归，49 项通过 |

共享接口、补丁、USB 集成、DSC/FDF/内存映射、完整构建及冻结由主代理负责。没有子代理执行手机操作。主代理最终还发现汇总脚本引用迁移前契约报告，已改为正确路径并逐项核对 native source/header/runner 哈希，重新生成整套 gate 与最终包。

## 已修缺陷与保护

保留了原子 API 编译错误、Read32 输出契约、PID1 参数、LLVM driver alias 和测试报告路径错误的原始证据；未移除功能、测试或异常保护。历史 R9/0021 总回归门禁未因此被冒称通过。

R20-display 仍为唯一已验收显示基线；R32 及旧冻结包/索引内容保持。HakiOS 启动总数仍 56，iOS16 为 25。本阶段未消费新的临时启动。

## 上机验收和之后的触摸

按仓库每轮设备操作确认规则，先说明此新包范围并取得对应确认，再单次身份/电量预检、临时启动并收集 USB 日志；不刷写、不读取分区、不做 I2C/HID、不自动重试。

这次预期只有：单核 affinity1 → RAM PID1 软件 hold → 180 秒正常 timer → 两次排空 → UEFI 摘要 10 秒 → PMIC/IMEM/PS_HOLD 单次复位 → 原厂 Fastboot。复位后需要限定的只读枚举及用户目视确认；如果停在摘要，保留失败文字，不手动伪报成功。

实际 SPMI/PMIC 访问、secure-world 行为、复位原因保留和最终 Fastboot 去向均未验证。软件预算不能中断永久挂起的 MMIO/SMC。只有本轮通过后才继续 R32 触摸的 NACK/FAIL/INVALID_READ_ADDR 调查；先核对 FIFO 命令与板级前提，不猜地址/电源或直接放宽错误掩码。
"""
    stem.with_suffix('.md').write_text(text)
    print('Report written:',stem.with_suffix('.md'))


if __name__=='__main__':
    main()
