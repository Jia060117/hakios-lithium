#!/usr/bin/env python3
"""Publish consistent local project records for the verified HakiOS first-test package."""
from datetime import datetime
import hakios

def edit(name, before, after):
    path = hakios.ROOT / name
    text = path.read_text(encoding='utf-8')
    hakios.require(text.count(before) == 1, 'Documentation anchor differs: ' + name)
    path.write_text(text.replace(before, after, 1), encoding='utf-8')

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    package = hakios.read(hakios.ROOT / 'reports/hakios-test-package.json')
    preflight = hakios.read(hakios.ROOT / 'reports/hakios-device-preflight.json')
    hakios.require(package['inputs'] == hakios.inputs(spec) and package['static_package_verified'] and
        package['image_sha256'] == hakios.sha(hakios.ROOT / package['image_path']), 'Package is stale')
    state = hakios.read(hakios.ROOT / 'project.json')
    state['active_kernel'].update(built_for_lithium=True, packaged_for_lithium=True,
        status='Complete HakiOS 8796.121.2 built and statically verified in the first lithium platform-test image; no hardware acceptance')
    state['stage'] = 'P1: first HakiOS lithium platform-test package ready; hardware test pending'
    state['acceptance'].update(P0='bounded_plan_and_build_inputs_closed', P1='test_package_ready_hardware_pending')
    state['current_device_status'] = 'Latest USB query found no authorized MIX in ADB/Fastboot; last kernel screen remains the historical 7195 panic'
    state['device_state'].update(live_usb_requeried=True, latest_usb_query_report='reports/hakios-device-preflight.json')
    state['work_status'].update(status='project_refactor_complete_platform_test_ready',
        kernel_builds_performed=True, device_operations_performed=False, device_queries_performed=True,
        device_boots_performed=False, regression_tests_passed=9)
    state['test_package_report'] = 'reports/hakios-test-package.json'
    state['p0_closure_report'] = 'reports/hakios-p0-closure.json'
    state['current_report'] = 'reports/hakios-platform-test-ready.md'
    hakios.write(hakios.ROOT / 'project.json', state)
    report = f'''# HakiOS lithium 首个实机测试包

{datetime.now().astimezone().isoformat()}。工程重构、完整依赖准备、内核/加载器/固件构建与临时启动包静态核验已完成，**可进入 MIX 真机平台测试，尚无 HakiOS 真机通过证据**。

唯一内核基线是完整 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU `8796.121.2`。Apple XNU 8796、完整 HakiOS patch/files/drivers、HFS、pthread 和 firehose 均沿上游流程准备。原始 checkout 保持 clean；98 份旧源码、75 份原脚本及6份历史文档逐字节保持冻结。没有执行旧7195内核或 QEMU。

本机差异限于 ARMv8.0 能力选择、GIC stride、BGR24 framebuffer 串口组件、MIX Mu 板层和加载契约。复用来源及哈希见 `hakios-firmware-sources.json`、平台补丁 provenance 和 `hakios-dependencies.json`。完整 Mu 固件使用其正常依赖，BDS 直接 LoadImage/StartImage HakiOS EFI；BootServices 保留供上游日志返回机制使用。首包不启用 UFS、GPU、触摸及其他未适配的设备驱动。

## 固定测试输入

| 项目 | 值 |
| --- | --- |
| 临时镜像 | `{package['image_path']}` |
| 镜像长度 | {package['image_bytes']} 字节 |
| 镜像 SHA256 | `{package['image_sha256']}` |
| 内核 SHA256 | `{package['kernel_sha256']}` |
| 加载器 SHA256 | `{package['loader_sha256']}` |
| 固件 SHA256 | `{package['fd_sha256']}` |
| XNUP SHA256 | `{package['payload_sha256']}` |
| 初始程序 | 上游静态 `userland/src/hello.c`，通过 mockfs 作为进程1 |
| 初始参数 | `{package['cmdline']}` |
| XNU 独占 | `[0x100000000,0x120000000)`，Mu 分配 HOB，加载器核对描述符 |
| XNUP 保留 | `[0x120000000,0x124000000)` |
| EFI FAT | `[0x42a00000,0x42b00000)`，1MiB只读 RAM 卷 |

原厂20份 DTB 未修改；EFI FAT 回读、Linux Image/XNUP 头、压缩镜像回读一致。内核、loader、hello 执行区检查未发现非兼容 PAN/UAO、LSE 或 PAC 指令。159 个 XPACLRI 属于 ARMv8.0 安全 HINT 编码；SHA256 K256 的256字节常量与固定源码完全一致，已按数据区处理，未将反汇编数据误判为指令。

内核编译与链接通过；完整上游 `tools/make-kernel.sh` 仅接受它明确规定的 kext 符号集检查失败，其他构建错误均拒绝。最初递归 Make 自动提升并发到5时已停止，随后显式 MAKEJOBS=--jobs=2；缺少 python 命令的主机依赖已修复。最终流程保持 WSL 8GB/4CPU/2GB swap、每任务6GB/200%CPU、并发2、大任务顺序执行。

## 实机步骤与验收

当前预检未发现手机，未发送启动、刷写、清除或格式化命令。手机 userdata 仍按既有记录为 iOS HFSX，普通重启不能保证进入 Android。连接原授权 MIX 并以电源+音量下进入 Fastboot 后运行：

```powershell
Set-Location F:\\IOS-MIX1
python scripts/boot-hakios-lithium.py
python scripts/boot-hakios-lithium.py --boot
```

新入口重新核对授权设备标识、产品、解锁状态和 userdata 几何，再对冻结 SHA256 的 HakiOS 镜像执行一次 `fastboot boot`。不持久刷写。记录屏幕中的 HAKIOS UEFI/BDS、XNULOADER、内核日志和 `Hello from user space on Darwin!`；单次最小程序成功只证明平台闭环。P1仍需真实 timer IRQ、线程定时睡眠/唤醒及切换连续10分钟、3次复现。内核返回后加载器保持日志画面，需按键回 Fastboot。

## 兼容边界

原始 D20AP DMG 已重新只读挂载并核对真实 dyld、launchd、backboardd、SpringBoard 的 14.8/18H17 身份与 Mach-O 依赖。commpage 定义差异和实际共享缓存字符串见 `hakios-ios14-boundary.json`，字符串存在不能证明接口兼容或 GL renderer 可达。

A530 的有限候选是固定 ios-sailfish 中的同代 buffer/submit/SMMU/固件硬件组件，加 Mesa freedreno GLES，再按 HakiOS IOSurface 接口集成；该候选原针对iOS16，18H17 EAGL/CoreAnimation ABI及GL回退仍需验证。没有采用 A642L 的 GMU/RPMh/a660 路径，也没有宣称 A530 Metal 已支持。P0 闭合的是输入、方案和加载实现，P2/P3仍须验证 iOS14 私有接口与图形闭包；需要重建通用服务或整条私有图形栈时按 PLAN 提交具体阻塞。
'''
    (hakios.ROOT / 'reports/hakios-platform-test-ready.md').write_text(report, encoding='utf-8')
    readme = hakios.ROOT / 'README.md'
    text = readme.read_text(encoding='utf-8')
    start, end = text.index('## 当前结果'), text.index('## 活动结构')
    text = text[:start] + '''## 当前结果

完整 HakiOS 依赖、MIX 平台补丁、Mu 板层、内核与上游 loader/hello 已构建。首个临时启动镜像通过静态检查；9项路线隔离回归及上游设备/config/驱动清单检查通过。详细输入、哈希和上机步骤见 [首包记录](reports/hakios-platform-test-ready.md)。

P0 的构建输入、有限适配方案和加载实现已闭合；iOS14 用户空间、A530 GPU、触摸和真机运行仍未验收。未运行 QEMU 或旧7195内核。当前 USB 未发现手机，未启动或写入设备。userdata仍为iOS HFSX，失败后用电源+音量下回Fastboot；恢复Android须原厂recovery重建userdata。

''' + text[end:]
    text = text.replace('上游device.conf、configs、soc接口的MIX设备层；当前为配置骨架', 'MIX配置、平台补丁、完整Mu板层与加载契约')
    text = text.replace('`hakios-kernel`仅在P0闭包和本机加载契约有对应证据后执行完整上游构建，当前拒绝。',
        '`hakios-kernel`在有效P0证据下执行完整上游构建；`hakios-deps`、`hakios-support`、`hakios-package`分别准备依赖、构建支持组件与静态核验测试镜像。')
    text = text.replace('后续先完成P0的依赖与SDK固定、ARMv8.0和加载契约、iOS14私有接口及A530全路径审查，再进入P1。',
        '下一项是P1真机平台测试；P2/P3继续核对18H17私有接口及A530 GLES候选的实际兼容。')
    readme.write_text(text, encoding='utf-8')
    plan = hakios.ROOT / 'PLAN.md'
    text = plan.read_text(encoding='utf-8')
    a = text.index('项目入口和状态已完成重构')
    b = text.index('\n\n**活动工作边界**', a)
    text = text[:a] + '项目重构、完整依赖、内核和Mu/loader/hello构建及首包静态检查已完成。P0输入/有限方案/加载实现闭合，首个MIX真机平台测试包就绪；尚未启动HakiOS，iOS14与A530兼容仍未验证。当前结果见 reports/hakios-platform-test-ready.md，P0证据见 reports/hakios-p0-closure.json。' + text[b:]
    a = text.index('**下一项工作**')
    b = text.index('审核过程原文保留', a)
    text = text[:a] + '''**下一项工作**

执行首个HakiOS MIX临时启动测试，核对UEFI/loader/内核/hello同次屏幕证据，再推进10分钟timer与线程验收。当前手机未在ADB/Fastboot连接，未发出启动命令。测试入口 scripts/boot-hakios-lithium.py 先核对设备身份、解锁和分区几何；--boot仅临时启动固定镜像。用户空间始终目标18H17，hello仅验平台。

活动WSL任务为 hakios-audit / hakios-prepare / hakios-check / hakios-deps / hakios-support / hakios-kernel / hakios-package。旧 kernel/native-package/root-transfer 在访问WSL前拒绝，60个旧入口保持无副作用拒绝桩。iOS14私有ABI与A530 GLES候选须在P2/P3按现有差异台账验证；不恢复FactoryData旧路线。

''' + text[b:]
    plan.write_text(text, encoding='utf-8')
    operations = hakios.ROOT / 'OPERATIONS.md'
    text = operations.read_text(encoding='utf-8')
    a = text.index('本轮按新计划完成工程重构')
    b = text.index('\n\n此前“分析后停止”', a)
    text = text[:a] + '新路线已完成工程重构、完整HakiOS依赖/内核、Mu板层/loader/hello及首包静态核验，记录见 reports/hakios-platform-test-ready.md。最新USB查询未发现手机，未发送临时启动、刷写、清除或格式化；未运行QEMU或旧7195内核。P0输入与有限方案已闭合，HakiOS真机运行、iOS14及A530仍未验证。' + text[b:]
    operations.write_text(text, encoding='utf-8')
    scripts = hakios.ROOT / 'scripts/README.md'
    text = scripts.read_text(encoding='utf-8').replace('当前拒绝', '有效P0证据下执行').replace('8项路线隔离', '9项路线隔离')
    text = text.replace('没有新增启动或刷写入口，后续需满足PLAN与OPERATIONS中的实机实验条件后实现。',
        '新增 boot-hakios-lithium.py：默认只读预检，--boot核对授权MIX身份/解锁/分区后只临时启动经验证的HakiOS镜像。')
    text += '\n依赖、支持组件和打包任务分别为 hakios-deps / hakios-support / hakios-package；具体输入和实机步骤见 [首包记录](../reports/hakios-platform-test-ready.md)。\n'
    scripts.write_text(text, encoding='utf-8')
    audit = hakios.ROOT / 'reports/hakios-p0-audit.md'
    text = audit.read_text(encoding='utf-8')
    text = text.replace('2026-11-14。项目重构和配置集成已完成，**P0 尚未通过**。',
        '此文为2026-11-14首轮审计快照；当前P0输入/有限方案/加载实现已闭合，详见 [首包记录](hakios-platform-test-ready.md) 和 hakios-p0-closure.json。以下保留首轮发现，当时**P0 尚未通过**。')
    audit.write_text(text, encoding='utf-8')
    print('Updated plan, operations, project state and first-test instructions; hardware acceptance remains false')

if __name__ == '__main__':
    main()
