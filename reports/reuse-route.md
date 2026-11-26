# iOS 14 / MIX 1：以现成移植为基础的调整路线

**历史路线记录。2026-11-14用户已明确改为完整HakiOS唯一活动基线，原7195内核仅作只读对照，不再使用。下文的Apple7195主线及后续命令已停用；现行约束见根目录AGENTS.md，现行计划见PLAN.md。**

日期：2026-11-12。用户要求减少重复实现，优先参考成熟开源项目和 Apple 官方内核。目标仍为 **iOS 14.8 / 18H17，原生 ARM64，最终 SpringBoard、真实触摸与系统应用**。

## 调整决定

此前已复用 Mu-Silicium、DarwinBoot 和 UniXNU 的部分代码，并非全部从零实现；但后半段投入了过多工作定位“退出 UEFI 后恢复原厂 Linux”，这不是最终 iOS 启动链的必要验收目标。第十轮已目视执行到 Linux 开启 MMU 后的高虚拟地址钩子，结果归档后将原厂恢复诊断作为已有辅助工具保留。

主线改为：**Apple XNU 7195 基线 + 同芯片 sailfish 移植的可审查模块 + UniXNU 构建/ADT/加载组件 + lithium 板级配置。** 2026-11-13 已完成 lithium 内核构建、指令检查和 QEMU 的四个早期启动断点，正在补齐 BSD 前的 SHA1 等平台服务，然后准备真机 XNU 临时启动。详见 `ios14-xnu-bringup.md`；下文初期候选检查是历史记录。

## 本次重新核查的来源

