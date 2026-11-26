# HakiOS 自动保留平台测试结果包

2026-11-15T00:49:20.964388+08:00：完成完整HakiOS依赖同步、loader/hello/Mu和内核构建、镜像及ARMv8静态核验。新内核和旧R8/R9包不同，尚未临时启动，不能把编译通过当作测量或显示的真机通过。完整输入见 [机器记录](hakios-unattended-test-ready.json)。

镜像：`artifacts/hakios/retained-result/lithium-hakios-test.img`；SHA256：`0c47097279c73ff89a024da8ce499da1f846fcb8529bffeb2852deab878604de`。内核SHA256：`f66cdf17db064cba0dc5cf37209f691db8b1b1cef975715f7b45f6a37b5d6105`。完整固定HakiOS基线仍为63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / XNU8796.121.2。

用户无需守屏。进入Fastboot并保持USB连接后临时启动，程序自行完成600次select等待并定时返回。结束摘要保留到重启，方便时提供一张结束画面或转录即可；十分钟运行是稳定性测试时长，中途照片和手动秒表均不再要求。

新增0003板级观测补丁只沿既有HakiOS计时、memory回调和返回日志扩展：timer每分钟显式写入loader日志，保存采样数、停滞数和最小IRQ增长；返回前直接保存最终PPI27起止/增量、CNTVCT起止、CNTFRQ和换算时长。hello最终600次等待、错误和getrusage切换统计沿原有上游实现。loader清屏显示原因和最终数据，原始日志仍在RAM。硬件计数器起点为内核EARLY_BOOT，hello elapsed_ms有单独起点；两者共享SoC时间基准，不宣称独立外部计时。CNTFRQ需实测为已知19200000Hz，异常显示valid=0。

结束屏包含 `LITHIUM CLOCK`、`LITHIUM FINAL`、最后 `LITHIUM TIMER`、`LITHIUM SELECT` 和 `LITHIUM HELLO`，以及 `MEASUREMENTS SAVED UNTIL REBOOT`。缺失、异常或提前返回均需按实际结果记录。未完成真实timer/线程十分钟和连续三次启动前，完整P1不通过。

修正了依赖补丁的串行组装及失败时恢复原源码，真实Git补丁组装/回滚回归与原路线隔离共10项通过。旧7195来源、脚本归档和历史文档保持只读。新包准备未新增设备启动或分区写入，未运行QEMU。iOS14、GPU、触摸和UFS仍未验证。
