# HakiOS MIX 第13轮：加载器卷查找失败

2026-11-15 17:26:47北京时间，核验授权MIX身份、解锁、userdata容量及电池启动条件后，临时启动P2只读存储包，Fastboot接受。镜像SHA256：`28fdb35a51f2f9a512e50d109156cafb7b842e9e237d978b00e29402d3b8b78c`。完整同次记录见 [JSON](hakios-device-r13.json)。

用户照片确认MMU/HOB、BDS及GOP，红字停点为 `HAKIOS LOADER FAILED / FIND VOLUME CODE 8000000000000003`（EFI_UNSUPPORTED）。下方有FAT安装和多组块设备日志；没有EFI文件校验、LoadImage/StartImage、内核或诊断PID1证据。本轮未完成UFS/HFS文件读取验收。

冻结FAT首扇区实际包含type=0x01、start=0、blocks=2048的MBR记录。新启用的完整Mu PartitionDxe据此建立FAT子句柄，FAT协议在子句柄上发布；BDS仍遍历连接驱动前取得的BlockIO数组，只查询原RAM父句柄的文件系统协议。该实现缺陷与照片中“FAT已安装但FIND VOLUME返回不支持”吻合。修正为先连接UFS及RAM设备，再重新枚举当前SimpleFileSystem句柄，仅接受RAM来源的加载器卷，继续验证EFI完整长度和哈希。

本轮仅一次fastboot boot，累计HakiOS临时启动13次，无分区写入。原图已保存并记录SHA256；最后画面为UEFI失败摘要，用户未确认再次回Fastboot。P1三次合格保持有效；当前新内核、PSCI交接、HFS和18H17文件读取仍未验证。未运行旧7195或QEMU。
