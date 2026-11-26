# 完整 HakiOS 的 MIX iOS16 设备层

唯一活动基线是完整 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`、XNU8796.121.2，目标为用户提供的 iPhone10,3 / D22AP / iOS16.5 / 20F66 / arm64。7195只读，不构建或运行；不使用QEMU。迁移前代码在 `archive/ios14/` 和备份分支，历史运行结论不构成新iOS16验证。

上游Mach、BSD、VM、IOKit、IOSurface、系统服务和用户空间注入实现完整保留。活动补丁仅包含MSM8996pro/ARMv8.0能力、GIC redistributor stride、BGR24物理显示与控制台交接、UFS buffer映射、Mu/加载器契约和日志通道。用户空间仍从UFS按需读取，阶段三准备上游System/cryptex/Data。

内存目标为6GB机型的全部可用RAM。首个扩大窗口为 `[0x100000000,0x1b4000000)`，2880MiB；启动载荷和驻留UEFI堆各保留64MiB，位于窗口外。这些范围在已采集的上部DDR内，并避开记录的保留区。低地址DDR包含固定与未知动态保留区，XNU当前启动分配器使用连续窗口，尚未开放；不能把2880MiB当成最终上限或宣布6GB已可用。配置见 `config/hakios-lithium-memory.json`。

USB复用ios-sailfish的同版本DWC3 EP0组件，按MIX原始DT和MSM8996 GCC替换地址与时钟门控，新增只向电脑发送日志的bulk IN接口。`lithiumusb=0`关闭、`1`只读探测、`2`在电源/时钟和device mode满足条件时接管。默认关闭，待阶段四联合测试启用；不满足MMIO条件则跳过。后台轮询，DMA缓存显式维护，不阻挡桌面启动、不接收执行命令、不写存储。来源见 `drivers/usb/usb-provenance.json`。

日志读取沿用上游256KiB启动头和1MiB环形缓冲区，帧包含绝对字节偏移与丢失计数，电脑端保留原始帧和文本。采集入口为 `scripts/capture-hakios-usb.py`，使用PyUSB/libusb，Windows使用单独日志设备的WinUSB兼容标识。枚举、Windows驱动绑定、传输和完整日志捕获仍未真机验证；不能称为已可用的USB功能。它不是通用USB或ADB实现。

`hakios-prepare/check/deps/kernel/support` 分别同步、配置检查、依赖准备、完整内核和完整Mu/loader构建。新工作空间和产物使用ios16独立目录；旧诊断和打包入口已阻止。阶段二仅电脑端构建与检查，原始D22AP身份、System/cryptex/Data和启动包在阶段三准备，阶段四才上机。
