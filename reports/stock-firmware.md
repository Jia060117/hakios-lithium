# 原厂固件提取记录

日期：2026-11-12（Asia/Shanghai）。目标仍为 iOS 14 / lithium。

## 输入与版本匹配

输入目录：`lithium_images_V9.5.2.0.NAHCNFA_20180405.0000.00_7.0_cn/`。
`misc.txt` 的 device 为 lithium，build_number 为 V9.5.2.0.NAHCNFA。
boot ramdisk 的 bootimage fingerprint，以及 recovery ramdisk 的 build fingerprint，均与此前保存的手机实测一致：

```text
Xiaomi/lithium/lithium:7.0/NRD90M/V9.5.2.0.NAHCNFA:user/release-keys
```

提取内核的版本与实机一致：`3.18.31-perf-g0bf156d-01100-g2a3feef`，编译日期为 2018-04-05。
boot 和 recovery 中的 kernel + appended DTB 内容完全相同。
两个镜像的 legacy Android SHA-1 ID、内核 gzip 和 ramdisk gzip 均通过内部一致性检查。
已为输入目录的 71 个文件和同名 TGZ 压缩包生成 SHA-256 清单，见 `stock-firmware.json`。
没有将这些本地哈希与厂商发布的可信摘要或签名进行认证比对。

## 输出

本地输出目录：`artifacts/stock/V9.5.2.0.NAHCNFA/`。

| 路径 | 内容 |
| --- | --- |
| `boot/boot.img` | 原厂 boot 的逐字节副本 |
| `recovery/recovery.img` | 原厂 recovery 的逐字节副本 |
| `boot/Image`、`boot/Image.gz` | 解压和压缩的 ARM64 Linux 内核 |
| `boot/kernel-dtb.gz` | 原始压缩内核及其追加的全部 DTB |
| `boot/ramdisk.cpio.gz` | 原始 boot ramdisk |
| `boot/ramdisk-text/` | default.prop、fstab.qcom 等选定普通文本文件 |
| `recovery/ramdisk-text/` | recovery 的对应文本文件 |
| `dtbs/00.dtb`—`dtbs/19.dtb` | 从原厂 boot 解析出的 20 份设备树 |
| `lithium-stock.dtb` | 按实机标识唯一匹配的完整静态 DTB |
| `lithium-stock.dts` | 该 DTB 的完整反编译文本 |
| `lithium-roundtrip.dtb` | 从 DTS 重新编译的验证输出 |
| `hardware-nodes.json` | 显示、GPU、UFS、USB 等候选节点摘要 |
| `stock-partitions.json` | 原厂 partition.xml 的分区标签、标称容量和 UFS LUN |
| `dtc-*.log` | dtc 原始警告日志 |

## 选中的设备树

20 份候选中，只有索引 **3**（第 4 份）与实机已保存的 7 个根属性的原始字节全部一致。
这些属性为 model、compatible、qcom,msm-id、qcom,board-id、qcom,pmic-id、#address-cells 和 #size-cells。

| 项目 | 值 |
| --- | --- |
| 大小 | 438684 字节 |
| 节点数 | 1898 |
| msm-id | 十进制 `[305, 65536]` / 十六进制 `[0x131, 0x10000]` |
| board-id | 十进制 `[38, 0]` / 十六进制 `[0x26, 0]` |
| pmic-id | 十进制 `[131081, 65555, 0, 0]` |
| SHA-256 | `93dc50ef24a5a6e56c19346ce5c1aa43c11f1e1842970d150020cde6c3b79985` |

WSL Ubuntu 的 dtc 已完成 DTB → DTS → DTB 往返转换。
重新解析后，所有节点、每项属性的原始字节及 memreserve 条目均与输入一致。
另用独立的 fdtget 读取确认 msm-id 和 board-id。
原厂树存在历史格式警告：反编译 252 条、重新编译 1091 条。两个命令均成功退出；没有修改原始属性来消除警告。
这只验证结构和属性保存，不证明设备树可直接用于 UEFI 或 iOS。

## 重要边界和下一步

1. 取得的是原厂包中的完整静态 DTB，不是手机当前启动后经过 bootloader 修正的运行时 FDT。原厂 `/memory/reg` 为 `[0, 0, 0, 0]`，不能拿它直接建立实际 RAM 映射。
2. 原厂 boot/recovery 是恢复参考材料，尚未从实机读取对应分区；它们不包含本机独有的 persist、基带校准或用户数据备份。
3. partition.xml 是包内标称布局，部分末尾分区会随存储容量扩展；不能将其视为真机 GPT 或直接推导 `/dev/block/sd*` 映射。
4. 本轮未运行任何刷机脚本。包内 flash_all 会刷写、擦除分区；flash_all_lock 系列还涉及重新锁定，不属于本轮离线提取步骤。
5. 完成提取时 `adb devices -l` 列表为空；离线步骤已完成。手机恢复连接后，优先补采运行时 RAM 范围和引导修正信息，再进入 UEFI 适配研究。

## 镜像摘要

```text
boot.img      2c070c67b07000a02de42fa05a6bcd93b7c32b3770d3fa9e495f321626278f44
recovery.img  325fc5f46c4ea29dad53a6bdfad2bfaf9bb49b5ffc50b69f188e3c506918f511
```

可重复执行脚本：`scripts/extract-stock.py`。脚本只访问电脑本地文件与 WSL dtc，不调用 ADB、Fastboot 或固件包中的脚本。
