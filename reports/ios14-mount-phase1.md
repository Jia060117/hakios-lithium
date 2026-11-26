# iOS14 mount-phase-1：第十五轮证据与第十六轮修复

更新：2026-11-14。第十五轮已运行 launchd 主程序、fsck 和 mount；fsck 明确跳过了容器检查，不能记为 fsck 检查通过。最后是 mount-phase-1 exit(66) 用户态 panic，尚未运行 SpringBoard。

照片冻结于 `local/20261114-170643-581033-native-xnu/native-xnu-mount-phase1-panic.jpg`，SHA256 为 `5ba6d47dc73987ced7e87875d493343d859161f7722d28b71095b8800632037f`。

原版 mount 的本地反汇编显示 phase 1 要求绑定 `/private/preboot/<boot-manifest-hash>/usr/standalone/firmware`。必需的绑定在 0x1000043fc 调用 helper，0x1000063b8 调用 mount；源目录缺失返回 ENOENT，最终映射到 exit 66。同时 mount_hfs 更新根挂载返回 EROFS。不能仅忽略其中一个错误。

## 实现

- `0026-lithium-volatile-root.patch` 为指定根设备提供按需分配的 RAM 块写入层，4 KiB 粒度、最多 16,384 个脏块（64 MiB），哈希表额外 512 KiB。它只对本次启动有效，没有持久写入。超限、分配失败和底层读取失败返回真实错误，并保留精确已完成字节数。
- 物理 SCSI 传输、方向、白名单、DMA、GPT 核验函数与第十五轮逐字一致，仍只允许 TUR、READ CAPACITY、READ10。没有物理写入回调。对上层只暴露 16 GiB 根卷范围，算术溢出和末尾跨界均拒绝。
- 仅明确带 `lithium_root_cow=1`、原卷头 CRC 正确、设备树副本完整且各修改块原始 CRC 都匹配时，才启用写入层。任何 seed 失败都会释放已分配块并保持未启用状态。
- VFS 仅对已核验、已启用写入层的同一根设备清除初始只读标志。HFS 按原正常流程创建和使用日志；没有忽略脏卷保护或伪造写入成功。
- 在电脑上以 O_RDONLY 文件描述符和 MAP_PRIVATE 构建新目录，将原根卷的固件目录完整复制到所需 preboot 路径。24 项目录/文件形成 593 个变化块（seed 文件 2,433,704 字节）。原 mount 和 bindfs 仍执行实际挂载；不修改系统可执行文件或签名。
- 命名空间沿用固定参考 ADT 的 48 字节占位 boot-manifest-hash。它没有被宣称为 Apple 已认证的 manifest。新增内容只是原根卷已有固件文件的逐字副本，没有写入手机固件分区。
- RAM 根卷使用 XNU7195 原生 `vm_compressor=2` 模式，保留内存压缩，关闭磁盘交换，避免交换文件继续占用 RAM 写入层。

## 验证与边界

`ios14-preboot-seed.json` 记录完整原文件比对、复制文件核验和 fsck -f -n。原 329,334 个目录/文件的文件数据、文件记录和权限匹配；仅 preboot 目录增加一个子项。所有复制文件单独核验内容、UID/GID 和模式。主机原镜像 SHA256 保持 `396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3`。

`ios14-native-cow-validation.json` 记录 ASan/UBSan 下 12,000 次读写与独立字节数组比较，以及真实 adapter、真实内核 strategy 的边界、剩余长度、容量、分配失败、CRC、缺少属性和回滚测试。最终内核反汇编检查实际调用链；打包和临时启动绑定全部哈希与报告。既有 HFS、AMFI、固件信任、密码算法、DMA 和页表检查保持执行。

这些是主机验证。第十六轮的 HFS 可写、日志初始化、bindfs 挂载和后续启动阶段必须由实机屏幕证明。64 MiB RAM 写入上限仅适合当前启动验证；设备重启后所有新文件和日志变化丢弃，userdata 仍为原 iOS HFSX 根卷，不能默认启动 Android。
