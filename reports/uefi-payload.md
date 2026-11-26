# iOS 14 EFI 的内存 FAT 集成

日期：2026-11-12。目标保持 iOS 14.8 / 18H17。本阶段执行的是适配 7195 boot_args 的 DarwinBoot EFI 入口及 loader 协议安装，未调用其 Boot 方法，没有加载 XNU 或启动 iOS 用户空间。

## 已实现

`scripts/prepare-uefi-payload.py` 确定性生成 1 MiB FAT12 卷，包含 `DARWIN14.EFI`、`CHECK.TXT`、`PAYLOAD.JSN`，由 mtools 独立逐文件读回。EFI 为 36,352 字节，SHA-256 `9af607ab2aebeb3bd60f4fbcf87c7ce0b49bdf7eba5c99f19db941647e358386`。

卷由 aboot 按扩大 ramdisk 明确加载至 `[0x44d00000,0x44e00000)`，原厂 initrd 保持原字节。`LithiumRamDiskDxe` 发布只读 BlockIo，检查媒体 ID、边界及写保护；标准 DiskIo/FatDxe 挂载。文本控制台提供真实 SimpleTextOut，更新系统表 CRC。

BDS 读出完整 EFI，验证长度、文件结束及 FNV64 `0x75f981c4a004af25`，正式发送 EndOfDxe 后调用 LoadImage/StartImage，再检查 UniXNU loader revision `0x20000` 和 Boot 指针。安全模块保留；结束 DXE 事件符合固定 Mu 的 SecurityStub 加载策略。FNV64 是内容诊断，主机输入另用 SHA-256；未将它描述为密码学签名验证。

新增 DXE 连续分配空间位于已测原厂 Image 保留窗口内的 `[0x40400000,0x41000000)`。本版恢复 stub 在关闭 MMU/缓存后复制原厂内核，并由执行 CPU 逐字读回比较完整 `0x1ff9000` 字节；失败绘制红块。此检查是第八轮新增，不能倒推旧版本通过。

## 电脑验证

普通场景与 SCTLR.A/4 字节 FDT 对齐场景均实际执行 EFI，返回 flags=7 和预期 FNV64，并验证恢复复制、采集器、同次状态副本、20 次 timer 和服务退出。损坏文件负例只翻转 QEMU 临时副本的 EFI 首字节，保留 FAT 结构，确认固件拒绝加载；其测试通过不表示 EFI 被执行。

对应报告为 `uefi-qemu.json`、`uefi-qemu-strict-align.json`、`uefi-qemu-payload-reject.json`。QEMU 不执行 Qualcomm Linux；原厂 Linux 继续入口用停止指令替代。这些结果不能替代真机外设或 Android 返回验收。

## 第八轮真机证据

真机临时镜像 SHA-256 为 `f3d3f4b0fc89dc12c2fb183b2c3f000c41a3696cec62e274597160ef70251636`，构建标识 `0x23523fa6b21e4e74`。用户明确反馈：显示 `FAT FILE AND EFI PASS`、五个青色块、无红块、没有进入 Android。

条件 PASS 证明文件读取、内容检查、EFI 入口和 loader 协议检查走完；第五块证明本版 CPU 复制读回、采集器和同次状态副本完成。证据来源为用户文字，尚无本轮照片或恢复的同次二进制 journal；末尾分支和 Linux 主体执行仍未证实。详细输入与结果见 `uefi-device-attempt-08.json`，本地冻结文件在该报告指定的忽略目录。

下一步定位 Linux 最早入口及异常，并继续 7195 内核工具链、ADT、内核分配窗口和 24 位显示适配。现有 EFI 成功不改变 SpringBoard、真实触摸及系统应用的最终验收要求。
