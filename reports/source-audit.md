# 移植依据审计

日期：2026-11-12。对象：本机 lithium、用户提供的 V9.5.2.0.NAHCNFA 固件，以及锁文件所列源码提交。下列判断仅覆盖这些材料，不代表验证了其他设备的运行结果。

同日后续按用户要求扩大了复用审计：新增同为骁龙821的 `joranv1/ios-sailfish` 固定源码、还原26个参考驱动文件、形成首个7195 GIC候选补丁。当前主线调整及验证边界见 `reuse-route.md`、`reuse-audit.json`。下文保留初次审计范围，不能用旧清单推断新增来源已完成7195适配。

## 1. 证据等级

| 类别 | 本项目中的含义 |
| --- | --- |
| 实测 | ADB 读取、原始字节校验、离线解析和真实文件输入执行 |
| 源码事实 | 指定提交中的配置、函数、补丁和依赖，未在 MIX 上运行 |
| 作者报告 | 上游 README、注释或照片描述的进度，本项目未独立复现 |
| 待验证 | 计划中的实现或实验，不能填写成已支持 |

后续已执行三轮原厂串联入口诊断、显示采样和日志观察器；第三轮 UEFI 的 BDS/GOP/20 次计时已有用户照片目视证据，但恢复交接失败。手机随后重新连回原厂 Android，扩大 ramdisk 与 initrd 独立诊断已在真机通过。最新设备实验见 `entry-bringup.md`、`display-bringup.md`、`uefi-bringup.md` 和 `handoff-bringup.md`。详细本机标识与启动参数仅放在被忽略的 `reports/local/`、`artifacts/`。

## 2. 两个原项目可以提供什么

两个原项目的提交和 UniXNU 的 20 个声明子模块见根目录 `upstreams.lock.json`。研究仓库见 `reports/research-upstreams.lock.json`。

| 输入 | 在已检查提交中的情况 | 本项目用法 |
| --- | --- | --- |
| HakiOS | `Makefile:75` 指向 XNU 8796.121.2 | 对照平台层、引导、驱动和构建方法；不作为 iOS 14 二进制/ABI 基准 |
| UniXNU | `feeds.toml:10` 固定 XNU 10063.141.1；现有 iPadOS 17 profile 同样固定该版本 | 复用可审计的编排、ADT、加载、预链接方法；所有内核接口需检查 7195 适用性 |
| UniXNU ARM64 profile | `profiles/arch/arm64/profile.toml:3` 设置 4096 页 | 这是其构建选择，不是本机页粒度能力的实测 |
| UniXNU 解码 | `scripts/unixnu/ipsw.py:259–295` 走 LZFSE 解码流程 | 不能原样处理本次取得的 complzss；本项目已独立完成离线解码，但尚未集成 profile |
| Gree MSM8996 设备仓库 | `board.toml:147–152` 明示 UFS、BLSP、Adreno530 为探测框架，平台实现有 stub | 只复用确认过的 SoC 资源与诊断经验，不据此宣称硬件驱动完成 |
| Gree XNU 补丁 | `patches/xnu/series:244–273` 已到 v137 的 early boot/maxmem 路径调试 | README 的早期缺口清单不是完整现状；早期检查点不等于 launchd 或桌面成功 |
| 旧 edk2-lithium | `README.md:1–8` 写明废弃和 AArch32 切换失败 | 历史参考；不能把该失败等同于 ARM64 方案已被证明不可能 |
| Gree UEFI | README 描述菜单启动和原生显示待完善，包含作者运行报告及待验证项 | 可作现代固件参考，板级地址、触摸、显示、厂商模块均重新取材 |

上述路径分别相对所属 checkout。5 个研究 checkout 当前均干净。没有把 Gree 的板级补丁直接应用到 MIX，也没有更新已固定的上游提交。

## 3. 已验证的 iOS 14 输入

实际读取 Apple CDN 中的 `BuildManifest.plist`，确认 iOS **14.8 / 18H17 / iPhone8,1 / n71ap**。URL 定位最初来自 ipsw.me，版本与身份来自实际 Apple manifest；没有下载完整 IPSW，也未独立校验完整 IPSW 的第三方哈希。

| 项目 | 结果 |
| --- | --- |
| 选定 ZIP 成员 | `kernelcache.release.n71` |
| 外层容器 | IM4P，DER 边界已解析 |
| 压缩 | `complzss`；384 字节压缩头；压缩流 19,063,374 字节 |
| 解压大小 | 38,764,544 字节 |
| 解压校验 | 流消费长度、输出长度及 Adler32 `0x811d440e` 均通过 |
| Mach-O | little-endian ARM64，CPU type `0x100000c`，subtype `0x0`，MH_EXECUTE |
| 版本串 | `Darwin Kernel Version 20.6.0: Tue Aug 24 21:10:33 PDT 2021; root:xnu-7195.140.44~1/RELEASE_ARM64_S8000` |
| 解压后 SHA-256 | `f2fe5f5e0684989ae2c3d37dbf641c207fa67d09273bba45e10f8376fc9d4fba` |
| 预链接信息 | 217 项；是带 ID/IDREF 的 OSUnserialize XML，不能直接当普通 plist 读取 |
| 压缩流后附数据 | 49,152 字节已保留并单独哈希；含义未解释 |

可复现命令：`python scripts/decode-ios14.py`。解码格式参考 Apple `libkern/mkext.c:109–174`，预链接 XML 使用已固定 UniXNU 的 `native_prelink.py:83` 解析器。原始 kernelcache 也被该上游的 Mach-O 解析器成功识别。这仅验证输入格式，不验证重绑定或运行兼容性。

精确报告、组件版本及依赖见 `reports/ios14-inputs.json`。其中包括：

