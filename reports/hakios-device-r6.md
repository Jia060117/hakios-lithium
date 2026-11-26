# HakiOS lithium 第六轮真机测试

2026-11-14 21:06:29（北京时间）核验授权 MIX 的身份、解锁和 userdata 容量后执行一次临时启动，Fastboot 返回 OKAY。两张用户照片确认 `LOADIMAGE OK`、`MIX LOADIMAGE STATUS SUCCESS CODE 0`，随后 `XNULOADER V3`、`PLATFORM LITHIUM` 和内核准备日志。完整上游 loader 已在真机执行，并完成内核/ramdisk、设备树和 boot args 准备。没有 XNU 入口或 hello 执行证据，P1 尚未通过。

本轮镜像 SHA256：`bfe2b8c3e7e6acea6b929ebe0eab4ab07d4226fa2f0e0a00d080a6156ec71ac7`。固定 HakiOS 8796.121.2 内核 SHA256 仍为 `02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698`。完整冻结输入及照片位于 `artifacts/hakios/device-tests/20261114-210629/`；同次哈希和逐项状态见 [机器记录](hakios-device-r6.json)。

loader 实际装载地址为 `0x127695000`，内存大小 `0x96B000`，完整落在第五轮增加的高位 UEFI 堆中。这证实标准 Mu 装载回退可以利用该堆满足完整 loader 的连续内存需求。此证据属于第六轮，不能反推第五轮具体执行阶段。

最终照片显示 `UEFI EXCEPTION TYPE 0 ELR 12769A574 ESR 96000147 FAR 415D0000`，loader 偏移 `0x5574`。对同次 loader 反汇编，首选映像地址 `0x40405574` 的指令是 `dc cvac, x10`，对应交接前 `clean_dcache(sp_now - 0x2000, sp_now + 0x2000)`。ESR 表示同 EL 数据访问异常、缓存维护、三级地址转换失败。FAR `0x415D0000` 正好等于板层 UEFI 栈 `[0x41590000,0x415D0000)` 的上边界；其后有未映射空隙。日志 `JUMPING TO ...` 打印在实际缓存清理及分支之前，不能计为已经执行内核跳转。

下一轮将 lithium 的交接栈清理限定为实际分配的完整栈范围，并同步修正 `jump_to_kernel` 中的最后一次清理范围。静态核对 loader 常量、Mu 内存图及栈 PCD 一致，继续复用上游缓存与交接实现。

本轮没有分区刷写、清除、格式化、重分区、旧7195执行或 QEMU。用户随后确认已用实体按键返回 Fastboot。iOS14、timer/线程、GPU与触摸均未通过验收。
