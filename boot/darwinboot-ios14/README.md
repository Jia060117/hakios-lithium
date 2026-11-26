# DarwinBoot 的 7195 引导参数版本

本目录适配 iOS 14 所属的 XNU 7195 ARM64 `boot_args`。已生成 EFI 驱动，并通过内存 FAT 卷集成到 lithium UEFI。普通和严格对齐 QEMU 场景实际执行该 EFI，第八轮真机用户反馈条件文字 `FAT FILE AND EFI PASS`；这证明 EFI 入口和 loader 协议检查的目视结果，同次二进制日志尚未取得。尚未调用 Boot 方法或启动 XNU。

实际基准为已固定的 DarwinBoot `5243c68ec332a227fe3680cd5da6d5a0e127035c`、Mu-Silicium `6b3d90b79e191e08171bfedd3980234a7e49d340`、XNU 7195.141.2 `776661b72c2db9861865df68d309f6f35faccff4`。使用的是当前子模块锁定版本，没有按组件目录的另一 revision 自动更新。

本项目的原始 14.8 内核是 7195.140.44。这里直接编译检查的是邻近开源 7195.141.2 的原始头文件；它不证明原始内核的全部私有 ABI，也不替代版本差异审计。

## 改动

`BootArgs.h` 保持 DarwinBoot 的字段名称，按 7195 定义命令行长度及尾部字段：

| 项目 | 当前上游 Darwin 23 结构 | 7195 参考结构 |
| --- | --- | --- |
| 命令行容量，含结尾 NUL | 1024 | 608 |
| `CommandLine` 偏移 | `0x6c` | `0x6c` |
| `BootFlags` 偏移 | `0x470` | `0x2d0` |
| `MemorySizeActual` 偏移 | `0x478` | `0x2d8` |
| 整体大小 | `0x480` | `0x2e0` |

`command-line.patch` 同时把驱动内的局部命令行数组改成依据 `DARWIN_BOOT_ARGS.CommandLine` 的实际大小分配。原有长度检查、清零和复制均使用这个数组的 `sizeof`，因此会一同变为 608 字节。只替换结构头而保留原来的 1024 字节复制会写出新结构的命令行字段，本实现将两处一起适配。

没有修改 Mach-O 解析器、视频代码或平台硬件实现。独立 WSL 工作区中移除可选的上游 `Test/Validation.inf` 构建项，完整保留并编译实际驱动。原始 checkout 未修改。

## 复现

```powershell
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/build-ios14-darwinboot.py
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-ios14-darwinboot.py
```

工作区为 `/home/jia/ios-mix1-build/darwinboot-ios14-workspace`，输出为 `out/darwinboot-ios14/DarwinBoot-ios14.efi`。两个命令均完全离线，不接触手机。

构建脚本按 Windows Git 的 CRLF 策略执行 WSL 只读状态检查，在工作副本中统一文本换行后应用补丁，不更改上游 Git 配置或文件。构建与源哈希记录在 `reports/ios14-loader-build.json`。

## 验证结果和范围

- 在 ARM64 ELF 和 ARM64 iOS Mach-O 编译目标上，用未修改的 7195 `pexpert/arm64/boot.h` 对照全部语义字段、六个视频字段、大小、对齐及版本常量。仅跳过不定义这些字段的 debug/prototype 依赖头；没有替换原始 `boot_args` 定义。
- 编译并链接一个无运行库依赖的 ARM64 Mach-O ABI 探针，确认 MH_EXECUTE、普通 ARM64 subtype 0 且无未解析符号。这不是 XNU 内核构建。
- 完整 EFI 驱动构建成功，检查 PE32+、AArch64 与 EFI boot-service driver 子系统。
- 从实际编译的驱动源码提取命令行验证和复制片段，在主机执行 7 项检查：空指针、空串、普通参数、607 字符上限、608 字符拒绝、非 ASCII、UTF-16 surrogate。检查整个输出，仅允许命令行字段变化，并确认相邻启动标志、内存字段及前后保护字节保持原样。
- 使用上游现有测试编译工作副本中的真实 Mach-O 解析代码，已接受本项目 SHA-256 固定的 iOS 14.8 原始内核，并通过其原有架构和越界输入用例。

以上独立检查结果在 `reports/ios14-loader-validation.json`，保留其原始验证范围。后续完整 UEFI 的 FAT 读取、EFI 执行、严格对齐和损坏文件拒绝记录在 `reports/uefi-payload.md` 与 `.json`；这部分没有完成 XNU 的布局或内核交接。

## 尚需实现

当前手机只有基于实测 BGR24 扫描缓冲的 PixelBltOnly GOP；上游 DarwinBoot 视频路径要求 BGRA32。XNU 7195 控制台的实际绘字路径也需要另外适配该 24 位布局。不能把现有三字节缓冲标成四字节后交给这些代码。

lithium 固件已有 1 MiB 只读 FAT12 payload 和 EFI 加载路径，足够 XNU 的内核窗口分配仍未集成。7195 内核也没有自动获得上游新系统 profile 的 memory-excluded、packed ramdisk 或其他自定义扩展支持；这些要逐项移植后才可使用。

2026-11-13：完整lithium XNU7195、平台ADT和直接加载镜像已另行构建；此EFI驱动保留为既有产物。当前主线为直接实机XNU入口，见../../PLAN.md。14.8私有组件及用户空间仍待完成。主目标保持为实际运行 iOS 14 的 SpringBoard、触摸和系统应用。
