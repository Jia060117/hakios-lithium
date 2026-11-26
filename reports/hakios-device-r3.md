# HakiOS lithium 第三轮真机测试

2026-11-14 20:30:24（北京时间）重新核验授权 MIX 的身份、解锁状态和 userdata 容量后执行一次临时启动，Fastboot 发送和 Booting 均返回 OKAY。用户照片确认完整 HakiOS 包进入 Mu UEFI，屏幕显示 `MMU AND HOBS OK`、`HAKIOS BDS`、`DXE GOP OK`、`HAKIOS EFI VERIFIED`，随后显示 `HAKIOS LOADER FAILED`。本轮已取得 UEFI 平台和 EFI 文件校验的真机证据，未取得 loader 入口、内核或 hello 执行证据，P1 尚未通过。

本轮镜像 SHA256：`1a84489835335b284cdeca1b92819e971aeddfb1b2935b468cb8a2545bf009f8`。完整 HakiOS 内核 SHA256 为 `02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698`，仍来自固定提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`，版本 8796.121.2。冻结输入位于 `artifacts/hakios/device-tests/20261114-203024/`，照片和逐项状态见 [机器记录](hakios-device-r3.json)。

照片日志中的 `CONVERTPAGES FAILED TO FIND RANGE A1000000 A196AFFF` 与本轮 loader PE 的首选地址 `0xA1000000`、内存映像大小 `0x96B000` 精确匹配。该首选范围不在当前 lithium UEFI 内存图中。PE 文件虽仅 64 KiB，但上游 BSS 使装载内存约为 9.4 MiB。它保留重定位信息，Mu 正常加载流程可以在首选分配失败后尝试任意地址，因此这条日志尚不能单独证明最终错误的唯一原因；本轮没有输出 LoadImage / StartImage 返回码。

第四轮将通过上游链接参数把 lithium loader 的首选地址设为已有的 `[0x40400000,0x41000000)` 常规内存窗口，并输出 EFI 空闲内存、LoadImage / StartImage 返回码及实际装载地址。使用现有 UEFI 加载与内存管理实现；不把未核验的高地址范围新增为可分配 RAM。

本轮仅临时 boot，没有分区刷写、清除、格式化、重分区、旧7195执行或 QEMU。用户随后明确确认已用电源＋音量下返回 Fastboot 并保持连接，可在新包构建和静态核验后复测。iOS 14 兼容、timer/线程、图形和触摸均未通过验收。
