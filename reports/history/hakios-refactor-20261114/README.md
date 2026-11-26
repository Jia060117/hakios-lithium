# iOS 14 on Xiaomi Mi MIX / lithium

目标：在小米 MIX 一代（Snapdragon 821 / MSM8996pro、Adreno 530）上研究启动 iOS 14。
**最高项目级路线约束：基于完整 HakiOS 移植增加 MIX 一代设备支持，HakiOS 为唯一活动内核基线；原先7195内核仅作只读对照，不再使用。** 用户已于2026-11-14明确确认，当前及后续会话均遵循[项目指令](AGENTS.md)和[现行计划](PLAN.md)。本轮已完成约束落盘，HakiOS新基线尚未构建或真机验证；下一项工作是P0完整依赖、iOS14兼容和A530图形路线审计。QEMU停用。

userdata已被iOS根卷替换。实验卡住时先保留屏幕照片，再用电源＋音量下进入Fastboot；恢复Android需要原厂recovery重建userdata。写入结果见[根卷安装记录](reports/ios14-root-install.json)，不能再按此前轮次假定长按电源即可恢复Android桌面。

[实施计划](PLAN.md)已切换为HakiOS唯一基线，旧活动计划原文已[归档](reports/history/PLAN-before-hakios-20261114.md)。UniXNU、ios-sailfish仅作组件和资料来源，不自动替换基线。此前7195真机记录只作历史对照，不能据此继续其构建、启动或FactoryData修复。

本地IPSW已核验为iOS14.8 /18H17，用户空间仍固定iPhone10,1 /D20AP。[本地输入记录](reports/ios14-local-inputs.json)中的原始Apple内核仅用于系统版本与依赖对照；活动内核使用完整HakiOS移植，跨版本兼容必须实际验证。

用户已授权必要的root、清除数据和临时启动，且不要求备份，见[操作授权](OPERATIONS.md)。

## 工作目录与历史工具

下表中的旧7195、QEMU和原厂恢复工具保留为历史资料，不是新路线的执行入口；执行约束以AGENTS.md为准。

