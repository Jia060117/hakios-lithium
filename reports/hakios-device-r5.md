# HakiOS lithium 第五轮真机测试

2026-11-14 20:55:06（北京时间）核验授权 MIX 的身份、解锁和 userdata 容量后临时启动，Fastboot 返回 OKAY。用户报告 **“这次直接重启进miui了”**。没有可辨识的本轮 UEFI/loader/内核日志，失败阶段保持未知，不能由此前 UEFI 照片或本轮内存修改推定本轮 loader、内核已运行。

本轮镜像 SHA256：`e44328863c4d8eeba608111c93d89498160192e3bed6d70e2632b66bc93c4b89`。固定 HakiOS 8796.121.2 内核 SHA256 仍为 `02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698`。完整冻结输入位于 `artifacts/hakios/device-tests/20261114-205506/`；命令结果与逐项状态见 [机器记录](hakios-device-r5.json)。

本轮在实测高位 DDR 中增加独立 64 MiB UEFI 常规堆 `[0x124000000,0x128000000)`。静态包校验核对了实测 RAM、10个原厂保留范围/动态分配窗口以及活动板层其他内存区域，未见重叠；这些检查仅证明输入边界，不能证明本轮运行通过。完整上游 loader 的缓冲区保留。

下一轮在 LoadImage 之前及 LoadImage 成功、StartImage 之前增加可拍照的延时，并显示实际装载基址，以定位自动重启发生在装载还是后续执行阶段。继续使用完整 Mu 和 HakiOS 的标准加载/异常机制。

本轮没有分区刷写、清除、格式化、重分区、旧7195执行或 QEMU。MIUI 启动画面不证明 Android 完成启动，userdata 仍按已有安装记录为 iOS HFSX。P1、iOS14、timer/线程、GPU和触摸均未通过验收。
