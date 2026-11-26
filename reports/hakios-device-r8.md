# HakiOS lithium 第八轮真机测试

2026-11-14 23:43:29（北京时间）重新核验授权MIX身份、解锁和userdata容量后临时启动，Fastboot返回OKAY。三张同次照片确认完整HakiOS内核、上游PID1 hello、实际timer IRQ递增及线程等待/切换；最终600次select成功、errors=0、elapsed_ms=602287，并正常定时返回loader。完整输入和原图哈希见 [机器记录](hakios-device-r8.json)。

镜像SHA256：`e0d0394a99d41fa64b727752632215d8d87869cf7ba02ceb37a830857aa26e4f`；内核SHA256：`cea35d92711dec40712b1044174df8c49b78f20ae1a0a3d3e346364989a97b01`。完整HakiOS固定提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2。没有把第七轮照片当成本轮直接证据；UEFI加载前置完成由本轮内核运行推得。

第一张显示三组采样，第二张覆盖九组：

| TIMER PPI27 | heartbeat | SELECT wakes | errors | elapsed_ms | nvcsw | nivcsw |
| --- | --- | --- | --- | --- | --- | --- |
| 2995 | 6 | 60 | 0 | 60238 | 60 | 6 |
| 6586 | 12 | 120 | 0 | 120465 | 120 | 6 |
| 10179 | 18 | 180 | 0 | 180693 | 180 | 6 |
| 13770 | 24 | 240 | 0 | 240922 | 240 | 6 |
| 17366 | 30 | 300 | 0 | 301148 | 300 | 6 |
| 20956 | 36 | 360 | 0 | 361377 | 360 | 6 |
| 24546 | 42 | 420 | 0 | 421604 | 420 | 6 |
| 28141 | 48 | 480 | 0 | 481831 | 480 | 6 |
| 31733 | 54 | 540 | 0 | 542059 | 540 | 6 |

各组memory的free/available为469 MiB，wired为42 MiB。PPI27只在实际确认虚拟timer IRQ的分支递增；timer行与select行是相邻的独立输出，不能当作同一时刻的原子快照。用户确认“已进入 Hello，日志继续增加”和“看起来一切正常”。

第三张结束屏显示：

```text
LITHIUM RETURN LOG 17236 BYTES
KERNEL TIMED RETURN OBSERVED
LITHIUM SELECT WAKES 600 ERRORS 0 ELAPSED MS 602287 NVCSW 600 NIVCSW 6
LITHIUM HELLO WAIT COMPLETE KEEPING PID1 ALIVE FOR LOADER TIMED RETURN
RAW LOG RETAINED IN RAM SCREEN SHOWS TEXT SUMMARY
LITHIUM TEST ENDED USE POWER VOLUME DOWN FOR FASTBOOT
```

这是本轮正常定时返回的直接证据，未见panic原因行。PID1没有在600次等待后主动退出；有界清屏摘要已改善第七轮返回乱码。原始RAM日志未读回电脑，照片不是完整原始日志。终屏未显示最后的LITHIUM TIMER行，不能推算最终PPI计数。

现有证据记录一次600次成功等待和正常返回，用户程序的内核时钟间隔为602.287秒。独立外部时长尚未核验，最后直接timer样本只到heartbeat54、相邻select时长542059ms，完整10分钟timer/线程及连续3次启动验收尚未通过。下轮需拍600次前后timer/select，并对照独立计时保存结束摘要。660秒返回从内核EARLY_BOOT计时，不能仅凭参数认定hello运行时长。

用户随后报告手机没电、正在充电，要求先准备镜像。复测副本已从本轮冻结镜像逐字节保持不变并核验SHA256，见 [准备记录](hakios-repeat-ready.json)。未确认回Fastboot，本次证据整理和镜像准备没有发出新的设备启动或写入命令。

正常上游完整构建、ARMv8指令、栈/内存、来源检查和9项路线隔离回归此前通过。本轮保留启动时的构建报告，运行结果由独立设备报告记录。hello/mockfs仍只验平台；iOS14兼容、UFS、GPU和触摸未验证。累计八次HakiOS临时boot，没有分区写入、旧7195执行或QEMU。
