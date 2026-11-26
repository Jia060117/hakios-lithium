# HakiOS 完整基线 P0 审计：首轮来源与缺口

此文为2026-11-14首轮审计快照；当前P0输入/有限方案/加载实现已闭合，详见 [首包记录](hakios-platform-test-ready.md) 和 hakios-p0-closure.json。以下保留首轮发现，当时**P0 尚未通过**。本报告基于固定源码和既有 MIX 观测，没有构建内核、运行 QEMU 或操作手机。跨版本兼容、A530 与首个包的加载契约仍需实际核对。

活动来源固定 `upstream/hakios-xnu-kernel` / `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU `8796.121.2`。完整隔离 checkout 位于 Ubuntu `/home/jia/ios-mix1-build/hakios-lithium`，分支 `codex/hakios-lithium`；原始 checkout 没有改动。不是从旧 7195 构建树复制的内核。

## 来源与真实实现

| 层 | 固定来源与实现 | 当前结论 |
| --- | --- | --- |
| 通用内核 | `xnu/apply.sh` 固定 Apple XNU `c54f35ca767986246321eb901baf8f5ff7923f6a`，叠加完整 `xnu/hakios.patch`、`xnu/files/`、`drivers/map` | 保留完整移植；不能只抽取几个驱动重建裸 XNU |
| pthread / os_log | `xnu-deps/setup.sh` 获取 libpthread `a6c70ca0…`、libdispatch `a641558f…`，导出本内核头并构建静态库 | 上游已有正常集成；不重写通用服务 |
| HFS / APFS | HFS `627.100.6` / `31d5ec23…` 加上游补丁；APFS 实现在 `xnu/files/bsd/miscfs/apfs/` | 有实际源码，不能视为 Apple 私有 APFS kext 或已验证 iOS14 根布局 |
| DriverKit | `xnu-deps/host/bin/iig` 使用 MacOSX13.3 SDK 预生成头；`gen-iig-stubs.py` 明确写明方法返回 unsupported、用户态 DriverKit 驱动不工作 | 存在占位实现；须核对 iOS14 所需组件是否依赖它，不能把链接成功算完整支持 |
| 策略与签名 | `kern_minimac.c`、`miniamfi.c`、`miniamfi_entitlements.cpp` 为上游实现 | 复用上游，列出与 iOS14 调用的实际差异；不能宣称等同完整 Apple 私有服务 |
| 显示 / IOSurface | `MiniIOSurface.cpp`、`MiniIOMobileFramebuffer.cpp` 与上游用户空间集成 | 有实现；用户客户端选择器、结构大小、合成路径及 MIX scanout 尚未核对 |
| GPU | `drivers/gpu/hakios.adreno.c` 开头明确所有地址属于 redwood / Adreno 642L，使用 GMU、RPMh、a660 固件路径 | 不能直接用于 A530；完整 A5xx 路径仍未建立 |

`config/hakios-lithium.json` 固定 13 项声明依赖的完整提交或归档 SHA256，逐一与原始上游脚本核对。`reports/hakios-source-audit.json` 记录 879 个原始工作树文件的 SHA256。这里的哈希指当前 checkout 字节；基线提交负责 Git 来源身份。依赖清单区分必需构建依赖、其他设备资料、UEFI 与图形候选，未把 redwood 设备仓库作为 lithium 必需依赖。

尚未完成依赖获取与闭包验证：本机 theos/cctools、iPhoneOS16.5 SDK 的版本和哈希；MacOSX13.3 SDK Kernel.framework 归档哈希；选定加载路径所需 UEFI/U-Boot 的完整依赖；图形候选的 Meson/Rust/LLVM 等工具依赖；本机外设固件选择。上游 setup 的 macOS SDK URL 没有对应归档哈希。构建 SDK 16.5 不改变最终 iOS14 用户空间目标。

## iOS14 与 MSM8996 边界

| 边界 | 已有证据 | 下一项可审查工作 |
| --- | --- | --- |
| CPU 指令 | `reports/entry-state.json`：Kryo 无 LSE，MMFR1=0；上游 patch 为 BCM2837 添加 `+pan` | 核对 PAN/LSE 与用户访存实现，形成限定于 ARMv8.0 的平台补丁并检查实际输出指令；未证明现默认配置可执行 |
| 中断 / timer | 上游 `gic_v3_irq.c` 从 `/arm-io/gic` 取寄存器；当前 redistributor frame 常量为 0x20000 | 按原厂 DTB 核对 MSM8996 寄存器布局、PPI、redistributor stride、19.2MHz timer、PSCI/CPU 交接；不继承旧内核运行通过标志 |
| dyld / Mach / commpage | 系统身份仍是 D20AP / 18H17；上游针对 16.5 | 列出真实 dyld、launchd 的 syscall/trap/commpage 与私有接口需求；不能直接链接 7195 二进制 kext 到 8796 |
| 根卷 / Data | 现手机 userdata 为已核验的 iOS14 HFSX；上游有 APFS/HFS 集成 | 先查根设备、挂载、firmlink/bind、Data/FactoryData 路径和驱动依赖，决定最终布局；不继续旧 FactoryData panic 修补 |
| IOKit / HID | 上游输入控制器面向 SPI GT9916/FT3680；MIX 为 I2C FT5x46 | 复用上游 HID 设计，新增本机 I2C、GPIO/IRQ、触摸封包适配；核对 iOS14 用户客户端接口 |

这些是具体待核对边界，没有证据表明必须改变路线，也没有证据表明全部可以有界适配。需要大规模重建通用服务时按 PLAN.md 提交具体阻塞。

## A530 图形路径

保留上游 IOSurface、framebuffer、GPU 用户客户端设计；硬件端需原厂 MSM8996/MDP5/SMMU/A530 与同代来源的实际适配。Mesa 25.0.7 有固定归档哈希，上游 `tools/mk-turnip.sh` 接入 Vulkan，`tools/mk-m2v.sh` 固定 AdrenoMetal 与 metal2vulkan 并明确针对 iOS16.5。**尚未证明该 Vulkan/Metal 组合覆盖 A530，或能保持 iOS14 ABI。** 不能把 A642L 的 GMU/RPMh/a660 固件配置换名字当成 A530 支持。

下一次需核对可用 A5xx 内核命令提交、内存映射、固件、同步/fence、渲染驱动及 iOS14 合成链。若只能获得基本 framebuffer 或软件绘制，就明确记录该范围；若缺少整条 GPU 路径，先报告，不扩展成自造整套 GPU/Metal 栈。

## 首个 MIX 包的设计边界

使用完整 HakiOS 内核和上游 `userland/src/hello.c` 做 P1 平台验证；加载器以 `xnuloader/loader.c` 和 XNUP payload 契约为基准。它要求 UEFI 内存图、boot_args/ADT、载荷位置、块服务和 CPU 交接，不能套用 `boot/darwinboot-ios14/` 的 7195 ABI。上游 PAYLOAD_BASE=0xf0000000 与 MAX_MEM_SIZE=6GiB 均须结合 MIX 保留区重新审查，尚未分配为本机安全地址。

选择现有 lithium UEFI 硬件成果还是上游 U-Boot 的本机适配，需要核对完整交接及内存边界后确定；现阶段不打包两个并行入口、不使用旧 shim 兜底。初包只验证平台，不能替代 iOS14 或图形验收。

## 本轮验收与继续条件

已通过：HakiOS 上游 `hakios-device.py check lithium`、`make lithium_defconfig DEVICE=lithium JOBS=2`、`hakios-config.py check`、`drivers-check.py`；新配置包含完整 common_defconfig、MSM8996 和 lithium 片段。所有尚未适配的设备驱动及对应 boot 参数关闭。检查只证明配置集成。

构建任务要求 P0 闭包证据与同一份来源/配置哈希，以及已实现的本机加载契约；当前条件不成立，`hakios-kernel` 会拒绝执行。下一步依顺序完成依赖/SDK 固定、ARMv8.0 与加载契约核对、iOS14 和 A530 方案边界，再进入 P1。P0 状态保持 in_progress，真机能力全部未验证。
