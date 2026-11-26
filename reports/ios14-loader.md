# iOS 14 引导参数适配与独立 EFI 构建

日期：2026-11-12。目标为 iOS 14.8 / 18H17，物理设备为 Xiaomi MIX 一代 lithium。

**针对 XNU 7195 引导参数布局的 DarwinBoot EFI 驱动已构建，结构编译检查、7 项主机命令行检查及真实 14.8 内核的 Mach-O 解析检查通过。后续集成已在普通/严格对齐 QEMU 实际执行 EFI；第八轮真机显示 FAT/EFI 条件 PASS，目视证明 EFI 入口及 loader 协议检查。尚未调用 Boot 方法、启动 XNU 或 iOS。** 集成结果见 `uefi-payload.md`，本报告保留独立构建的证据范围。UEFI 自动返回 Android 仍在定位，最新状态见 `uefi-device.json`。

## 输入和实现

源码与完整复现方法见 `boot/darwinboot-ios14/README.md`，机器记录见 `ios14-loader-build.json` 和 `ios14-loader-validation.json`。

固定 DarwinBoot 子模块为 `5243c68ec332a227fe3680cd5da6d5a0e127035c`，固定 Mu 为 `6b3d90b79e191e08171bfedd3980234a7e49d340`。引导参数权威参考是已固定 XNU 7195.141.2 的未改动 `pexpert/pexpert/arm64/boot.h`。原始 iOS 14.8 内核为 7195.140.44；未将邻近源码描述为精确匹配。

上游当前结构的命令行容量是 1024 字节，而 7195 参考为 608 字节。新版本的结构大小为 `0x2e0`，`bootFlags` 在 `0x2d0`，`memSizeActual` 在 `0x2d8`。独立 overlay 同时替换 ABI 头文件和驱动的局部命令行容量，保持既有清零、检查与复制逻辑使用同一长度。

## 产物与检查

| 产物 | 结果 |
| --- | --- |
| `out/darwinboot-ios14/DarwinBoot-ios14.efi` | 36,352 字节，AArch64 PE32+ EFI boot-service driver |
| EFI SHA-256 | `9af607ab2aebeb3bd60f4fbcf87c7ce0b49bdf7eba5c99f19db941647e358386` |
| `out/darwinboot-ios14/abi-smoke.macho` | ARM64 subtype 0，MH_EXECUTE，无未解析符号；仅 ABI 探针 |
| Mach-O 探针 SHA-256 | `f76bd66d6c549a37b106101a24a805712a63578b6fd31b1f685a9c73d9fd9aff` |
| 编译工具 | clang 18.1.3；Mach-O 链接使用 ld64.lld 18.1.3；EFI 使用现有 Mu CLANGPDB |
| ABI 编译 | ELF 与 Mach-O 两种目标直接比较原始 7195 结构全部语义字段与六个视频字段 |
| 命令行主机检查 | 7 项通过；607 字符接受，608 字符拒绝，相邻字段及保护字节不变 |
| 真实内核解析 | 生产解析代码接受已校验的 14.8 内核 `f2fe5f5e...fc9d4fba`；不代表可执行该内核 |

未修改上游源码，工作副本在 WSL ext4 中。构建没有使用 Apple SDK；ABI 探针只依赖原始结构定义、编译器基础头和 Mu 数据类型，不能据此判定完整 7195 SDK、MIG、IIG 或 IOKit 已可用。

## 下一步约束

电脑侧可以继续 7195 内核对象/依赖、ADT 与平台差异工作，但 G2 真机整体验收仍未通过。该驱动已由 lithium 的 FAT payload 路径实际加载，BDS 的 ExitBootServices 和原厂内核复制读回也在 QEMU 验证，并有第八轮真机检查点证据。XNU 所需的内核分配窗口、ADT 与 Boot 方法交接仍未建立，不能把 BDS 的原厂恢复路径视为 DarwinBoot 内核交接测试。

当前 24 位屏幕与上游 32 位视频路径尚不匹配，现代 DarwinBoot 的内存/ramdisk 扩展也需要 7195 内核逐项支持。这些不因结构编译通过而消失。

扩大 ramdisk 独立交接诊断已经通过真机；目前继续隔离恢复复制、UEFI 初始化及 Linux 入口问题。当前不把 14 项 QEMU 诊断用例、7 项主机命令行用例、EFI 构建或原始内核解析当成 iOS 上机。