| 来源 | 已核实的材料 | 采用方式与边界 |
| --- | --- | --- |
| [Apple xnu-7195.141.2](https://github.com/apple-oss-distributions/xnu/tree/xnu-7195.141.2) | 已固定官方提交 `776661b72c2db9861865df68d309f6f35faccff4`；包含 ARM64 启动、VM、IOKit、GICv3 实现及 boot_args 定义 | 作为 iOS14 同代源码基线，直接在既有子系统上适配；不是原始 14.8 内核 7195.140.44 的精确源码 |
| [ios-sailfish](https://github.com/joranv1/ios-sailfish) | 新下载并固定 `6f27e41ff249f2680540ebe8ee5ab982c5e641c9`，该提交日期 2026-10-22；实际有 183 份 XNU 补丁、shim、ADT/payload 工具及 GLES 适配代码 | Pixel 1 与 MIX 都使用 Snapdragon 821 / Adreno530，优先对照 CPU、GIC、UFS、RPM、GPU、24bpp 显示；其 XNU8796/iOS16 用户空间不替代本项目目标 |
| [UniXNU](https://github.com/UniXNU/UniXNU) | 已固定 `c34f018aafd6befd5dc93c05a8dccd5bdce6724b`，构建编排、ADT、DarwinBoot、组件化驱动可用作来源；基础 feed 为 10063.141.1 | 复用编排和独立组件，单独适配 7195；当前 checkout 没有已完成的 iOS14 profile |
| [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel) | 已固定 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`；Makefile 目标为 8796.121.2 | 参考构建、日志、平台层和显示接口，按 7195 重新核对接口 |
| [UniXNU-msm8996-common](https://github.com/midopix/UniXNU-msm8996-common) 与 [g0245d](https://github.com/midopix/UniXNU-g0245d) | 已有固定 checkout；SoC 描述、设备仓库格式、GIC/timer 经验可取用，g0245d 有较多早期启动补丁 | 参考 SoC 资源和工程组织；不把 README 中计划的 UFS/GPU/显示驱动当成已完成实现，也不继承无限追加启动标记的路线 |

`ios-sailfish` README 报告 iOS16 锁屏、SpringBoard、应用、Adreno530 加速及 Wi-Fi。**本项目没有独立复现这些运行结果。** 本次确认的是实际补丁与实现代码存在，并可重建供阅读；不是确认完整参考项目可重建、可启动，更不是 MIX 已有 iOS 驱动。搜索中出现的 `QidiWangSY/ios-sailfish` 地址返回 404，未作为有效输入；采用的是实际可访问的 `joranv1/ios-sailfish`。

## 已执行的代码工作

1. `python scripts/audit-reuse-candidates.py` 核验 sailfish 与 Apple checkout 的固定提交和干净状态；在隔离目录应用 125 份涉及新驱动文件的补丁，重建 **26 个独立参考文件**。清单、字节数、SHA-256 和补丁顺序见 `reuse-audit.json`，代码位于 `out/reuse/sailfish-drivers-6f27e41ff249/`。外部依赖 staging 脚本没有执行。
2. 对五份关键补丁分别进行原始 7195 上的只读 `git apply --check`。平台 expert 补丁可独立应用；机器配置、timer/GIC、像素格式及 ARMv8.0 补丁需要重新处理上下文或前置依赖。这个检查不构成整套补丁兼容性或编译验证。
3. 已将 sailfish 的 GIC redistributor stride 属性接口整理为 **7195 候选补丁**：`ports/lithium/patches/xnu-7195/0001-gicv3-redistributor-stride.patch`。默认保持原有步长，可通过 ADT 指定 0x40000；MIX 原厂 DTS 确有该步长。候选对固定 Apple7195 通过应用检查；尚未编译、上机或加入可启动镜像。
4. 用户确认第十轮八黄、一绿、无红；电脑 140 秒未观察自动返回。用户明确手动重启后，电脑核验 lithium、MIUI V9.5.2.0.NAHCNFA、boot_completed=1。原厂 Linux 诊断结果见 `uefi-device-attempt-10.json`。该结果不表示 XNU 启动。

## 复用优先级与真正差异

| 子系统 | 首选参考 | MIX 需要处理的差异 |
| --- | --- | --- |
| CPU / MMU / GIC / timer | Apple7195 现有实现，sailfish 0079/0080/0088 等平台适配 | ARMv8.0、无 LSE；16K 页作为候选；GICR stride 0x40000；地址、CPU 拓扑及频率以本机采样为准 |
| 启动与日志 | 现有 DarwinBoot7195、sailfish shim/loader/ADT/payload 与 pstore 实现 | Pixel shim 的 kernel 0x80080000、FDT 0x82600000、pstore 和 framebuffer 不能直接用于 MIX；保留 MIX 已核验的地址取材 |
| UFS | `bsd/dev/sweet_ufs.c` 及 Pixel/8996 后续补丁 | 控制器和 PHY 初始化、DMA、时钟、分区范围及根文件系统接口均须检查；使用既有驱动作基础，不重写通用 UFS 协议栈 |
| GPU / 合成 | `sweet_gpu.c`、SweetGPU、SweetSurface、SweetFramebuffer、sweetgl/Mesa freedreno | A530 同代实现有参考价值；7195 KPI、iOS14 IOSurface/IOMobileFramebuffer 及 CoreAnimation 私有接口尚未移植；不能复制 iOS16 dylib 后宣称兼容 |
| 显示 | 已验证的 MIX 1080×2040、stride3240、BGR24，以及 sailfish24bpp和 MDP 路径 | Pixel 是 1080×1920 命令模式面板；面板时序、扫描与刷新方式按 MIX 原厂材料适配 |
| 触摸 | 已有输入总线/HID 代码，MIX 原厂 FT5x46 设备树和 Linux 驱动 | 本机 `/proc/bus/input/devices` 实测为 **ft5x46**；Pixel 使用 Synaptics RMI4，不能照搬设备协议及供电参数 |
| iOS 用户空间 | 已固定的 14.8 / iPhone8,1 材料，参考两项目的 root/launchd/显示服务集成方法 | 7195 ABI、dyld、文件系统、IOSurface/HID、安全服务替代接口需按 iOS14 适配；不改用 iOS16 用户空间 |

同芯片减少的是硬件协议实现与排错成本，不消除板级线路和内核版本差异。Apple 官方 XNU 是内核来源，不能直接补齐 Adreno/触摸适配或 iOS 图形用户空间。

## 后续验收顺序

1. **可复现的 7195 内核构建**：沿既有 UniXNU 的 Linux 构建能力核对 7195 所需 clang/ld、MIG/IIG、SDK/头文件和支持库，落实最小 lithium 配置。输出内核 Mach-O、符号、构建日志和固定输入，编译失败时记录实际命令和首个阻塞接口。sailfish 当前脚本依赖 Mac/Xcode15.4/KDK13.5，并明确选择8796；不能只替换一个版本变量当作7195构建方案。
2. **真正进入 XNU**：复用已有加载器和参考平台代码，生成匹配7195的 boot_args/ADT，在已核验本机内存范围内临时启动。验收是 XNU 自身日志/异常与平台初始化，不是恢复 Linux 的色块。
3. **复用驱动逐项集成**：优先内存根/日志、UFS、framebuffer 与 HID，再适配 A530 合成路径；每项用实际 I/O 或画面/触摸事件验收。
4. **iOS14 用户空间**：依次到 launchd、backboardd、SpringBoard、真实触摸和系统应用。内核编译、屏幕点亮或模拟通过均不改名为最终成功。

本次调整没有启动第十一轮手机实验，没有 root、清除或分区刷写。原有授权继续有效。
