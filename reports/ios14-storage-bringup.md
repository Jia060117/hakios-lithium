# iOS14 根盘与 MIX UFS 接入

2026-11-14：完整iOS14.8根卷已安装。第九轮手机读回卷头CRC32=0x6143e5cf并选中ufsroot0，HFS生成UUID时MD5Init空指针panic；第十轮已发送真实MD5修复，根挂载和用户空间仍待实机证据。

## 已核验输入

- 用户 IPSW：iOS14.8 / 18H17，D20AP，保持原始 Apple 文件在本机。
- OS DMG：`E:\IOS-MIX1-artifacts\ios14-d20ap\018-62062-016.dmg`，4,681,242,228 字节，SHA256 `04e18e061ca74ab5ca53cc6fc4d227844ff9a1490feb3036325f3b04d5e93b15`。ZIP CRC 通过；UDIF 逻辑磁盘 7,272,429,056 字节，GPT 内含 Apple_APFS。
- APFS System 卷区分大小写，没有 FileVault，卷占用 6,833,553,408 字节。在 WSL `/home/jia/ios-mix1-build/ios14-root-ro` 只读挂载；真实根位于其 `root` 子目录。未在手机挂载。
- APFS 工具：`sgan81/apfs-fuse` 固定提交 `66b86bd525e8cb90f9012543be89b1f092b75cf3`，其 LZFSE 子模块 `e634ca58b4821d9f3d560cdc6df5dec02ffc93fd`。WSL 本地编译，挂载明确 `-o ro`。
- launchd、dyld、SpringBoard 可执行文件为 ARM64，CPU subtype 0；共享缓存 `dyld_shared_cache_arm64` 为 2,218,328,064 字节。逐文件哈希和权限见 `ios14-root-files.json`。这些是电脑读取证据，不是用户空间执行证据。
- RestoreRamDisk 为 IM4P 包装，尚未解包为文件系统。不能直接当作原始 ramdisk。

## UFS 第七轮

复用 ios-sailfish 固定提交的 0028 只读 BSD UFS 驱动和 0034 高位 DMA 检查，适配 XNU7195。源码由 `scripts/prepare-ios14-native-ufs.py` 生成 0018 补丁；未修改参考仓库。第七轮仅去掉了不存在的 `KMA_DATA`，仍传入 `KMA_ZERO | KMA_PERMANENT`，这一适配不完整：虽然宏存在，7195 的 `kmem_alloc_contig` 明确拒绝它们。

MIX 原厂 DT：控制器 `/soc/ufshc@624000`，reg `[0x624000,0x2500]`，PHY `0x627000`。DMA 位于当前 XNU 上方 RAM 窗口；驱动先检查控制器 64 位地址能力。只有主机启用、设备存在、队列就绪且没有未完成命令时才接管传输表；确认新表地址生效后才发命令。每次请求都复查表基址。发生超时后不复用该 DMA 缓冲。

只允许 TEST UNIT READY、READ CAPACITY(10)、READ(10)，BSD 写接口返回 EROFS。当前启动参数只有 `lithium_ufs_probe=1`，没有设置 `rd=ufsroot0`，也没有格式化或挂载现有 Android 分区。

预期顺序：映射控制器 → 打印 VER/CAP/HCE/HCS/DMA → 容量 → protective MBR → GPT 主表与分区数组 CRC → userdata 边界 → 首块 CRC/文件系统 magic → `read-only probe result 0`。随后仍会进入普通根匹配等待；该等待不表示探测失败，也不表示已挂载根盘。

检查的已测几何来自 Android sysfs，不是参考手机：LUN0 预期 251,112,980,480 字节，逻辑块 4096；userdata 起点 402,653,184 字节、长度 250,710,192,128 字节。此 LUN 对应关系仍须由真实 READ CAPACITY/GPT 验证。GPT 解析拒绝错误 CRC、越界表、溢出计数、反转/越界分区、重名及目标分区重叠。16 项合成接受/拒绝检查及 CRC32 已知答案通过，报告 `ios14-native-ufs-validation.json`；合成数据不是真机 GPT。

第七轮内核 `a092ee4ebf1c6625de2d6d01ec20300583beaf5b74b828c6df9acdfffe1cf1e3`；镜像 `cf31b9fd1eee1d14dae16f80bea97ddadd7858baec06f1d2c12f33dcfe227846`；冻结目录 `reports/local/20261113-144858-642552-native-xnu`。

照片 `native-xnu-ufs-dma-failure.jpg` 的构建时间 14:47:31 对应该内核，显示 `no contiguous DMA memory`、`read-only probe result 12` 和 `Still waiting for root device`。12 是驱动将任意分配失败转换后的 ENOMEM，并非内核分配器的原始返回码。源码 `osfmk/vm/vm_kern.c:131` 只允许 `KMA_KOBJECT | KMA_LOMEM | KMA_NOPAGEWAIT`，因此该请求会在查找页面前返回 KERN_INVALID_ARGUMENT。没有证据表明真实连续内存耗尽。