| 路径 | 内容 |
| --- | --- |
| `AGENTS.md` | 用户确认的最高项目级路线约束；HakiOS唯一活动基线 |
| `project.json` | 固定目标、HakiOS活动状态与历史证据范围 |
| `PLAN.md` | HakiOS完整基线、MIX适配、iOS14兼容和真机验收计划 |
| `upstreams.lock.json` | 两个上游及 UniXNU 子模块的精确提交 |
| `upstream/hakios-xnu-kernel/` | HakiOS 原始仓库，独立 Git checkout |
| `upstream/UniXNU/` | UniXNU 原始仓库，含递归子模块 |
| `scripts/probe-device.py` | 无 root 的只读 ADB 探测 |
| `scripts/lock-upstreams.py` | 记录现有上游版本，不拉取更新 |
| `scripts/extract-stock.py` | 离线拆解原厂 boot/recovery、选择设备树并进行 dtc 往返验证 |
| `scripts/capture-live-dt.py` | 无 root 采集运行时 sysfs 设备树，分析 RAM、保留区域和原厂差异 |
| `scripts/inspect-ios14.py` | 限量 HTTP Range 读取 Apple manifest 和选定 kernelcache |
| `scripts/decode-ios14.py` | 离线校验 complzss、ARM64 Mach-O、内核版本及预链接信息 |
| `scripts/audit-platform-inputs.py` | 离线审计参考内存表、MIX 原厂 ELF 和研究源码 pins |
| `scripts/check-fastboot.py` | 限时进入 Fastboot 查询并返回 Android；不刷写或清除；支持离线重解析日志 |
| `scripts/probe-blocks.py` | 不需 root，读取实际磁盘/分区名称、范围和逻辑块大小 |
| `diagnostics/entry-probe.S` | 不使用栈的 ARMv8.0 入口观察代码，记录后接回原厂内核 |
| `scripts/analyze-aboot.py` | 原厂 aboot 派生分析 ELF 和跳转路径反汇编；不修改原文件 |
| `scripts/build-entry-probe.py` | 构建诊断 boot 镜像，检查原厂内容、DTB 与内存区间 |
| `scripts/validate-entry-probe.py` | WSL/QEMU 的 10 个执行验证用例 |
| `scripts/boot-entry-probe.py` | 实际 Fastboot 临时启动、二进制记录采集及最终原厂重启对照 |
| `reports/entry-bringup.md`、`reports/entry-state.json` | 三轮真机入口诊断结果及复现方法 |
| `reports/display-bringup.md`、`reports/display-state.json` | 原厂启动显示状态、像素布局及白名单采样 |
| `firmware/` | lithium 平台层、GOP/BDS、启动 shim 和原厂内核恢复 stub |
| `reports/uefi-bringup.md`、`reports/uefi-build.json` | 最小 UEFI 构建、内存图、实验过程和当前限制 |
| `reports/uefi-qemu.json`、`reports/uefi-device.json` | 模拟执行和真机临时启动的分开记录 |
| `diagnostics/handoff-probe.S`、`diagnostics/handoff-probe.c` | 不进入 UEFI 的扩大 ramdisk 装载校验与原厂 initrd 交接诊断 |
| `scripts/build-handoff-probe.py`、`scripts/validate-handoff-probe.py`、`scripts/boot-handoff-probe.py` | 独立诊断的离线构建、14 项 QEMU 检查、本地输入校验及临时启动 |
| `reports/handoff-bringup.md`、`reports/handoff-probe-qemu.json` | 独立交接诊断的实验边界与模拟结果，真机尚未测试 |
| `boot/darwinboot-ios14/` | 独立的 7195 boot_args 头、命令行容量补丁和原始头编译校验 |
| `scripts/build-ios14-darwinboot.py`、`scripts/validate-ios14-darwinboot.py` | 构建 EFI 驱动、交叉编译 ABI、主机命令行边界检查及真实 14.8 内核解析 |
| `reports/ios14-loader.md`、`reports/ios14-loader-build.json`、`reports/ios14-loader-validation.json` | 7195 引导参数适配证据及尚未实现的整机引导部分 |
| `reports/initialization-2026-11-12.md` | 初始化结果和已知缺口 |
| `reports/stock-firmware.md` | 原厂固件处理结果和后续取材缺口 |
| `reports/stock-firmware.json` | 固件文件哈希、镜像元数据、20 份 DTB 及匹配结果 |
| `reports/live-memory.md`、`reports/live-memory.json` | 运行时 RAM、保留内存和设备树差异 |
| `reports/ios14-inputs.json` | 已验证的 14.8 manifest、内核哈希、精确版本及 217 个组件 |
| `reports/platform-inputs.json` | 参考 UEFI 地址冲突与 MIX XBL/aboot 程序头 |
| `reports/fastboot-status.json` | 真机 BL/critical 解锁、可用变量和返回 Android 的结果 |
| `reports/partition-map.json`、`reports/block-geometry.json` | 68个真实分区和6个磁盘的内核几何；不是原始GPT备份 |
| `reports/device-readiness.md` | 本次设备操作、授权变更及分区核验结果 |
| `reports/research-upstreams.lock.json` | 5 个研究仓库和已解析的 Mu 参考提交 |
| `reports/plan-review.md` | 计划范围、证据与未实现状态的复核 |
| `reports/local/` | 详细本机探测日志，不纳入版本控制 |
| `artifacts/local/` | 从本机提取的二进制设备树属性，不纳入版本控制 |
| `artifacts/stock/V9.5.2.0.NAHCNFA/` | 原厂 boot/recovery、内核、DTB/DTS 和 ramdisk 文本，不纳入版本控制 |
| `artifacts/live/` | 运行时可读设备树原始归档，含本机启动参数，不纳入版本控制 |

两个上游均为浅克隆；当前工作树完整，历史提交未全部下载。UniXNU 的 20 个声明子模块已初始化。
上游工作树没有改动；下载它们不等于把其现有系统版本设为本项目目标。

## 重新探测手机

使用 Windows PowerShell，确保 ADB 在 PATH 中，连接一台已授权的 `lithium`：

```powershell
Set-Location F:\IOS-MIX1
python scripts/probe-device.py
```

连接多台设备时，用 `--serial` 显式选择设备。脚本仅接受 `lithium`，不会调用 `su`、重启、安装软件、刷写或擦除分区。
命令失败、权限不足和空属性会原样记录；“没有找到 su”不等于完整证明设备从未 root。
最新日志位置记录在 `reports/local/latest.json` 中。

## 当前验证边界

