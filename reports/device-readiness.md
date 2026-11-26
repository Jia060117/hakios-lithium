# 设备准备与授权更新

日期：2026-11-12（Asia/Shanghai）。本次按用户“免备份、必要时清除和自行 root”的授权继续准备设备。

## 已完成

- 探测唯一已授权的 ADB 设备，重启前核验 `ro.product.device=lithium`。
- 进入 Fastboot，`oem device-info` 实际返回 `Device unlocked: true` 与 `Device critical unlocked: true`。
- Fastboot product 返回 `MSM8996_A8`；原设备 ADB 序列对应的同一会话完成查询，没有根据这个通用 product 名识别其他设备。
- Fastboot 最大下载量 `0x20000000`，userdata 类型 ext4。`secure=yes` 原样记录；解锁结论来自 OEM device-info。
- 随后通过 Fastboot reboot 返回 Android，再次核验 lithium，`sys.boot_completed=1`。
- 普通 ADB 即可读取 6 个磁盘、68 个带 PARTNAME 的分区，共74个 sysfs 对象。名称来自内核 uevent；尺寸/起始位置来自 sysfs，逐项检查逻辑块对齐和父磁盘范围。

| 分区 | 实际块设备 | 容量 |
| --- | --- | --- |
| boot | `/dev/block/sde36` | 67,108,864 字节 / 64MiB |
| recovery | `/dev/block/sda13` | 67,108,864 字节 / 64MiB |
| userdata | `/dev/block/sda15` | 250,710,192,128 字节 / 约233.49GiB |
| persist | `/dev/block/sda12` | 33,554,432 字节 / 32MiB |

userdata 的 sysfs 容量与 Fastboot `0x3a5f7df000` 完全一致，名称由独立的 PARTNAME 确认。所有六个磁盘可读到的逻辑块大小均为4096字节；sysfs size/start 的512字节扇区计数已转换为字节，不能把二者混用。

Fastboot 的 boot/recovery 容量、unlocked、version-bootloader、current-slot 查询均返回空值；空值保持 null，不根据命令返回码0推定支持。这些分区的容量结论来自后续 sysfs 取材。未读取原始 GPT，不能宣称已校验 GPT CRC、备用表或原始分区 GUID。

## 当前操作状态

未创建手机备份，未执行 root、清除、格式化、刷写、重新分区或临时 recovery。准备阶段完成 Android→Fastboot→Android 查询往返后，已于同日完成三次自定义入口诊断的 `fastboot boot` 临时启动，均进入 Android 并取回记录；最后普通重启，确认回到原厂启动、诊断属性消失。备份不作为前置条件；需要 root/清除时按 `OPERATIONS.md` 中既有授权执行。

已按 ELF 程序头生成专用分析副本，解决 LLVM objdump 对原始节元数据的读取问题，原固件未修改。aboot 静态路径及真实 ARM64 入口已核验，详见 `reports/entry-bringup.md`。下一步是显示交接与完整 UEFI 内存占用，当前无需 root。

## 复现与材料

- `scripts/check-fastboot.py --enter-bootloader --return-to-android`：会重启，查询后返回，不刷写或清除。
- `scripts/check-fastboot.py --reparse-log <本地fastboot.json>`：完全离线重解析，不再次重启。
- `scripts/probe-blocks.py`：不需 root 的几何/分区名称探测，不读取分区内容。
- `reports/fastboot-status.json`：可共享的查询结果；详细日志路径在该文件中，原始序列号只保存在忽略目录。
- `reports/block-geometry.json`、`reports/partition-map.json`：本机实际几何和68个分区映射。

新的脚本已在本机实际执行成功；Fastboot 空值解析、JSON一致性和分区边界通过检查。iOS14、UEFI、7195内核和图形的实现状态没有因本次准备而变为已支持。