0019 独立补丁将该调用改为 `KMA_NONE`，保存并打印原始 `kern_return_t`；分配成功后显式清零全部 `0x104000` 字节，逐页验证物理连续性。该 API 自身将分配页 wire，驱动保留映射直到重启，不需要不受支持的 PERMANENT 标志。没有修改通用分配器或减少 GPT 校验。

`ios14-native-ufs-dma-validation.json` 检查了真实编译指令：新内核 allocation call=0xfffffff0073ae9d4，x6=0；bzero call=0xfffffff0073aea18，长度0x104000；原始7195接口允许标志掩码0xe。同一检查读取冻结第七轮二进制并拒绝其x6=0x1040。此检查不运行内核，不证明真机 DMA 已成功。

第八轮内核 `5af454ac62268580190af3a3e81c038ed1bcfe344d0ea9050de3131881384fce`，镜像 `78aabc92c42ce86f99dcf6ebacb7a55d44c7c2aef0a01a90f8864ab9e7dac2c1`。在用户手动恢复、重新核验 lithium 和 boot_completed 后执行临时启动，没有持久分区写入。

## 第八轮实机读取结果

照片冻结在 `reports/local/20261113-145705-688590-native-xnu/native-xnu-ufs-read-success.jpg`，SHA256 `7552e4365ac94d238f0f0e16a06e7aea2a87c8c21e2d32a166a2346fc88ae863`。`kr=0`、DMA 清零及连续性检查通过，PA `0x102630000`；VER `0x10100`、CAP `0x107001f`、HCE=1、HCS=`0x10f`。LUN0 last LBA=`0x3a777ff`、块大小4096，与 Android 几何一致。GPT 头和数组 CRC 通过，userdata 起点 `0x18000`、239095 MiB；首块读取成功，`read-only probe result 0`。随后普通 IOMedia / Apple_HFS 根匹配等待符合探测模式设计。

用户已手动恢复 Android；ADB 重新核验 lithium / boot_completed=1。八轮均仅临时启动，没有 root、清除或持久写入。

## 根文件系统接入

当前内核没有 APFS 驱动。采用参考路线，将解压后的真实 iOS 文件写入 HFSX。Apple hfs-556.100.11 固定提交 `ba1a9cdc6e83ae9dfb61e4b5a822fbf185ae5866` 已通过0020接入7195，0021为精确 `rd=ufsroot0` 提供 BSD 根设备。新内核 SHA256 `17977540d63a9440e05ee09e38935e5c9b41ad1561849c6244222e521fa0919a` 构建、指令扫描、alias及DMA检查通过，尚未实机测试。HFS在vfsinit后注册；明文根卷不启用AppleKeyStore内容保护，iOS配置不构建经典HFS转换器；低空间阈值的唯一浮点表达式改为等值整数运算。参考checkout未修改。

WSL没有hfsplus内核模块，使用纯主机工具：hfsprogs 540.1.linux3-5build3，以及libdmg-hfsplus `7ac55ec64c96f7800d9818ce64c79670e7f02b67` 的专用导入器。保留文件内容、uid/gid、mode、时间及符号链接；源卷没有普通文件硬链接，遇到未支持类型或规范化冲突即失败。HFS Unicode分解和POSIX冒号到盘上斜杠映射已适配；10项小卷内容/元数据/链接验证与fsck通过。APFS扩展属性、资源fork未复制，文件内容已解压，不保留decmpfs标记。

完整根卷清单共329334项（238674文件、88961目录含根、1699符号链接），文件内容8651900353字节。目标是E盘16GiB HFSX镜像。导入、逐文件内容和元数据验证、只读fsck及SHA256全部通过后才能用于设备。共享缓存超过2GB，不能塞入约40MiB的bootloader装载窗口；完整根卷需要写入已授权userdata，XNU侧首次只读挂载。

当前UFS读取已通过；HFS mount、launchd、timer IRQ和持续调度稳定性仍未实机证明。任何后续持久写入必须单独记录，不能继续描述为“全程仅临时启动”。

主机大卷适配补充：原libdmg分配器清零使用32位乘法，超过4GiB会回绕；已将磁盘字节偏移提升到off_t。6GiB稀疏测试卷在4328521728位置分配，修正版保留33554432位置的低位哨兵、文件校验及fsck通过；仅恢复旧乘法的独立构建实际覆盖了低位哨兵。证据见ios14-hfs-large-validation.json。另将extent overflow索引改为fork内逻辑块号，匹配原读取/移除代码。导入使用固定长度、有界mmap，verify只读打开；先前不完整镜像已隔离。

