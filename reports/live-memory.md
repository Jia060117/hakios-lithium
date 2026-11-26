# 运行时内存和设备树采集

日期：2026-11-12（Asia/Shanghai）。设备：lithium / MSM8996pro，项目目标仍为 iOS 14。

## 已完成

ADB 已重新连接并授权，手机保持 Android 运行。通过非 root 的 ADB exec-out 读取整个可见 sysfs 设备树，取得：

- 1898 个节点、14615 项属性，原始 tar 保存在 `artifacts/live/`。
- 所有原厂节点和属性均存在；7 个设备标识根属性与匹配的原厂 DTB 完全一致。
- 排除 1898 项 sysfs 自动生成的 name 属性后，原厂与运行时树仅有 7 项差异。
- 通过独立读取 `/memory/reg` 再次核对 RAM 二进制值。
- 最新常规探测时电量 25%，USB 正在供电；没有 root、重启或任何分区写入。

具体归档路径、SHA-256、结构化范围和差异清单见 `live-memory.json`。

## RAM 范围

根节点的 #address-cells 和 #size-cells 均为 2，以大端 64 位地址/长度解析。
下表终点均**不包含在范围内**。

| 起点 | 终点（不含） | 长度 |
| --- | --- | --- |
| `0x40000000` | `0x100000000` | 3072 MiB |
| `0x100000000` | `0x1bd950000` | 3033.3125 MiB |

合计声明 RAM 为 6401884160 字节，约 5.9622 GiB。
这是设备树向内核声明的 RAM，不是扣除保留区、内核、initrd 后可供新加载器自由使用的空间。

## 保留区

| 节点 | 固定地址 / 分配方式 | 长度 |
| --- | --- | --- |
| removed_regions | `0x85800000`，no-map | 68 MiB |
| modem_region | `0x89c00000`，no-map | 98 MiB |
| peripheral_region | `0x8fe00000`，no-map | 43 MiB |
| cont_splash_mem | `0x83401000` | 36 MiB |
| cont_splash_mem_hdmi | `0xb1c00000` | 36 MiB |
| dfps_data_mem | `0x83400000` | 4 KiB |
| ocimem_log | `0xb4000000` | 1 MiB |
| adsp_region | 动态池，reusable，实际地址未取得 | 4 MiB |
| secure_region | 动态池，reusable，实际地址未取得 | 92 MiB |
| qseecom_region | 动态池，reusable，实际地址未取得 | 20 MiB |

这是 reserved-memory 节点清单，不是完整的所有占用/保留空间清单。动态池的分配窗口见 JSON，不能把分配窗口当成实际已占用位置。

## 启动时差异

原厂 `/memory/reg` 的零占位值已被修改为上述两段 RAM。
另外 6 项都位于 `/chosen`：bootargs、linux,initrd-start、linux,initrd-end、hwversion、pureason、smeminfo。
当前 Android initrd 为 `[0x42a00000, 0x42c28cf1)`；这只描述当前启动，不指定未来 UEFI 的加载位置。
原始 bootargs 保留在本地归档中，未把设备唯一标识复制进共享报告。

## 采集验证和边界

实测 MIUI 9 / Android 7 的 shell 二进制通道会把部分 LF 改写为 CRLF，即使指定 `adb shell -T` 也会影响数据。
初次 shell 归档因此被判无效并单独留存，未用作结果；有效归档通过 exec-out 重新采集。
已检查块对齐、tar 头校验和、结尾、成员类型、所有原厂节点/属性存在性，并与另一次二进制 memory/reg 读取一致。
复用脚本为 `scripts/capture-live-dt.py`，常规探测脚本也已改用 exec-out 读取二进制属性。
此前从手机取得的 7 个根属性都已与原厂 DTB 按字节匹配，不受这一传输问题影响。

`/sys/firmware/fdt` 原始 blob 仍受 root-only 权限限制；sysfs 快照不包含它的原始头部和 memreserve 二进制块。
`/proc/iomem` 与 memblock 调试信息也无法通过当前 shell 权限读取。
因此还不能声称已取得完整的安全分配内存表；后续 UEFI 必须结合平台内存表、固件占用和加载地址审查。
本轮资料采集已完成，下一阶段为 lithium UEFI / MSM8996 引导链的静态核对。