- ADB 已授权并可读取机型、系统、SoC、GPU、内存、屏幕、电池、输入设备和分区容量。
- 已进入 Fastboot 独立确认 BL 与 critical 均解锁，随后返回 Android；查询 product 为 `MSM8996_A8`，重启前后 ADB 身份均为 lithium。
- 未发现 PATH 内的 `su`，当前 ADB 为普通 shell；初始化不需要 root。
- 已取得真机设备树根属性，并从匹配原厂 boot 提取完整静态 DTB。索引 3 的设备树与真机 7 个根属性完全匹配，dtc 往返转换通过。
- `/sys/firmware/fdt` 存在但受读取权限限制；分区名称目录也不可读。
- 原厂 boot/recovery 已从固件包取得；用户不要求手机备份，后续不以备份为前置条件。
- 已读取运行时 RAM：`[0x40000000, 0x100000000)` 和 `[0x100000000, 0x1bd950000)`，并保存 1898 个节点、14615 项属性的 sysfs 快照。
- 已整理 7 个固定 reserved-memory 节点和 3 个动态池。显示探针已取得入口 FDT 的 memreserve 表并读到空表终止项；动态池的实际分配位置仍未知。这些 RAM 范围不是空闲内存清单。
- 原厂串联入口、显示及日志采集探针均已取得真机记录。第三轮 UEFI 已取得屏幕证据，随后重新连回原厂 Android；独立装载诊断取得同次日志。UEFI 最新实验见 `reports/uefi-device.json`，历史第三轮见 `reports/uefi-device-attempt-03.json`；未 root、刷写、清除或重新分区。

## 后续顺序

依照 `AGENTS.md` 和 `PLAN.md`，先完成HakiOS完整基线的P0审计，再增加MIX平台支持。旧7195的构建/打包/启动入口停止使用，既有硬件取材和观测用于新适配的输入与对照。
参考 UEFI 的 FD、日志和 FV 区域已发现与 MIX 保留内存冲突，不能直接启动参考镜像。
最终主要验收是 iOS 14 SpringBoard、真实触控和系统应用；内核控制台只是中间结果。需要 root 或清除时可按已有用户授权执行。

## 重新采集运行时设备树

连接唯一一台已授权的 lithium，运行：

```powershell
python scripts/probe-device.py
python scripts/capture-live-dt.py
```

归档使用 `adb exec-out` 保留原始二进制。实测这版 MIUI 的 shell 通道即使加 `-T` 也会改写部分换行字节，不能用于二进制属性或归档传输。
脚本检查 tar 块对齐、头校验和、结尾、节点/属性完整性，并与独立采集的 memory/reg 对照。
原始 tar 中可能有本机标识；共享报告只包含必要地址、属性长度和哈希。
可用 `--archive` 离线分析已保存的归档；归档路径见 `reports/live-memory.json`。

## 重新提取原厂材料

原始目录保留不变，在工作目录运行：

```powershell
python scripts/extract-stock.py 'F:\IOS-MIX1\lithium_images_V9.5.2.0.NAHCNFA_20180405.0000.00_7.0_cn'
```

脚本依赖已保存的真机探测结果及 WSL Ubuntu 的 `dtc`，无需手机在线，不调用 ADB。
它验证镜像内部 SHA-1 ID、gzip CRC、build fingerprint、内核版本和设备树标识，并生成文件 SHA-256 清单。
这些一致性检查不等于厂商签名认证。原厂设备树的历史格式警告保存在 dtc 日志中，没有通过修改原始属性消除警告。

## 构建主机

已发现 Windows Platform-Tools、Git、Python，以及 WSL2 Ubuntu。
已在 Ubuntu 中运行只读检查，确认存在 `git`、`python3`、`clang`、`make`、`dtc`。
这只是工具存在性检查；没有验证 Apple SDK、交叉编译器、XNU 依赖或完整构建。

上游参考：

- HakiOS：`upstream/hakios-xnu-kernel/docs/porting/index.md`
- HakiOS 构建：`upstream/hakios-xnu-kernel/docs/getting-started/building.md`
- UniXNU：`upstream/UniXNU/docs/porting/index.md`
- UniXNU 构建：`upstream/UniXNU/docs/building.md`

保留各上游的原有许可证。固件包、压缩包和提取镜像仅保存在本地，由 `.gitignore` 排除；本项目不提供 Apple 系统镜像。