传输采用Ubuntu固定android-libsparse 34.0.4-1build3。解码器对每个RAW/FILL字节重新计算SHA256，拒绝DONT_CARE、截断、尾随数据和内容篡改，避免在根卷范围内遗留旧Android字节。小卷正反例见ios14-root-transfer-validation.json。真机脚本再次检查序列号、MSM8996_A8、BL解锁、userdata容量及max-download-size，使用fastboot -S 256M；完成后留在Fastboot，随后直接临时启动新XNU。根模式携带镜像首4096字节CRC，实机读取不匹配即拒绝挂载；CRC仅验证卷头，不宣称全盘读回。

该完整镜像末尾包含约6267.94MiB的FILL零填充，仍需由bootloader实际写入。因此36/36发送完成不代表刷写完成，最后一段可能明显慢于中间分段；以最后的Writing OKAY和进程返回0为准。

后续主机工具还修复了overflow extent记录写入的大端字节序。独立目录构建的回归测试将两份文件交错分配为每份24个extent，关闭并重开卷后逐字节核验，fsck通过，见ios14-hfs-extents-validation.json；没有在正在运行的完整根卷导入过程中替换其可执行文件。

16:00主机Windows重启（Win32_OperatingSystem.LastBootUpTime），导致完整卷导入在约285000项处中断，USB手机未做新操作。重启后未完成卷的只读fsck通过；APFS源已按原参数重新只读挂载。新增resume模式先运行fsck，每个已有文件都比较内容、类型、大小、uid/gid和mode，发现不符即失败，只创建缺失项。小卷验证拒绝owner不匹配，恢复正确所有者后完成缺失文件导入、全量verify与fsck。完整卷恢复进行中；全量成功报告生成前不用于设备。

2026-11-13 16:59恢复结果：完整根卷16:19已完成全量内容/元数据验证、fsck和SHA256（见ios14-hfs-root.json、ios14-hfs-recovery.json）。16:24的Windows蓝屏中断了E盘归档；按用户要求限制WSL为8GB/4处理器/2GB swap，并以单任务6GB、2线程恢复工作。中断归档未通过前缀比较，保留并隔离；完成重新复制和E盘副本读回哈希验证。原始根卷16GiB，SHA256=396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3；稀疏镜像9383190364字节，SHA256=d9d1bb002bb53259c718ff2b1ca7f6132609ee420e2a619711ef130c11807751。稀疏解码全部字节匹配，DONT_CARE为0，见ios14-root-transfer.json。该轮没有操作手机分区，也未实机挂载根卷或启动launchd。

## 第九轮根卷安装与启动（17:29）

完整根卷SHA256 `396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3`，16GiB；稀疏传输SHA256 `d9d1bb002bb53259c718ff2b1ca7f6132609ee420e2a619711ef130c11807751`，9383190364字节。36段Writing均返回OKAY，988.584秒完成；最后一段含大段零填充，实际Writing耗时535.085秒。写入日志：`reports/local/20261113-171243-160743-root-install/flash.log`。仅userdata被持久替换，不能再将本轮整体描述为仅临时启动。

此前16:48的限资源增量构建生成新内核，旧启动包没有同步更新；安装前离线检查因此停止，尚未执行手机命令。随后通过 `scripts/run-wsl-limited.ps1 -Task native-package` 顺序完成1703600条指令扫描、alias与DMA检查、重新打包和离线启动校验，全部通过。当前内核SHA256 `1afe0a00b73c4ac2615fbcaab8425a27f6892ec9f35f41d99301ffe8108cb859`；启动镜像SHA256 `b33ea650354596bd3a46357bc72bf4d6afef72ab60e2b4be2e251790e6ffcb14`，16318464字节。

写入完成后直接从Fastboot执行第九轮临时启动，Sending及Booting均返回OKAY。冻结目录：`reports/local/20261113-172930-455158-native-xnu`。启动参数：`debug=0x14e serial=3 cpus=1 keepsyms=1 -v rd=ufsroot0 lithium_root_crc=0x6143e5cf launchdsuffix=release`。boot报告中的partition_writes_performed=false仅描述临时boot步骤；已完成的userdata写入记录在ios14-root-install.json。

当前屏幕证据待取得，USB未提供ADB/Fastboot；不据此判定XNU进入、根卷挂载或launchd成功。下一次观察需核对 `HFSX root header CRC32 verified 0x6143e5cf`、`vfs_mountroot succeeded`、`root vnode installed`、`load_init_program` 或实际panic。先保留照片；退出实验用电源＋音量下进入Fastboot，恢复Android须原厂recovery重建userdata。

2026-11-14更新：第九轮HFS卷头已读回，MD5Init空加密表异常已定位并修复；第十轮已临时启动，屏幕待观察。详见 `ios14-native-panic-1afe0a00.md`。