| 组件 | 实际版本 | 对计划的影响 |
| --- | --- | --- |
| APFS | 1677.142.1 | 需要 7195 KPI、IOStorage、crypto 与挂载策略审计 |
| IOSurface | 290.7 | 映射、锁页、同步与用户客户端 ABI 需重新绑定 |
| IOGPUFamily | 21.9 | 不能复用新系统的私有类布局和选择器表 |
| IOMobileGraphicsFamily | 343.0.0 | 显示服务与 framebuffer 并不是同一层 |
| IOHIDFamily | 2.0.0 | 触摸需对接该版事件接口 |
| AGXG5P / AGXFirmwareKextG5P | 173.26.4 | Apple GPU 实现不是 Adreno 530 驱动 |
| corecrypto | 11.1 | 脚本文件名 `build-corecrypto-14.py` 不能证明对应 iOS 14 的 ABI |

Apple 仓库远程 `*7195*` 标签清单已保存在本地研究材料并进入锁文件观察项；本次没有发现 `xnu-7195.140.44` 标签。已下载的 **7195.141.2** 是同一大版本参考源码，**不是与上述原始内核精确匹配的源码**。原始内核符号、结构、函数语义和 kext ABI 的差异分析是进入用户空间前的硬门槛。

## 4. MIX 内存与参考 UEFI 的冲突

数据源：`reports/live-memory.json` 与 Gree `MemoryMapLib.c`。审计脚本：`python scripts/audit-platform-inputs.py`；机器可读结果：`reports/platform-inputs.json`。

| 参考表中的用途 | 半开地址范围 | MIX 中的实际关系 |
| --- | --- | --- |
| FV Region | `[0x8a000000,0x8c000000)` | 落在 modem no-map 区，不能作为 FV 解压或分配空间 |
| ABOOT FV | `[0x8c000000,0x8c200000)` | 同样落在 modem 区；即使表项标成 Reserv，也不能据名称复制 aboot/FV 到这里 |
| UEFI FD | `[0x92000000,0x92300000)` | 落在 peripheral no-map 区，不能采用该 FD 搬移地址 |
| Advanced Logger | `[0x92300000,0x92400000)` | 也落在 peripheral no-map 区，不能写作日志缓冲 |

MIX peripheral 区实际终点是 **0x92900000**。显示保留区、dfps 和 SMEM 的相交需要按原固件语义保留映射，不与可写分配区混为一谈。脚本将这些相交单独标记为需要语义复核。

两段声明 RAM 共 6,401,884,160 字节；不是可用内存表。Android 期间的 ADSP/secure/qseecom 动态池没有取得实际地址，也不一定等同于 aboot 交接时的占用。后续显示探针已读取入口 FDT 的 memreserve 表至空表终止项；这不代表 reserved-memory 节点或其他 DDR 没有占用。

### 原厂 ELF 实际解析

| 文件 | 格式/入口 | 关键程序头范围 |
| --- | --- | --- |
| `emmc_appsboot.mbn` | ELF32 / ARM，入口 `0x92a00000` | PT_LOAD `[0x92a00000,0x92bd74f8)` |
| `xbl.elf` | ELF64 / AArch64，入口 `0x06214120` | 完整程序头见 JSON；包含 `[0x80200000,0x802f0000)` 和多个片上、DDR 范围 |

Gree 文档的 aboot 范围不能替代 MIX 数据。ELF 程序头只描述该镜像的一组加载范围，不能证明它们在后续阶段仍存活，也不覆盖堆、运行时重定位和所有安全固件区域。尤其不能据 XBL 段和 Linux 标准加载范围的交叠，直接推断可用或不可用；必须给出占用生命周期。

后续已根据真机入口记录，在原厂 Image 保留窗口内选定 lithium 专用 FD、堆、页表、栈及恢复 stub 的区间，并通过静态范围检查和 QEMU 执行。完整表见 `uefi-bringup.md`；它不证明未知 DDR 空闲，也没有验证真机 UEFI 交接。aboot 的下载/解压 scratch 仍由原引导器管理；扩大 ramdisk 的独立真机装载和 initrd 地址调整已取得同次日志，见 `handoff-bringup.md`。

## 5. 已知外部依赖缺口

Gree overlay 参考的 Mu-Silicium `v4.1` 固定为 `6b3d90b79e191e08171bfedd3980234a7e49d340`；后续完整递归 checkout 已取得并构建，pins 见研究锁文件。lithium 平台层在独立工作区构建，没有使用浮动 main。

Gree UEFI README 要求其本机提取的 Qualcomm DXE；本项目没有这些输入，也不能认为 MIX 的 XBL 一定提供相同 GUID/协议/版本的模块。先在 MIX 自有固件中建立 FV/FFS/GUID 清单；缺模块时采用最小诊断固件或适配源码实现，不能拿其他手机的 SPMI/PMIC 二进制补齐。

本机 WSL clang18、make、dtc、cmake、ninja、QEMU 等工具已用于实际构建。最小 lithium UEFI 和独立 7195 引导参数版本的 DarwinBoot EFI 已构建；ARM64 Mach-O 最小编译/链接、原始 7195 参考头 ABI 编译校验及真实 14.8 内核解析通过，见 `ios14-loader.md`。完整 7195 内核工具链、内核对象、MIG/IIG、IOKit、依赖 archive 和 SDK 仍未验证。UniXNU 现有新内核工具链不能据此保证 7195 可编译。

图形、APFS、Data seed、dyld、启动服务、SEP 替代接口及 iOS 14 ABI 审计均未完成。固件里的 Wi-Fi/蓝牙材料与设备树可用于取材，Linux/Android 驱动不能直接加载进 XNU。完整后续实验、失败条件和资源预算见根目录 `PLAN.md`。
