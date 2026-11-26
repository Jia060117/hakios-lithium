# HakiOS lithium 第二轮真机测试

2026-11-14 20:19:04（北京时间）在用户确认手机稳定处于 Fastboot 后，重新核验身份、解锁及 userdata 容量，执行一次临时启动。Fastboot 返回码 0，发送和 Booting 均 OKAY。用户报告 **“几行字一闪而过就没了，都来不及捕捉”**，随后明确确认重新出现 MIUI 启动画面。本轮仍未通过平台启动验收。

本轮镜像 SHA256：`a407484baee0ec46799d0a44e08ff1f38f099573945c7ab1021493321f6cb76e`。完整 HakiOS 内核、hello 和 XNUP 的哈希与首轮相同；活动内核仍为固定 HakiOS 8796.121.2。首轮基础上增加了 BootShim 早期 APSS 看门狗关闭、异常停屏和四个交接检查点，并校正 Image 保留长度。源文件、镜像及加载组件冻结于 `artifacts/hakios/device-tests/20261114-201904/`，完整索引见 [机器记录](hakios-device-r2.json)。

闪过的文字没有内容、照片或日志可核对，不能据此确认 UEFI、BDS、loader 或内核实际执行。四个检查点与异常红块也没有获得可辨识观察，故精确失败阶段保持未知。后续查询原授权设备重新处于 Fastboot，ADB 为空；回到 Fastboot 的具体方式没有用户证据，不计为自动恢复。

正常构建、配置/依赖及静态包核验通过；Mu 23个已编译模块的反汇编扫描没有命中非兼容 PAN/UAO、LSE 或 PAC 指令。原厂20份 DTB 未改动。没有刷写、清除、格式化、重分区、旧7195启动或 QEMU。

现有 Mu SEC 在平台绘字后会清空显示缓冲区，而本机 SerialPortLib 原先仅将上游日志写入保留 RAM。上游 SEC 异常处理本来会通过 SerialPortWrite 打印异常地址，再进入 CpuDeadLoop；原 PcdDebugPropertyMask `0x03` 未启用断言死循环。这解释了当前诊断可见性的缺口，但尚未定位重启原因。下一轮把同一上游日志流显示到现有 MIX framebuffer，复用现有字体，并启用上游 `DEBUG_PROPERTY_ASSERT_DEADLOOP_ENABLED`（mask `0x23`），捕捉原始错误。没有补造另一套异常或运行服务。

两轮都只证明 Fastboot 接受镜像。内核、hello、timer/线程、iOS14、图形和触摸的验收状态保持未通过。
