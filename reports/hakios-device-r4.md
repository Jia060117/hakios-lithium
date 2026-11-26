# HakiOS lithium 第四轮真机测试

2026-11-14 20:43:29（北京时间）核验授权 MIX 的身份、解锁和 userdata 容量后执行一次临时启动，Fastboot 返回 OKAY。本轮将上游 loader 的 lithium 首选地址设为 `0x40400000`，并输出标准 UEFI 加载返回码与空闲内存。用户照片明确显示 `LOADIMAGE CODE 8000000000000009`，日志显示 `MIX LOADIMAGE STATUS OUT OF RESOURCES`：失败已定位到 LoadImage 的内存分配阶段，尚未执行 StartImage。

本轮镜像 SHA256：`a4d31ced18b0e308eee0abac44c16e3ee55fd19ba913646c36e057795e97fb09`。固定 HakiOS 8796.121.2 内核 SHA256 仍为 `02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698`。完整冻结输入位于 `artifacts/hakios/device-tests/20261114-204329/`；照片、命令结果与验收状态见 [机器记录](hakios-device-r4.json)。

照片再次证实 UEFI、MMU/HOB、BDS、GOP 和 EFI 文件校验。PE 内存映像大小 `0x96B000`，约 9.4 MiB；日志所示最大空闲块为 `0x709000`，约 7.0 MiB。首选范围 `40400000 40D6AFFF` 跨越多个内存条目，Mu 首选分配与正常任意地址回退均未取得足够连续空间。此轮证明此前地址修正不足以解决装载问题。

下一轮在已记录高位 DDR 中为 UEFI 增加独立 64 MiB 常规堆 `[0x124000000,0x128000000)`，由完整 Mu 的现有内存和 PE 装载实现管理。静态检查核对实测 RAM、全部原厂保留范围及动态分配窗口；该堆与 XNU 独占的 `[0x100000000,0x120000000)` 和 XNUP 的 `[0x120000000,0x124000000)` 分离。该修改的真机有效性仍待复测。

本轮仅临时 boot，没有分区刷写、清除、格式化、重分区、旧7195执行或 QEMU。用户提供照片后确认已返回 Fastboot。HakiOS 内核、hello、timer/线程、iOS14、GPU 和触摸均未通过验收。
