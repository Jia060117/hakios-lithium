# HakiOS P2 只读存储测试包

后续状态：本包已用于第13轮，停在UEFI加载器卷查找；其输入保持冻结。下一轮改用[BDS卷枚举修正包](hakios-p2-storage-test-ready-bds-volume.md)，不要把以下历史准备状态当作当前手机状态。

2026-11-15：下一轮MIX实机测试镜像已准备，尚未启动手机。完整机器记录见 [JSON](hakios-p2-storage-test-ready.json)，冻结目录为 `artifacts/hakios/p2-storage-ready/`。P1已由R10/R11/R12通过，不再重复同一平台验收。

活动内核固定为完整HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2。下一轮验证MIX存储接入及真实18H17文件读取，尚不执行iOS的dyld或launchd。

| 产物 | SHA256 |
| --- | --- |
| lithium-hakios-test.img（15,724,544字节） | `28fdb35a51f2f9a512e50d109156cafb7b842e9e237d978b00e29402d3b8b78c` |
| 完整HakiOS内核 | `3c55962843b7bbc072981c647297ad0631f9e405ef250ff62e94c61a5433098a` |
| EFI loader | `9c8d682150e29cc59f9889286a330ccb84e6b10c629370a18423a0a16bc21355` |
| Mu firmware | `0d2b0d406046298783e62adf86ef054be6794a12ffe3227cd2253053680a2035` |
| APFS诊断根盘 | `e4c662b50594b0bbe58166fc1bbafc5637907f1dfba2d797aa62e44c6aeeef44` |
| 诊断PID1 | `77000ea899388da732a183b514704b58fb41bc959bee747b65225c0557b6e1b0` |

**实验路径**

Mu板层在本机HCI `0x624000` 接入warm UFS LUN0。标准描述符与SCSI传输取自原始ios-sailfish组件，固定提交 `6f27e41ff249f2680540ebe8ee5ab982c5e641c9`，来源哈希与改动记录已冻结。要求LK留下启用、空闲、有设备且支持64位DMA的链路；不实现PHY/时钟/电源冷初始化。仅允许TUR、READ CAPACITY和READ10，BlockIO写请求返回写保护；超时后停止再次提交并保留DMA内存。

GPT使用完整Mu的PartitionDxe，内核使用完整HakiOS UFS共享页桥和HFS实现。UEFI留在原核心，内核沿上游PSCI路径运行于MPIDR `0x100`，只给内核一个CPU。小型APFS根盘中的诊断PID1只读挂载 `/dev/rufs_userdata`，核对卷头与三个文件，再卸载并等待正常定时返回。未启用SM7325的native UFS路径。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 lithium_storage_test=1 loader_return_after=120
```

userdata期待几何为LUN0、4096字节块、起始LBA98304、61208543块。首8192字节FNV64为 `5b520a1785fef109`，偏移1024处为HFSX签名HX。文件期待来自实际18H17只读输入：

| 文件 | 字节数 | FNV64 |
| --- | ---: | --- |
| System/Library/CoreServices/SystemVersion.plist | 570 | `86e046dc36a18202` |
| usr/lib/dyld | 733616 | `30d75a79560d7677` |
| sbin/launchd | 403488 | `1a468883bb725db5` |

**上机方式和通过条件**

手机进入Fastboot并保持USB连接后，执行现有 `python scripts/boot-hakios-lithium.py --boot`。脚本先核验当前镜像哈希、输入、授权设备身份、解锁状态和userdata容量，再临时启动。当前活动镜像与冻结镜像逐字节一致。

无需守屏、秒表或中途抢拍。内核约120秒后返回，前面另有25秒加载停留；结束摘要按实现持续保留至重启，方便时提供一次照片或转录即可。这一新存储包的摘要显示仍须本次实机确认。

通过需要同次摘要确认：

- `LITHIUM P2 LOADER READY userdata HFSX read_only=1`。
- `HEADER` 的 `match=1`，`MOUNT rc=0` 且HFS只读标志通过检查。
- `VERSION`、`DYLD`、`LAUNCHD` 均为 `match=1`，长度及FNV与表中一致。
- `UNMOUNT rc=0`，`RESULT PASS read_only_mount_and_files`。
- `KERNEL TIMED RETURN OBSERVED`，没有panic，摘要持续保留。

若失败，保留UFS阶段和寄存器、LOADER FAIL/STOP、PSCI或kernel silence停点，以及已取得的P2阶段/内核panic原因。UEFI UFS阶段：1探测、2warm条件不符、3列表未接受、4TUR失败、5容量失败、6就绪、7协议安装失败、8读取失败。`rc_ocs_st`高32位为传输返回码，中间8位为OCS，最低8位为SCSI状态。遇到缺失数据时按未知记录，不从编译或命令接受推定真机通过。

**电脑端证据与边界**

完整HakiOS依赖、设备配置、Mu/loader、启用UFS的内核和诊断根盘均已构建。上游APFS builder生成根盘，apfsck判定clean，apfs-tool逐项读回一致。7项实际传输代码模拟通过，启用ASAN/UBSAN，覆盖写/LUN/长度/大DMA拒绝、列表归属丢失、读取完成缓存交接和超时禁止重用；这些是模拟MMIO检查，未使用QEMU。

5个实际二进制通过ARMv8指令检查；内核中核对 `_hakios_ufs_init`、`_hfs_mount`、`_hfs_vfsops`，两项存储DXE哈希与固件FV构建报告一致。独立解包冻结Android容器，核对header ID、gzip、原厂DTB表、FAT内EFI和XNUP中内核/APFS/命令行。原始Image为22,208,112字节，距早期Info区尚余332,176字节，未跨过已验证加载边界。运行时ADT由冻结loader生成，尚无此次实机ADT抓取。

99项源文件、配置、二进制、反汇编和证据已在冻结目录记录哈希。项目一致性检查另核对P1原图和历史冻结来源，不改写P1验收。此次准备没有设备查询、临时启动或分区写入；累计HakiOS临时启动仍为12次。手机最后确认画面为R12结束摘要，尚未确认再次回Fastboot，userdata仍为已安装的iOS HFSX。

P2新包的UFS、PSCI交接、HFS挂载和文件读取均待真机验证；iOS14跨版本ABI、私有服务、GPU、触摸和SpringBoard也未验证。旧7195仅保留只读对照，未修改、构建、打包或执行。
